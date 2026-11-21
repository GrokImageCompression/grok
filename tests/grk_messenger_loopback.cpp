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
 * Multi-process loopback test for the shared memory Messenger protocol.
 *
 * Usage:
 *   grk_messenger_loopback              — client mode (default): launches server, exchanges frames
 *   grk_messenger_loopback --server     — server mode: receives uncompressed, sends compressed
 *
 * The client spawns the server as a child process via Messenger::launch().
 * They communicate over POSIX shared memory + named semaphores.
 *
 * Protocol flow:
 *   Server: sends GRK_MSGR_BATCH_COMPRESS_INIT to client
 *   Client: submits N uncompressed frames via GRK_MSGR_BATCH_SUBMIT_UNCOMPRESSED
 *   Server: for each frame, sends GRK_MSGR_BATCH_PROCESSED_UNCOMPRESSED + SUBMIT_COMPRESSED
 *   Client: receives compressed frames, validates, sends PROCESSSED_COMPRESSED
 *   Client: sends FLUSH + SHUTDOWN
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "Messenger.h"

using namespace grk_plugin;
using namespace std::chrono_literals;

static const uint32_t TEST_WIDTH = 64;
static const uint32_t TEST_HEIGHT = 64;
static const uint32_t TEST_SAMPLES = 3;
static const size_t TEST_NUM_FRAMES = 8;
static const uint8_t COMPRESS_MARKER = 0xAA;

/*************************** Server Mode *******************************/
static int runServer()
{
  setMessengerLogger(new MessengerLogger("[Server] "));
  getMessengerLogger()->info("Starting server");

  auto uncompressedFrameSize =
      Messenger::uncompressedFrameSize(TEST_WIDTH, TEST_HEIGHT, TEST_SAMPLES);
  auto compressedFrameSize = uncompressedFrameSize;

  MessengerBlockingQueue<std::string> processingQueue;

  auto proc = [&](const std::string& str) {
    if(str.empty())
      return;
    Msg msg(str);
    auto tag = msg.next();
    if(tag == GRK_MSGR_BATCH_SUBMIT_UNCOMPRESSED)
    {
      processingQueue.push(str);
    }
    else if(tag == GRK_MSGR_BATCH_SHUTDOWN)
    {
      processingQueue.deactivate();
    }
    else if(tag == GRK_MSGR_BATCH_FLUSH)
    {
      // acknowledged
    }
  };

  MessengerInit init(false, grokToClientMessageBuf, grokSentSynch, clientReceiveReadySynch,
                     clientToGrokMessageBuf, clientSentSynch, grokReceiveReadySynch, proc, 2,
                     uncompressedFrameSize, compressedFrameSize, TEST_NUM_FRAMES);
  Messenger server(init);

  // send COMPRESS_INIT to client
  server.send(GRK_MSGR_BATCH_COMPRESS_INIT, TEST_WIDTH, TEST_WIDTH, TEST_HEIGHT, TEST_SAMPLES, 12,
              compressedFrameSize, TEST_NUM_FRAMES);
  getMessengerLogger()->info("Sent COMPRESS_INIT");

  // process incoming uncompressed frames
  std::string str;
  while(processingQueue.waitAndPop(str))
  {
    Msg msg(str);
    msg.next(); // skip tag
    auto clientFrameId = msg.nextUint();
    auto uncompressedFrameId = msg.nextUint();

    // simulate compression: write marker byte pattern to compressed buffer
    BufferSrc compressed;
    if(!server.availableBuffers_.waitAndPop(compressed))
      break;

    auto* compPtr = server.getCompressedFrame(compressed.frameId_);
    if(compPtr)
      memset(compPtr, COMPRESS_MARKER, compressedFrameSize);

    // release uncompressed buffer back to client
    server.send(GRK_MSGR_BATCH_PROCESSED_UNCOMPRESSED, uncompressedFrameId);
    // send compressed result to client
    server.send(GRK_MSGR_BATCH_SUBMIT_COMPRESSED, clientFrameId, compressed.frameId_,
                compressedFrameSize);

    getMessengerLogger()->info("Processed frame %d", clientFrameId);
  }

  getMessengerLogger()->info("Server shutting down");
  return 0;
}

/*************************** Client Mode *******************************/
static int runClient(const char* selfPath)
{
  setMessengerLogger(new MessengerLogger("[Client] "));
  getMessengerLogger()->info("Starting client");

  std::atomic<int> framesCompressed{0};
  std::mutex completeMutex;
  std::condition_variable completeCondition;
  bool allValid = true;
  Messenger* messenger = nullptr;

  auto proc = [&](const std::string& str) {
    Msg msg(str);
    auto tag = msg.next();
    if(tag == GRK_MSGR_BATCH_SUBMIT_COMPRESSED)
    {
      auto clientFrameId = msg.nextUint();
      auto compressedFrameId = msg.nextUint();
      auto compressedLength = msg.nextUint();

      // validate compressed data has marker byte
      auto* ptr = messenger->getCompressedFrame(compressedFrameId);
      if(!ptr || (uint8_t)ptr[0] != COMPRESS_MARKER)
      {
        getMessengerLogger()->error("Frame %d: compressed data validation failed", clientFrameId);
        allValid = false;
      }
      else
      {
        getMessengerLogger()->info("Frame %d: compressed data validated (%d bytes)", clientFrameId,
                                   compressedLength);
      }

      // release compressed buffer
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

  // launch server as child process
  std::string serverCmd = std::string(selfPath) + " --server";
  getMessengerLogger()->info("Launching server: %s", serverCmd.c_str());
  if(!messenger->launch(serverCmd, ""))
  {
    getMessengerLogger()->error("Failed to launch server");
    delete messenger;
    return 1;
  }

  // wait for COMPRESS_INIT from server
  if(!messenger->waitForClientInit())
  {
    getMessengerLogger()->error("Client initialization failed");
    delete messenger;
    return 1;
  }
  getMessengerLogger()->info("Client initialized, submitting %d frames", (int)TEST_NUM_FRAMES);

  // submit uncompressed frames
  for(size_t i = 0; i < TEST_NUM_FRAMES; ++i)
  {
    BufferSrc src;
    if(!messenger->availableBuffers_.waitAndPop(src))
    {
      getMessengerLogger()->error("No available buffers");
      delete messenger;
      return 1;
    }
    // write test pattern: each frame filled with its index byte
    auto* ptr = messenger->getUncompressedFrame(src.frameId_);
    if(ptr)
      memset(ptr, (uint8_t)(i & 0xFF), messenger->init_.uncompressedFrameSize_);
    messenger->send(GRK_MSGR_BATCH_SUBMIT_UNCOMPRESSED, i, src.frameId_);
    getMessengerLogger()->info("Submitted uncompressed frame %d", (int)i);
  }

  // wait for all compressed frames to arrive
  {
    std::unique_lock<std::mutex> lk(completeMutex);
    bool done = completeCondition.wait_for(
        lk, 30s, [&] { return framesCompressed >= (int)TEST_NUM_FRAMES; });
    if(!done)
    {
      getMessengerLogger()->error("Timeout waiting for compressed frames (%d/%d)",
                                  (int)framesCompressed, (int)TEST_NUM_FRAMES);
      delete messenger;
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
      getMessengerLogger()->error("Server exited with code %d", result);
      delete messenger;
      return 1;
    }
  }

  delete messenger;

  if(!allValid)
  {
    fprintf(stderr, "FAIL: some frames had invalid compressed data\n");
    return 1;
  }

  fprintf(stdout, "\n========================================\n");
  fprintf(stdout, "Loopback test: %d/%d frames processed and validated\n", (int)framesCompressed,
          (int)TEST_NUM_FRAMES);
  fprintf(stdout, "PASS\n");
  fprintf(stdout, "========================================\n");
  return 0;
}

/*************************** Main *******************************/
int main(int argc, char** argv)
{
  if(argc > 1 && std::string(argv[1]) == "--server")
    return runServer();

  return runClient(argv[0]);
}
