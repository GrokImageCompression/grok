/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This source code is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
 * Two-process loopback test for CPU shared memory batch decompression.
 *
 * This test acts as a SHM client that launches grk_decompress as a server
 * process (two separate OS processes communicating via POSIX shared memory
 * and named semaphores).
 *
 * Protocol:
 *   1. Client compresses test frames in-process (via grk API)
 *   2. Client launches grk_decompress with --batch-src GRK_MSGR_BATCH_IMAGE,...
 *   3. grk_decompress creates Messenger server, sends DECOMPRESS_INIT
 *   4. Client submits compressed J2K frames via SUBMIT_COMPRESSED
 *   5. grk_decompress decompresses each frame, sends back via SUBMIT_UNCOMPRESSED
 *   6. Client validates pixel fidelity of received raw data
 *   7. Client sends FLUSH + SHUTDOWN, waits for server exit code
 *
 * Validation:
 *   - Decompressed image dimensions match original (width, height, components)
 *   - Pixel values round-trip within acceptable tolerance (lossless for 12-bit)
 *   - Server process exits with code 0
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Messenger.h"
#include "grok.h"

using namespace grk_plugin;
using namespace std::chrono_literals;

static const uint32_t TEST_WIDTH = 64;
static const uint32_t TEST_HEIGHT = 64;
static const uint32_t TEST_SAMPLES = 3;
static const uint32_t TEST_DEPTH = 12;
static const size_t TEST_NUM_FRAMES = 4;

// stored compressed frame
struct CompressedFrame
{
  uint32_t clientFrameId;
  std::vector<uint8_t> data;
};

// generate test pattern: interleaved 16-bit pixels
static void fillTestPattern(uint16_t* dst, size_t frameIndex)
{
  size_t numSamples = (size_t)TEST_WIDTH * TEST_HEIGHT * TEST_SAMPLES;
  for(size_t s = 0; s < numSamples; ++s)
    dst[s] = (uint16_t)((frameIndex * 100 + s) & 0x0FFF); // 12-bit range
}

// compress a test frame in-process, return the J2K codestream
static bool compressFrame(size_t frameIndex, CompressedFrame& out)
{
  grk_image_comp cmptparms[TEST_SAMPLES];
  for(uint32_t c = 0; c < TEST_SAMPLES; c++)
  {
    memset(&cmptparms[c], 0, sizeof(grk_image_comp));
    cmptparms[c].dx = 1;
    cmptparms[c].dy = 1;
    cmptparms[c].w = TEST_WIDTH;
    cmptparms[c].h = TEST_HEIGHT;
    cmptparms[c].prec = (uint8_t)TEST_DEPTH;
    cmptparms[c].sgnd = false;
  }
  grk_image* image = grk_image_new((uint16_t)TEST_SAMPLES, cmptparms, GRK_CLRSPC_SRGB, true);
  if(!image)
    return false;

  image->x0 = 0;
  image->y0 = 0;
  image->x1 = TEST_WIDTH;
  image->y1 = TEST_HEIGHT;

  // fill with test pattern (same as the SHM test pattern)
  for(uint32_t y = 0; y < TEST_HEIGHT; y++)
  {
    for(uint32_t x = 0; x < TEST_WIDTH; x++)
    {
      for(uint32_t c = 0; c < TEST_SAMPLES; c++)
      {
        size_t srcIdx = (y * TEST_WIDTH + x) * TEST_SAMPLES + c;
        ((int32_t*)image->comps[c].data)[y * TEST_WIDTH + x] =
            (int32_t)((frameIndex * 100 + srcIdx) & 0x0FFF);
      }
    }
  }

  size_t bufLen = TEST_WIDTH * TEST_HEIGHT * TEST_SAMPLES * 2 * 3 / 2;
  std::vector<uint8_t> buf(bufLen, 0);

  grk_cparameters params{};
  grk_compress_set_default_params(&params);
  params.cod_format = GRK_FMT_J2K;
  params.mct = (TEST_SAMPLES >= 3) ? 1 : 0;

  grk_stream_params sp{};
  sp.buf = buf.data();
  sp.buf_len = bufLen;

  grk_object* codec = grk_compress_init(&sp, &params, image);
  uint64_t compLen = 0;
  if(codec)
  {
    compLen = grk_compress(codec, nullptr);
    grk_object_unref(codec);
  }
  grk_object_unref(&image->obj);

  if(!compLen)
    return false;

  out.clientFrameId = (uint32_t)frameIndex;
  out.data.assign(buf.data(), buf.data() + compLen);
  return true;
}

int main(int argc, char** argv)
{
  if(argc < 2)
  {
    fprintf(stderr, "Usage: %s <path-to-grk_decompress>\n", argv[0]);
    return 1;
  }
  const char* decompressPath = argv[1];

  setMessengerLogger(new MessengerLogger("[SHM-DClient] "));

  // initialize grok library for compression
  grk_initialize(nullptr, 0, nullptr);

  // Phase 1: compress test frames in-process
  std::vector<CompressedFrame> compressedFrames(TEST_NUM_FRAMES);
  for(size_t i = 0; i < TEST_NUM_FRAMES; i++)
  {
    if(!compressFrame(i, compressedFrames[i]))
    {
      fprintf(stderr, "Failed to compress frame %zu\n", i);
      grk_deinitialize();
      return 1;
    }
    fprintf(stdout, "  Compressed frame %zu: %zu bytes\n", i, compressedFrames[i].data.size());
  }

  // Phase 2: send compressed frames via SHM to grk_decompress, receive decompressed
  std::atomic<int> framesDecompressed{0};
  std::mutex completeMutex;
  std::condition_variable completeCondition;
  bool allValid = true;
  Messenger* messenger = nullptr;

  // store received uncompressed frames for validation
  struct DecompressedFrame
  {
    uint32_t clientFrameId;
    std::vector<uint16_t> pixels;
  };
  std::mutex dframesMutex;
  std::vector<DecompressedFrame> decompressedFrames;

  auto proc = [&](const std::string& str) {
    Msg msg(str);
    auto tag = msg.next();
    if(tag == GRK_MSGR_BATCH_SUBMIT_UNCOMPRESSED)
    {
      auto clientFrameId = msg.nextUint();
      auto uncompressedFrameId = msg.nextUint();

      auto* ptr = reinterpret_cast<uint16_t*>(messenger->getUncompressedFrame(uncompressedFrameId));
      if(!ptr)
      {
        getMessengerLogger()->error("Frame %d: null uncompressed data", clientFrameId);
        allValid = false;
      }
      else
      {
        getMessengerLogger()->info("Frame %d: received decompressed data", clientFrameId);
        // copy pixels for validation
        size_t numSamples = (size_t)TEST_WIDTH * TEST_HEIGHT * TEST_SAMPLES;
        DecompressedFrame df;
        df.clientFrameId = clientFrameId;
        df.pixels.assign(ptr, ptr + numSamples);
        {
          std::lock_guard<std::mutex> lk(dframesMutex);
          decompressedFrames.push_back(std::move(df));
        }
      }

      messenger->send(GRK_MSGR_BATCH_PROCESSED_UNCOMPRESSED, uncompressedFrameId);

      auto count = ++framesDecompressed;
      if(count == (int)TEST_NUM_FRAMES)
        completeCondition.notify_one();
    }
  };

  MessengerInit init(true, clientToGrokMessageBuf, clientSentSynch, grokReceiveReadySynch,
                     grokToClientMessageBuf, grokSentSynch, clientReceiveReadySynch, proc,
                     std::thread::hardware_concurrency());
  messenger = new Messenger(init);

  // build grk_decompress command with shared memory batch source
  char batchSrc[256];
  snprintf(batchSrc, sizeof(batchSrc), "GRK_MSGR_BATCH_IMAGE,%u,%u,%u,%u,%u", TEST_WIDTH,
           TEST_WIDTH, TEST_HEIGHT, TEST_SAMPLES, TEST_DEPTH);

  std::string cmd = std::string(decompressPath) + " -G -2" + " -y " + batchSrc;

  getMessengerLogger()->info("Launching: %s", cmd.c_str());
  if(!messenger->launch(cmd, ""))
  {
    getMessengerLogger()->error("Failed to launch grk_decompress");
    delete messenger;
    grk_deinitialize();
    return 1;
  }

  if(!messenger->waitForClientInit())
  {
    getMessengerLogger()->error("Client initialization failed");
    delete messenger;
    grk_deinitialize();
    return 1;
  }
  getMessengerLogger()->info("Client initialized, submitting %d compressed frames",
                             (int)TEST_NUM_FRAMES);

  // submit compressed frames
  for(size_t i = 0; i < TEST_NUM_FRAMES; ++i)
  {
    BufferSrc src;
    if(!messenger->availableBuffers_.waitAndPop(src))
    {
      getMessengerLogger()->error("No available buffers for frame %d", (int)i);
      delete messenger;
      grk_deinitialize();
      return 1;
    }
    auto* ptr = messenger->getCompressedFrame(src.frameId_);
    if(ptr)
    {
      memcpy(ptr, compressedFrames[i].data.data(), compressedFrames[i].data.size());
    }
    messenger->send(GRK_MSGR_BATCH_SUBMIT_COMPRESSED, (uint32_t)i, src.frameId_,
                    (uint32_t)compressedFrames[i].data.size());
    getMessengerLogger()->info("Submitted compressed frame %d (%d bytes)", (int)i,
                               (int)compressedFrames[i].data.size());
  }

  // wait for all decompressed frames to arrive
  {
    std::unique_lock<std::mutex> lk(completeMutex);
    bool done = completeCondition.wait_for(
        lk, 60s, [&] { return framesDecompressed >= (int)TEST_NUM_FRAMES; });
    if(!done)
    {
      getMessengerLogger()->error("Timeout waiting for decompressed frames (%d/%d)",
                                  (int)framesDecompressed, (int)TEST_NUM_FRAMES);
      delete messenger;
      grk_deinitialize();
      return 1;
    }
  }

  // send flush and shutdown
  messenger->send(GRK_MSGR_BATCH_FLUSH, (uint32_t)TEST_NUM_FRAMES);
  messenger->send(GRK_MSGR_BATCH_SHUTDOWN);

  // wait for server process to exit
  if(messenger->async_result_.valid())
  {
    int result = messenger->async_result_.get();
    if(result != 0)
    {
      getMessengerLogger()->error("grk_decompress exited with code %d", result);
      delete messenger;
      grk_deinitialize();
      return 1;
    }
  }

  delete messenger;

  // Phase 3: validate pixel fidelity
  fprintf(stdout, "\n--- Decompression validation ---\n");
  int validCount = 0;
  for(const auto& df : decompressedFrames)
  {
    size_t frameIndex = df.clientFrameId;
    int maxError = 0;
    int errorCount = 0;
    size_t numSamples = (size_t)TEST_WIDTH * TEST_HEIGHT * TEST_SAMPLES;
    for(size_t s = 0; s < numSamples; ++s)
    {
      int32_t expected = (int32_t)((frameIndex * 100 + s) & 0x0FFF);
      int32_t actual = (int32_t)df.pixels[s];
      int err = abs(actual - expected);
      if(err > 0 && errorCount < 5)
      {
        fprintf(stderr, "    Frame %u [s=%zu]: actual=%d expected=%d\n", df.clientFrameId, s,
                actual, expected);
        errorCount++;
      }
      if(err > maxError)
        maxError = err;
    }
    if(maxError == 0)
    {
      fprintf(stdout, "  Frame %u: pixel-perfect round-trip OK\n", df.clientFrameId);
      validCount++;
    }
    else
    {
      fprintf(stdout, "  Frame %u: FAIL (max error = %d)\n", df.clientFrameId, maxError);
      allValid = false;
    }
  }

  grk_deinitialize();

  if(!allValid || validCount != (int)TEST_NUM_FRAMES)
  {
    fprintf(stderr, "FAIL: validation failed (%d/%d frames passed)\n", validCount,
            (int)TEST_NUM_FRAMES);
    return 1;
  }

  fprintf(stdout, "\n========================================\n");
  fprintf(stdout, "SHM batch decompress loopback test:\n");
  fprintf(stdout, "  %d/%d frames decompressed via SHM\n", (int)framesDecompressed,
          (int)TEST_NUM_FRAMES);
  fprintf(stdout, "  %d/%d frames pixel-validated\n", validCount, (int)TEST_NUM_FRAMES);
  fprintf(stdout, "  Server process exited cleanly\n");
  fprintf(stdout, "PASS\n");
  fprintf(stdout, "========================================\n");
  return 0;
}
