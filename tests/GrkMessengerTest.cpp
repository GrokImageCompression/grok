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

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "GrkMessengerTest.h"
#include "Messenger.h"

using namespace grk_plugin;
using namespace std::chrono_literals;

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg)                                        \
  do                                                                  \
  {                                                                   \
    if(!(cond))                                                       \
    {                                                                 \
      fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
      tests_failed++;                                                 \
      return;                                                         \
    }                                                                 \
  } while(0)

#define TEST_PASS(name)                    \
  do                                       \
  {                                        \
    fprintf(stdout, "  PASS: %s\n", name); \
    tests_passed++;                        \
  } while(0)

/*************************** Msg Parser Tests *******************************/
static void testMsgBasicParsing()
{
  Msg msg("hello,world,42");
  TEST_ASSERT(msg.next() == "hello", "first token should be hello");
  TEST_ASSERT(msg.next() == "world", "second token should be world");
  TEST_ASSERT(msg.nextUint() == 42, "third token should be 42");
  TEST_PASS("testMsgBasicParsing");
}

static void testMsgSingleToken()
{
  Msg msg("single");
  TEST_ASSERT(msg.next() == "single", "single token should parse");
  // exhausted — next should return empty
  TEST_ASSERT(msg.next() == "", "exhausted should return empty");
  TEST_PASS("testMsgSingleToken");
}

static void testMsgEmptyString()
{
  Msg msg("");
  auto tok = msg.next();
  TEST_ASSERT(tok == "", "empty string should yield empty token");
  TEST_PASS("testMsgEmptyString");
}

static void testMsgProtocolMessage()
{
  std::string raw = GRK_MSGR_BATCH_COMPRESS_INIT + ",1920,1920,1080,3,12,2073600,16";
  Msg msg(raw);
  TEST_ASSERT(msg.next() == GRK_MSGR_BATCH_COMPRESS_INIT, "tag should match");
  TEST_ASSERT(msg.nextUint() == 1920, "width");
  TEST_ASSERT(msg.nextUint() == 1920, "stride");
  TEST_ASSERT(msg.nextUint() == 1080, "height");
  TEST_ASSERT(msg.nextUint() == 3, "samplesPerPixel");
  TEST_ASSERT(msg.nextUint() == 12, "depth");
  TEST_ASSERT(msg.nextUint() == 2073600, "compressedFrameSize");
  TEST_ASSERT(msg.nextUint() == 16, "numFrames");
  TEST_PASS("testMsgProtocolMessage");
}

/*************************** BlockingQueue Tests *******************************/
static void testQueuePushPop()
{
  MessengerBlockingQueue<int> q;
  TEST_ASSERT(q.push(10), "push should succeed");
  TEST_ASSERT(q.push(20), "push should succeed");
  TEST_ASSERT(q.size() == 2, "size should be 2");

  int val = 0;
  TEST_ASSERT(q.pop(val), "pop should succeed");
  TEST_ASSERT(val == 10, "first pop should be 10");
  TEST_ASSERT(q.pop(val), "pop should succeed");
  TEST_ASSERT(val == 20, "second pop should be 20");
  TEST_ASSERT(!q.pop(val), "pop on empty should fail");
  TEST_PASS("testQueuePushPop");
}

static void testQueueMaxSize()
{
  MessengerBlockingQueue<int> q(2);
  TEST_ASSERT(q.push(1), "push 1 should succeed");
  TEST_ASSERT(q.push(2), "push 2 should succeed");
  TEST_ASSERT(!q.push(3), "push 3 should fail (full)");
  int val;
  q.pop(val);
  TEST_ASSERT(q.push(3), "push 3 should succeed after pop");
  TEST_PASS("testQueueMaxSize");
}

static void testQueueDeactivate()
{
  MessengerBlockingQueue<int> q;
  q.push(1);
  q.push(2);
  q.deactivate();
  TEST_ASSERT(q.size() == 0, "deactivate should clear queue");
  TEST_ASSERT(!q.push(3), "push after deactivate should fail");
  int val;
  TEST_ASSERT(!q.pop(val), "pop after deactivate should fail");
  TEST_PASS("testQueueDeactivate");
}

static void testQueueReactivate()
{
  MessengerBlockingQueue<int> q;
  q.push(1);
  q.deactivate();
  q.activate();
  TEST_ASSERT(q.push(42), "push after reactivate should succeed");
  int val;
  TEST_ASSERT(q.pop(val), "pop after reactivate should succeed");
  TEST_ASSERT(val == 42, "value should be 42");
  TEST_PASS("testQueueReactivate");
}

static void testQueueWaitAndPop()
{
  MessengerBlockingQueue<int> q;
  std::atomic<bool> popped{false};
  int result = 0;

  std::thread consumer([&]() {
    q.waitAndPop(result);
    popped = true;
  });

  // give consumer time to block
  std::this_thread::sleep_for(50ms);
  TEST_ASSERT(!popped, "consumer should be blocking");
  q.push(99);
  consumer.join();
  TEST_ASSERT(popped, "consumer should have popped");
  TEST_ASSERT(result == 99, "popped value should be 99");
  TEST_PASS("testQueueWaitAndPop");
}

static void testQueueWaitAndPush()
{
  MessengerBlockingQueue<int> q(1);
  q.push(1);

  std::atomic<bool> pushed{false};
  int val = 2;
  std::thread producer([&]() {
    q.waitAndPush(val);
    pushed = true;
  });

  std::this_thread::sleep_for(50ms);
  TEST_ASSERT(!pushed, "producer should be blocking");
  int tmp;
  q.pop(tmp); // make room
  producer.join();
  TEST_ASSERT(pushed, "producer should have pushed");
  q.pop(tmp);
  TEST_ASSERT(tmp == 2, "pushed value should be 2");
  TEST_PASS("testQueueWaitAndPush");
}

static void testQueueDeactivateUnblocksWaiters()
{
  MessengerBlockingQueue<int> q;
  std::atomic<bool> returned{false};

  std::thread consumer([&]() {
    int val;
    q.waitAndPop(val); // will block
    returned = true;
  });

  std::this_thread::sleep_for(50ms);
  TEST_ASSERT(!returned, "consumer should be blocking");
  q.deactivate();
  consumer.join();
  TEST_ASSERT(returned, "deactivate should unblock waiter");
  TEST_PASS("testQueueDeactivateUnblocksWaiters");
}

/*************************** BufferSrc Tests *******************************/
static void testBufferSrcDefault()
{
  BufferSrc src;
  TEST_ASSERT(src.file_.empty(), "default file should be empty");
  TEST_ASSERT(src.clientFrameId_ == 0, "default clientFrameId should be 0");
  TEST_ASSERT(src.frameId_ == 0, "default frameId should be 0");
  TEST_ASSERT(src.framePtr_ == nullptr, "default framePtr should be null");
  TEST_ASSERT(!src.fromDisk(), "default should not be fromDisk (empty file + null ptr)");
  TEST_PASS("testBufferSrcDefault");
}

static void testBufferSrcFromFile()
{
  BufferSrc src("test.tif");
  TEST_ASSERT(src.file_ == "test.tif", "file should be test.tif");
  TEST_ASSERT(src.fromDisk(), "should be fromDisk");
  TEST_ASSERT(src.index() == 0, "index should be 0");
  TEST_PASS("testBufferSrcFromFile");
}

static void testBufferSrcFromMemory()
{
  uint8_t data[64];
  BufferSrc src(5, 3, data);
  TEST_ASSERT(src.clientFrameId_ == 5, "clientFrameId should be 5");
  TEST_ASSERT(src.frameId_ == 3, "frameId should be 3");
  TEST_ASSERT(src.framePtr_ == data, "framePtr should match");
  TEST_ASSERT(!src.fromDisk(), "memory source should not be fromDisk");
  TEST_ASSERT(src.index() == 5, "index should be clientFrameId");
  TEST_PASS("testBufferSrcFromMemory");
}

/*************************** ScheduledFrames Tests *******************************/
static void testScheduledFramesStoreRetrieve()
{
  ScheduledFrames<BufferSrc> sf;
  uint8_t buf[32];
  BufferSrc item(10, 0, buf);
  sf.store(item);

  BufferSrc out;
  bool ok = sf.retrieve(10, out);
  TEST_ASSERT(ok, "retrieve should succeed");
  TEST_ASSERT(out.clientFrameId_ == 10, "retrieved clientFrameId should be 10");
  TEST_ASSERT(out.framePtr_ == buf, "retrieved framePtr should match");

  // second retrieve should fail (already removed)
  ok = sf.retrieve(10, out);
  TEST_ASSERT(!ok, "second retrieve should fail");
  TEST_PASS("testScheduledFramesStoreRetrieve");
}

static void testScheduledFramesNonExistent()
{
  ScheduledFrames<BufferSrc> sf;
  BufferSrc out;
  bool ok = sf.retrieve(999, out);
  TEST_ASSERT(!ok, "retrieve non-existent should fail");
  TEST_PASS("testScheduledFramesNonExistent");
}

static void testScheduledFramesDuplicateStore()
{
  ScheduledFrames<BufferSrc> sf;
  uint8_t buf1[1], buf2[1];
  BufferSrc item1(7, 0, buf1);
  BufferSrc item2(7, 1, buf2);
  sf.store(item1);
  sf.store(item2); // duplicate key — should be ignored (first wins)

  BufferSrc out;
  bool ok = sf.retrieve(7, out);
  TEST_ASSERT(ok, "retrieve should succeed");
  TEST_ASSERT(out.framePtr_ == buf1, "should get original, not duplicate");
  TEST_PASS("testScheduledFramesDuplicateStore");
}

/*************************** MessengerInit Tests *******************************/
static void testMessengerInitClient()
{
  auto proc = [](const std::string&) {};
  MessengerInit init(true, "outBuf", "outSent", "outReady", "inBuf", "inSent", "inReady", proc, 4);
  TEST_ASSERT(init.isClient_, "should be client");
  TEST_ASSERT(init.numProcessingThreads_ == 4, "should have 4 processing threads");
  TEST_ASSERT(init.uncompressedFrameSize_ == 0, "client init should have 0 uncompressed size");
  TEST_ASSERT(init.compressedFrameSize_ == 0, "client init should have 0 compressed size");
  TEST_ASSERT(init.numFrames_ == 0, "client init should have 0 frames");
  TEST_PASS("testMessengerInitClient");
}

static void testMessengerInitServer()
{
  auto proc = [](const std::string&) {};
  MessengerInit init(false, "outBuf", "outSent", "outReady", "inBuf", "inSent", "inReady", proc, 2,
                     1024, 512, 8);
  TEST_ASSERT(!init.isClient_, "should not be client");
  TEST_ASSERT(init.uncompressedFrameSize_ == 1024, "uncompressed size should be 1024");
  TEST_ASSERT(init.compressedFrameSize_ == 512, "compressed size should be 512");
  TEST_ASSERT(init.numFrames_ == 8, "numFrames should be 8");
  TEST_PASS("testMessengerInitServer");
}

/*************************** Messenger::send Tests *******************************/
static void testMessengerSendVariadic()
{
  // send() pushes a comma-separated message to sendQueue; test without starting threads
  auto proc = [](const std::string&) {};
  MessengerInit init(true, "outBuf", "outSent", "outReady", "inBuf", "inSent", "inReady", proc, 0,
                     0, 0, 0);
  // construct but do not start threads (isClient with 0 frames means no server init)
  Messenger m(init);
  m.send(GRK_MSGR_BATCH_SUBMIT_UNCOMPRESSED, 42, 7);

  std::string msg;
  TEST_ASSERT(m.sendQueue.pop(msg), "sendQueue should have a message");
  TEST_ASSERT(msg == "GRK_MSGR_BATCH_SUBMIT_UNCOMPRESSED,42,7",
              "message format should be comma-separated");
  TEST_PASS("testMessengerSendVariadic");
}

static void testMessengerSendNoArgs()
{
  auto proc = [](const std::string&) {};
  MessengerInit init(true, "outBuf", "outSent", "outReady", "inBuf", "inSent", "inReady", proc, 0,
                     0, 0, 0);
  Messenger m(init);
  m.send(GRK_MSGR_BATCH_SHUTDOWN);

  std::string msg;
  TEST_ASSERT(m.sendQueue.pop(msg), "sendQueue should have a message");
  TEST_ASSERT(msg == "GRK_MSGR_BATCH_SHUTDOWN", "no-arg send should be just the tag");
  TEST_PASS("testMessengerSendNoArgs");
}

/*************************** Uncompressed Frame Size *******************************/
static void testUncompressedFrameSize()
{
  size_t sz = Messenger::uncompressedFrameSize(1920, 1080, 3);
  // sizeof(uint16_t) * 1920 * 1080 * 3 = 2 * 1920 * 1080 * 3 = 12441600
  TEST_ASSERT(sz == 2 * 1920 * 1080 * 3, "uncompressed frame size calc");
  TEST_PASS("testUncompressedFrameSize");
}

/*************************** Shared Memory / Buffer Accessor Tests *******************************/
// Test that shared memory buffers can be created, accessed, and destroyed
static void testSharedMemoryAllocation()
{
  const size_t bufLen = 4096;
  grk_handle fd = 0;
  char* buf = nullptr;

  bool rc = SharedMemoryManager::initShm("/grk_test_shm_alloc", bufLen, &fd, &buf);
  TEST_ASSERT(rc, "initShm should succeed");
  TEST_ASSERT(buf != nullptr, "buffer should be non-null");

  // write and read back
  memset(buf, 0xBE, bufLen);
  TEST_ASSERT((uint8_t)buf[0] == 0xBE, "written data should persist");
  TEST_ASSERT((uint8_t)buf[bufLen - 1] == 0xBE, "written data at end should persist");

  rc = SharedMemoryManager::deinitShm("/grk_test_shm_alloc", bufLen, fd, &buf);
  TEST_ASSERT(rc, "deinitShm should succeed");
  TEST_ASSERT(buf == nullptr, "buffer should be null after deinit");
  TEST_PASS("testSharedMemoryAllocation");
}

// Test frame accessor methods
static void testFrameAccessors()
{
  auto proc = [](const std::string&) {};
  const size_t uncompressedSize = 256;
  const size_t compressedSize = 128;
  const size_t numFrames = 4;

  MessengerInit init(false, "Global\\grk_test_out", "Global\\grk_test_out_sent",
                     "Global\\grk_test_out_ready", "Global\\grk_test_in",
                     "Global\\grk_test_in_sent", "Global\\grk_test_in_ready", proc, 0,
                     uncompressedSize, compressedSize, numFrames);
  Messenger m(init);

  // verify frame pointers are spaced correctly
  for(size_t i = 0; i < numFrames; ++i)
  {
    auto* uf = m.getUncompressedFrame(i);
    auto* cf = m.getCompressedFrame(i);
    TEST_ASSERT(uf != nullptr, "uncompressed frame should be non-null");
    TEST_ASSERT(cf != nullptr, "compressed frame should be non-null");

    // write to check for overlapping mappings
    memset(uf, (uint8_t)i, uncompressedSize);
    memset(cf, (uint8_t)(i + 100), compressedSize);
  }

  // verify no corruption from overlapping writes
  for(size_t i = 0; i < numFrames; ++i)
  {
    auto* uf = m.getUncompressedFrame(i);
    auto* cf = m.getCompressedFrame(i);
    TEST_ASSERT((uint8_t)uf[0] == (uint8_t)i, "uncompressed data should not be corrupted");
    TEST_ASSERT((uint8_t)cf[0] == (uint8_t)(i + 100), "compressed data should not be corrupted");
  }

  // out-of-range should return null
  TEST_ASSERT(m.getUncompressedFrame(numFrames) == nullptr, "out-of-range should return null");
  TEST_ASSERT(m.getCompressedFrame(numFrames) == nullptr, "out-of-range should return null");
  TEST_PASS("testFrameAccessors");
}

// Test that server constructor fills availableBuffers_ queue with compressed buffers
static void testServerBufferQueue()
{
  auto proc = [](const std::string&) {};
  const size_t compressedSize = 64;
  const size_t numFrames = 3;

  MessengerInit init(false, "Global\\grk_test2_out", "Global\\grk_test2_out_sent",
                     "Global\\grk_test2_out_ready", "Global\\grk_test2_in",
                     "Global\\grk_test2_in_sent", "Global\\grk_test2_in_ready", proc, 0, 0,
                     compressedSize, numFrames);
  Messenger m(init);

  // server should have pre-filled availableBuffers_ with numFrames compressed buffers
  for(size_t i = 0; i < numFrames; ++i)
  {
    BufferSrc src;
    TEST_ASSERT(m.availableBuffers_.pop(src), "should get buffer from queue");
    TEST_ASSERT(src.frameId_ == i, "frameId should match index");
    TEST_ASSERT(src.framePtr_ != nullptr, "framePtr should be non-null");
  }

  // queue should now be empty
  BufferSrc src;
  TEST_ASSERT(!m.availableBuffers_.pop(src), "queue should be empty after draining");
  TEST_PASS("testServerBufferQueue");
}

// Test client initClient fills queue with uncompressed buffers
static void testClientInitBufferQueue()
{
  auto proc = [](const std::string&) {};
  const size_t uncompressedSize = 128;
  const size_t compressedSize = 64;
  const size_t numFrames = 2;

  MessengerInit init(true, "Global\\grk_test3_out", "Global\\grk_test3_out_sent",
                     "Global\\grk_test3_out_ready", "Global\\grk_test3_in",
                     "Global\\grk_test3_in_sent", "Global\\grk_test3_in_ready", proc, 0, 0, 0, 0);
  Messenger m(init);

  TEST_ASSERT(!m.initialized_, "client should not be initialized before initClient");
  m.initClient(uncompressedSize, compressedSize, numFrames);
  TEST_ASSERT(m.initialized_, "client should be initialized after initClient");

  // client queue should have uncompressed buffers
  for(size_t i = 0; i < numFrames; ++i)
  {
    BufferSrc src;
    TEST_ASSERT(m.availableBuffers_.pop(src), "should get buffer");
    TEST_ASSERT(src.frameId_ == i, "frameId should match");
  }
  TEST_PASS("testClientInitBufferQueue");
}

/*************************** Test Runner *******************************/
namespace grk
{

int GrkMessengerTest::main(int argc, char** argv)
{
  (void)argc;
  (void)argv;

  setMessengerLogger(new MessengerLogger("[Test] "));

  fprintf(stdout, "Running Messenger tests...\n");

  // Msg parser tests
  fprintf(stdout, "\n[Msg Parser]\n");
  testMsgBasicParsing();
  testMsgSingleToken();
  testMsgEmptyString();
  testMsgProtocolMessage();

  // BlockingQueue tests
  fprintf(stdout, "\n[BlockingQueue]\n");
  testQueuePushPop();
  testQueueMaxSize();
  testQueueDeactivate();
  testQueueReactivate();
  testQueueWaitAndPop();
  testQueueWaitAndPush();
  testQueueDeactivateUnblocksWaiters();

  // BufferSrc tests
  fprintf(stdout, "\n[BufferSrc]\n");
  testBufferSrcDefault();
  testBufferSrcFromFile();
  testBufferSrcFromMemory();

  // ScheduledFrames tests
  fprintf(stdout, "\n[ScheduledFrames]\n");
  testScheduledFramesStoreRetrieve();
  testScheduledFramesNonExistent();
  testScheduledFramesDuplicateStore();

  // MessengerInit tests
  fprintf(stdout, "\n[MessengerInit]\n");
  testMessengerInitClient();
  testMessengerInitServer();

  // Messenger send tests
  fprintf(stdout, "\n[Messenger::send]\n");
  testMessengerSendVariadic();
  testMessengerSendNoArgs();

  // Utility tests
  fprintf(stdout, "\n[Utility]\n");
  testUncompressedFrameSize();

  // Full protocol loopback test
  fprintf(stdout, "\n[Shared Memory / Buffers]\n");
  testSharedMemoryAllocation();
  testFrameAccessors();
  testServerBufferQueue();
  testClientInitBufferQueue();

  // Summary
  fprintf(stdout, "\n========================================\n");
  fprintf(stdout, "Results: %d passed, %d failed\n", tests_passed, tests_failed);
  fprintf(stdout, "========================================\n");

  return tests_failed > 0 ? 1 : 0;
}

} // namespace grk
