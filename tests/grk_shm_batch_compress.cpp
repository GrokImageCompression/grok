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
 * Two-process loopback test for CPU shared memory batch compression.
 *
 * This test acts as a SHM client that launches grk_compress as a server
 * process (two separate OS processes communicating via POSIX shared memory
 * and named semaphores).
 *
 * Protocol:
 *   1. Client launches grk_compress with --batch-src GRK_MSGR_BATCH_IMAGE,...
 *   2. grk_compress creates Messenger server, sends COMPRESS_INIT
 *   3. Client submits raw 16-bit RGB frames via SUBMIT_UNCOMPRESSED
 *   4. grk_compress compresses each frame to J2K, sends back via SUBMIT_COMPRESSED
 *   5. Client validates: J2K SOC marker, then decompresses and checks pixel fidelity
 *   6. Client sends FLUSH + SHUTDOWN, waits for server exit code
 *
 * Validation:
 *   - J2K SOC marker (0xFF 0x4F) present in every compressed frame
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

// stored compressed frame for post-hoc decompression validation
struct CompressedFrame
{
  uint32_t clientFrameId;
  std::vector<uint8_t> data;
};

// generate test pattern: interleaved 16-bit pixels, stored per-frame
static void fillTestPattern(uint16_t* dst, size_t frameIndex)
{
  size_t numSamples = (size_t)TEST_WIDTH * TEST_HEIGHT * TEST_SAMPLES;
  for(size_t s = 0; s < numSamples; ++s)
    dst[s] = (uint16_t)((frameIndex * 100 + s) & 0x0FFF); // 12-bit range
}

// decompress a J2K codestream and validate pixel fidelity
static bool validateDecompressedFrame(const CompressedFrame& frame, size_t frameIndex)
{
  grk_decompress_parameters dparams{};
  grk_stream_params streamParams{};
  streamParams.buf = const_cast<uint8_t*>(frame.data.data());
  streamParams.buf_len = frame.data.size();
  streamParams.is_read_stream = true;

  grk_object* codec = grk_decompress_init(&streamParams, &dparams);
  if(!codec)
  {
    fprintf(stderr, "  Frame %u: grk_decompress_init failed\n", frame.clientFrameId);
    return false;
  }
  grk_header_info headerInfo{};
  if(!grk_decompress_read_header(codec, &headerInfo))
  {
    fprintf(stderr, "  Frame %u: grk_decompress_read_header failed\n", frame.clientFrameId);
    grk_object_unref(codec);
    return false;
  }
  if(!grk_decompress(codec, nullptr))
  {
    fprintf(stderr, "  Frame %u: grk_decompress failed\n", frame.clientFrameId);
    grk_object_unref(codec);
    return false;
  }
  grk_image* image = grk_decompress_get_image(codec);
  if(!image)
  {
    fprintf(stderr, "  Frame %u: grk_decompress_get_image returned null\n", frame.clientFrameId);
    grk_object_unref(codec);
    return false;
  }

  // validate dimensions
  if(image->x1 != TEST_WIDTH || image->y1 != TEST_HEIGHT || image->numcomps != TEST_SAMPLES)
  {
    fprintf(stderr, "  Frame %u: dimension mismatch (%u x %u x %u, expected %u x %u x %u)\n",
            frame.clientFrameId, image->x1, image->y1, image->numcomps, TEST_WIDTH, TEST_HEIGHT,
            TEST_SAMPLES);
    grk_object_unref(codec);
    return false;
  }

  // validate pixel data (lossless at 12-bit, tolerance = 0)
  size_t numPixels = (size_t)TEST_WIDTH * TEST_HEIGHT;
  int maxError = 0;
  int errorCount = 0;
  for(uint32_t c = 0; c < TEST_SAMPLES; ++c)
  {
    auto* compData = static_cast<int32_t*>(image->comps[c].data);
    uint32_t stride = image->comps[c].stride;
    for(uint32_t y = 0; y < TEST_HEIGHT; ++y)
    {
      for(uint32_t x = 0; x < TEST_WIDTH; ++x)
      {
        size_t srcIdx = (y * TEST_WIDTH + x) * TEST_SAMPLES + c;
        int32_t expected = (int32_t)((frameIndex * 100 + srcIdx) & 0x0FFF);
        int32_t actual = compData[y * stride + x];
        int err = abs(actual - expected);
        if(err > 0 && errorCount < 5)
        {
          fprintf(stderr, "    [c=%u y=%u x=%u] actual=%d expected=%d (srcIdx=%zu)\n", c, y, x,
                  actual, expected, srcIdx);
          errorCount++;
        }
        if(err > maxError)
          maxError = err;
      }
    }
  }

  grk_object_unref(codec);

  if(maxError > 0)
  {
    fprintf(stderr, "  Frame %u: pixel mismatch (max error = %d)\n", frame.clientFrameId, maxError);
    return false;
  }

  return true;
}

int main(int argc, char** argv)
{
  if(argc < 2)
  {
    fprintf(stderr, "Usage: %s <path-to-grk_compress>\n", argv[0]);
    return 1;
  }
  const char* compressPath = argv[1];

  setMessengerLogger(new MessengerLogger("[SHM-Client] "));

  // initialize grok library for decompression validation
  grk_initialize(nullptr, 0, nullptr);

  std::atomic<int> framesCompressed{0};
  std::mutex completeMutex;
  std::condition_variable completeCondition;
  bool allValid = true;
  Messenger* messenger = nullptr;

  // store compressed frames for post-hoc decompression
  std::mutex framesMutex;
  std::vector<CompressedFrame> compressedFrames;

  auto proc = [&](const std::string& str) {
    Msg msg(str);
    auto tag = msg.next();
    if(tag == GRK_MSGR_BATCH_SUBMIT_COMPRESSED)
    {
      auto clientFrameId = msg.nextUint();
      auto compressedFrameId = msg.nextUint();
      auto compressedLength = msg.nextUint();

      auto* ptr = messenger->getCompressedFrame(compressedFrameId);
      if(!ptr || compressedLength < 2)
      {
        getMessengerLogger()->error("Frame %d: null/empty compressed data", clientFrameId);
        allValid = false;
      }
      else
      {
        // J2K SOC marker check
        if(ptr[0] == 0xFF && ptr[1] == 0x4F)
        {
          getMessengerLogger()->info("Frame %d: valid J2K codestream (%d bytes)", clientFrameId,
                                     compressedLength);
          // save for decompression validation
          CompressedFrame cf;
          cf.clientFrameId = clientFrameId;
          cf.data.assign(ptr, ptr + compressedLength);
          {
            std::lock_guard<std::mutex> lk(framesMutex);
            compressedFrames.push_back(std::move(cf));
          }
        }
        else
        {
          getMessengerLogger()->error("Frame %d: invalid J2K header (0x%02X 0x%02X)", clientFrameId,
                                      (unsigned)ptr[0], (unsigned)ptr[1]);
          allValid = false;
        }
      }

      messenger->send(GRK_MSGR_BATCH_PROCESSSED_COMPRESSED, compressedFrameId);

      auto count = ++framesCompressed;
      if(count == (int)TEST_NUM_FRAMES)
        completeCondition.notify_one();
    }
  };

  MessengerInit init(true, clientToGrokMessageBuf, clientSentSynch, grokReceiveReadySynch,
                     grokToClientMessageBuf, grokSentSynch, clientReceiveReadySynch, proc,
                     std::thread::hardware_concurrency());
  messenger = new Messenger(init);

  // build grk_compress command with shared memory batch source
  char batchSrc[256];
  snprintf(batchSrc, sizeof(batchSrc), "GRK_MSGR_BATCH_IMAGE,%u,%u,%u,%u,%u", TEST_WIDTH,
           TEST_WIDTH, TEST_HEIGHT, TEST_SAMPLES, TEST_DEPTH);

  std::string cmd = std::string(compressPath) + " -G -2" + " -y " + batchSrc + " -O j2k";

  getMessengerLogger()->info("Launching: %s", cmd.c_str());
  if(!messenger->launch(cmd, ""))
  {
    getMessengerLogger()->error("Failed to launch grk_compress");
    delete messenger;
    grk_deinitialize();
    return 1;
  }

  if(!messenger->waitForClientInit())
  {
    getMessengerLogger()->error("Client initialization failed (server did not send COMPRESS_INIT)");
    delete messenger;
    grk_deinitialize();
    return 1;
  }
  getMessengerLogger()->info("Client initialized, submitting %d frames", (int)TEST_NUM_FRAMES);

  // submit uncompressed frames
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
    auto* ptr = reinterpret_cast<uint16_t*>(messenger->getUncompressedFrame(src.frameId_));
    if(ptr)
      fillTestPattern(ptr, i);
    messenger->send(GRK_MSGR_BATCH_SUBMIT_UNCOMPRESSED, i, src.frameId_);
    getMessengerLogger()->info("Submitted uncompressed frame %d", (int)i);
  }

  // wait for all compressed frames to arrive
  {
    std::unique_lock<std::mutex> lk(completeMutex);
    bool done = completeCondition.wait_for(
        lk, 60s, [&] { return framesCompressed >= (int)TEST_NUM_FRAMES; });
    if(!done)
    {
      getMessengerLogger()->error("Timeout waiting for compressed frames (%d/%d)",
                                  (int)framesCompressed, (int)TEST_NUM_FRAMES);
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
      getMessengerLogger()->error("grk_compress exited with code %d", result);
      delete messenger;
      grk_deinitialize();
      return 1;
    }
  }

  delete messenger;

  // Phase 2: decompress each compressed frame and validate pixel fidelity
  fprintf(stdout, "\n--- Decompression validation ---\n");
  int decompressedCount = 0;
  for(const auto& cf : compressedFrames)
  {
    fprintf(stdout, "  Validating frame %u...\n", cf.clientFrameId);
    if(validateDecompressedFrame(cf, cf.clientFrameId))
    {
      fprintf(stdout, "  Frame %u: pixel-perfect round-trip OK\n", cf.clientFrameId);
      decompressedCount++;
    }
    else
    {
      allValid = false;
    }
  }

  grk_deinitialize();

  if(!allValid || decompressedCount != (int)TEST_NUM_FRAMES)
  {
    fprintf(stderr, "FAIL: validation failed (%d/%d frames passed decompression check)\n",
            decompressedCount, (int)TEST_NUM_FRAMES);
    return 1;
  }

  fprintf(stdout, "\n========================================\n");
  fprintf(stdout, "SHM batch compress loopback test:\n");
  fprintf(stdout, "  %d/%d frames compressed via SHM\n", (int)framesCompressed,
          (int)TEST_NUM_FRAMES);
  fprintf(stdout, "  %d/%d frames decompressed and pixel-validated\n", decompressedCount,
          (int)TEST_NUM_FRAMES);
  fprintf(stdout, "  Server process exited cleanly\n");
  fprintf(stdout, "PASS\n");
  fprintf(stdout, "========================================\n");
  return 0;
}
