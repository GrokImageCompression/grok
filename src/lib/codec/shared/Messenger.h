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
 * Shared-memory IPC header used by both Grok and external projects (e.g. DCP-o-matic).
 *
 * Porting notes for projects that copy this file:
 *
 *   1. inline globals (C++17) — sLogger, setMessengerLogger(), getMessengerLogger()
 *      are declared with `inline`. For C++11/14, change these to `extern` declarations
 *      and provide definitions in a single .cc file.
 *
 *   2. MessengerInit constructor — the first parameter is `bool isClient`. Client-only
 *      callers should pass `true`.
 *
 *   3. License — this file is AGPL-3.0. Downstream copies may need to adjust the
 *      license header to match the host project.
 */

#pragma once

#include <iostream>
#include <string>
#include <cstring>
#include <atomic>
#include <functional>
#include <sstream>
#include <future>
#include <map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <cassert>
#include <cstdarg>
#include <cinttypes>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <tlhelp32.h>
#pragma warning(disable : 4100)
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <signal.h>
#endif

namespace grk_plugin
{

/*************************** Shared Memory API *******************************/
/*
Compress workflow:
  1. client receives setup info
     GRK_MSGR_BATCH_COMPRESS_INIT, width, stride, height, samplesPerPixel, depth,
     compressed frame size, number of frames
     client creates blocking queue for uncompressed buffers and fills them with pointers
  2. client sends uncompressed
     GRK_MSGR_BATCH_SUBMIT_UNCOMPRESSED, client_frame_id, uncompressed_frame_id
  3. client receives release of uncompressed
     GRK_MSGR_BATCH_PROCESSED_UNCOMPRESSED, uncompressed_frame_id
  4. client receives compressed
     GRK_MSGR_BATCH_SUBMIT_COMPRESSED, client_frame_id, compressed_frame_id,
     compressed_frame_length
  5. client sends release of compressed
     GRK_MSGR_BATCH_PROCESSSED_COMPRESSED, compressed_frame_id
  6. client sends flush at end of batch
     GRK_MSGR_BATCH_FLUSH, enqueued_frame_count

Decompress workflow:
  1. client receives setup info
     GRK_MSGR_BATCH_DECOMPRESS_INIT, width, stride, height, samplesPerPixel, depth,
     compressed frame size, number of frames
     client creates shared buffers, drains uncompressed queue, and fills with
     compressed buffer pointers
  2. client sends compressed
     GRK_MSGR_BATCH_SUBMIT_COMPRESSED, client_frame_id, compressed_frame_id,
     compressed_frame_length
  3. client receives release of compressed
     GRK_MSGR_BATCH_PROCESSSED_COMPRESSED, compressed_frame_id
  4. client receives decompressed
     GRK_MSGR_BATCH_PROCESSED_UNCOMPRESSED, uncompressed_frame_id
  5. client sends flush at end of batch
     GRK_MSGR_BATCH_FLUSH, enqueued_frame_count

*/

/*************************** Synchronization Names *******************************/
// SHM segment and semaphore names are platform-conditional:
//
// macOS: POSIX shm_open/sem_open require names to start with '/' and contain
//        no other '/' characters. Using the "Global\" prefix fails on macOS.
//
// Linux/Windows: dcpomatic and grokPro use the "Global\" prefix unconditionally
//        (originally a Windows convention for cross-session shared memory).
//        On Linux, shm_open accepts arbitrary names (stored in /dev/shm/).
//        We must match this convention for dcpomatic compatibility.
//
// TODO: Coordinate with dcpomatic to adopt a unified naming scheme.
#ifdef __APPLE__
static std::string grokToClientMessageBuf = "/grok_to_client_message";
static std::string grokSentSynch = "/grok_sent";
static std::string clientReceiveReadySynch = "/client_receive_ready";

static std::string clientToGrokMessageBuf = "/client_to_grok_message";
static std::string clientSentSynch = "/client_sent";
static std::string grokReceiveReadySynch = "/grok_receive_ready";

static std::string grokUncompressedBuf = "/grok_uncompressed_buf";
static std::string grokCompressedBuf = "/grok_compressed_buf";
#else
static std::string grokToClientMessageBuf = "Global\\grok_to_client_message";
static std::string grokSentSynch = "Global\\grok_sent";
static std::string clientReceiveReadySynch = "Global\\client_receive_ready";

static std::string clientToGrokMessageBuf = "Global\\client_to_grok_message";
static std::string clientSentSynch = "Global\\client_sent";
static std::string grokReceiveReadySynch = "Global\\grok_receive_ready";

static std::string grokUncompressedBuf = "Global\\grok_uncompressed_buf";
static std::string grokCompressedBuf = "Global\\grok_compressed_buf";
#endif

/*************************** Message IDs *******************************/
static const std::string GRK_MSGR_BATCH_IMAGE = "GRK_MSGR_BATCH_IMAGE";
static const std::string GRK_MSGR_BATCH_COMPRESS_INIT = "GRK_MSGR_BATCH_COMPRESS_INIT";
static const std::string GRK_MSGR_BATCH_DECOMPRESS_INIT = "GRK_MSGR_BATCH_DECOMPRESS_INIT";
static const std::string GRK_MSGR_BATCH_SUBMIT_UNCOMPRESSED = "GRK_MSGR_BATCH_SUBMIT_UNCOMPRESSED";
static const std::string GRK_MSGR_BATCH_PROCESSED_UNCOMPRESSED =
    "GRK_MSGR_BATCH_PROCESSED_UNCOMPRESSED";
static const std::string GRK_MSGR_BATCH_SUBMIT_COMPRESSED = "GRK_MSGR_BATCH_SUBMIT_COMPRESSED";
static const std::string GRK_MSGR_BATCH_PROCESSSED_COMPRESSED =
    "GRK_MSGR_BATCH_PROCESSSED_COMPRESSED";
static const std::string GRK_MSGR_BATCH_SHUTDOWN = "GRK_MSGR_BATCH_SHUTDOWN";
static const std::string GRK_MSGR_BATCH_FLUSH = "GRK_MSGR_BATCH_FLUSH";
static const size_t messageBufferLen = 256;

/*************************** Logging *******************************/
struct IMessengerLogger
{
  virtual ~IMessengerLogger(void) = default;
  virtual void info(const char* fmt, ...) = 0;
  virtual void warn(const char* fmt, ...) = 0;
  virtual void error(const char* fmt, ...) = 0;

protected:
  template<typename... Args>
  std::string log_message(char const* const format, Args&... args) noexcept
  {
    constexpr size_t message_size = 512;
    char message[message_size];
    std::snprintf(message, message_size, format, args...);
    return std::string(message);
  }
};
struct MessengerLogger : public IMessengerLogger
{
  explicit MessengerLogger(const std::string& preamble) : preamble_(preamble) {}
  virtual ~MessengerLogger() = default;
  virtual void info(const char* fmt, ...) override
  {
    va_list args;
    std::string new_fmt = preamble_ + fmt + "\n";
    va_start(args, fmt);
    vfprintf(stdout, new_fmt.c_str(), args);
    va_end(args);
  }
  virtual void warn(const char* fmt, ...) override
  {
    va_list args;
    std::string new_fmt = preamble_ + fmt + "\n";
    va_start(args, fmt);
    vfprintf(stdout, new_fmt.c_str(), args);
    va_end(args);
  }
  virtual void error(const char* fmt, ...) override
  {
    va_list args;
    std::string new_fmt = preamble_ + fmt + "\n";
    va_start(args, fmt);
    vfprintf(stderr, new_fmt.c_str(), args);
    va_end(args);
  }

protected:
  std::string preamble_;
};

inline IMessengerLogger* sLogger = nullptr;
inline void setMessengerLogger(IMessengerLogger* logger)
{
  delete sLogger;
  sLogger = logger;
}
inline IMessengerLogger* getMessengerLogger(void)
{
  return sLogger;
}

/*************************** Messenger Initialization *******************************/
struct MessengerInit
{
  // server constructor (grok side): knows frame sizes at init time
  MessengerInit(bool isClient, const std::string& outBuf, const std::string& outSent,
                const std::string& outReceiveReady, const std::string& inBuf,
                const std::string& inSent, const std::string& inReceiveReady,
                std::function<void(std::string)> processor, size_t numProcessingThreads,
                size_t uncompressedFrameSize, size_t compressedFrameSize, size_t numFrames)
      : isClient_(isClient), outboundMessageBuf(outBuf), outboundSentSynch(outSent),
        outboundReceiveReadySynch(outReceiveReady), inboundMessageBuf(inBuf),
        inboundSentSynch(inSent), inboundReceiveReadySynch(inReceiveReady), processor_(processor),
        numProcessingThreads_(numProcessingThreads), uncompressedFrameSize_(uncompressedFrameSize),
        compressedFrameSize_(compressedFrameSize), numFrames_(numFrames)
  {
    if(firstLaunch(isClient_))
      unlink();
  }
  // client constructor: frame sizes not yet known (will be received via protocol)
  MessengerInit(bool isClient, const std::string& outBuf, const std::string& outSent,
                const std::string& outReceiveReady, const std::string& inBuf,
                const std::string& inSent, const std::string& inReceiveReady,
                std::function<void(std::string)> processor, size_t numProcessingThreads)
      : MessengerInit(isClient, outBuf, outSent, outReceiveReady, inBuf, inSent, inReceiveReady,
                      processor, numProcessingThreads, 0, 0, 0)
  {}
  void unlink(void)
  {
#ifndef _WIN32
    shm_unlink(grokToClientMessageBuf.c_str());
    shm_unlink(clientToGrokMessageBuf.c_str());
#endif
  }
  static bool firstLaunch(bool isClient)
  {
    bool debugGrok = false;
    return debugGrok != isClient;
  }

  bool isClient_;
  std::string outboundMessageBuf;
  std::string outboundSentSynch;
  std::string outboundReceiveReadySynch;

  std::string inboundMessageBuf;
  std::string inboundSentSynch;
  std::string inboundReceiveReadySynch;

  std::function<void(std::string)> processor_;
  size_t numProcessingThreads_;

  size_t uncompressedFrameSize_;
  size_t compressedFrameSize_;
  size_t numFrames_;
};

/*************************** Synchronization *******************************/
enum SynchDirection
{
  SYNCH_SENT,
  SYNCH_RECEIVE_READY
};

#ifdef _WIN32
typedef HANDLE grk_handle;
struct Synch
{
  Synch(bool isClient, std::string sentSemName, const std::string& receiveReadySemName)
      : isClient_(isClient), sentSemName_(sentSemName), receiveReadySemName_(receiveReadySemName)
  {
    open();
  }
  ~Synch()
  {
    close();
  }
  void post(SynchDirection dir)
  {
    auto sem = dir == SYNCH_SENT ? sentSem_ : receiveReadySem_;
    if(!ReleaseSemaphore(sem, 1, NULL))
      getMessengerLogger()->error("Error posting to semaphore: %d", GetLastError());
  }

  void wait(SynchDirection dir)
  {
    auto sem = dir == SYNCH_SENT ? sentSem_ : receiveReadySem_;
    DWORD result = WaitForSingleObject(sem, INFINITE);
    if(result != WAIT_OBJECT_0)
      getMessengerLogger()->error("Error waiting on semaphore: %d", GetLastError());
  }
  void open(void)
  {
    sentSem_ = CreateSemaphore(NULL, 0, 1, sentSemName_.c_str());
    if(sentSem_ == NULL)
      getMessengerLogger()->error("Error creating semaphore: %d", GetLastError());
    receiveReadySem_ = CreateSemaphore(NULL, 1, 1, receiveReadySemName_.c_str());
    if(receiveReadySem_ == NULL)
      getMessengerLogger()->error("Error creating semaphore: %d", GetLastError());
  }
  void close(void)
  {
    BOOL rc = CloseHandle(sentSem_);
    if(!rc)
      getMessengerLogger()->error("Error closing semaphore: %d", GetLastError());
    rc = CloseHandle(receiveReadySem_);
    if(!rc)
      getMessengerLogger()->error("Error closing semaphore: %d", GetLastError());
  }
  grk_handle sentSem_;
  grk_handle receiveReadySem_;

private:
  bool isClient_;
  std::string sentSemName_;
  std::string receiveReadySemName_;
};
struct SharedMemoryManager
{
  // Windows: CreateFileMapping transparently opens or creates the mapping,
  // so isCreator is unused — stale segments from a previous crash are handled
  // automatically, unlike POSIX where O_EXCL + shm_unlink recovery is needed.
  static bool initShm(const std::string& name, size_t len, grk_handle* hMapFile, char** buffer,
                      [[maybe_unused]] bool isCreator)
  {
    *hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE, // use paging file
                                  NULL, // default security
                                  PAGE_READWRITE, // read/write access
                                  0, // max object size (high-order DWORD)
                                  len, // max object size (low-order DWORD)
                                  name.c_str()); // name of mapping object

    if(*hMapFile == NULL)
    {
      getMessengerLogger()->error("Error creating file mapping: %d", GetLastError());
      return false;
    }

    *buffer = (char*)MapViewOfFile(*hMapFile, // handle to map object
                                   FILE_MAP_ALL_ACCESS, // read/write permission
                                   0, 0, len);

    if(*buffer == NULL)
    {
      getMessengerLogger()->error("Could not map view of file: %d", GetLastError());
      CloseHandle(*hMapFile);
      return false;
    }

    return *buffer != nullptr;
  }

  static bool deinitShm(const std::string& name, size_t len, grk_handle& hMapFile, char** buffer)
  {
    bool rc = UnmapViewOfFile(*buffer);
    *buffer = nullptr;
    if(!rc)
    {
      getMessengerLogger()->error("Could not unmap view of file: %d", GetLastError());
      return false;
    }

    rc = CloseHandle(hMapFile);
    hMapFile = 0;
    if(!rc)
    {
      getMessengerLogger()->error("Could not close handle: %d", GetLastError());
      return false;
    }

    return true;
  }
};

#else
typedef int grk_handle;
struct Synch
{
  Synch(bool isClient, const std::string& sentSemName, const std::string& receiveReadySemName)
      : isClient_(isClient), sentSemName_(sentSemName), receiveReadySemName_(receiveReadySemName)
  {
    // unlink semaphores in case of previous crash
    if(MessengerInit::firstLaunch(isClient_))
      unlink();
    open();
  }
  ~Synch()
  {
    close();
    if(MessengerInit::firstLaunch(isClient_))
      unlink();
  }
  void post(SynchDirection dir)
  {
    auto sem = (dir == SYNCH_SENT ? sentSem_ : receiveReadySem_);
    int rc = sem_post(sem);
    if(rc)
      getMessengerLogger()->error("Error posting to semaphore: %s", strerror(errno));
  }
  void wait(SynchDirection dir)
  {
    auto sem = dir == SYNCH_SENT ? sentSem_ : receiveReadySem_;
    int rc = sem_wait(sem);
    if(rc)
      getMessengerLogger()->error("Error waiting for semaphore: %s", strerror(errno));
  }
  void open(void)
  {
    sentSem_ = sem_open(sentSemName_.c_str(), O_CREAT, 0666, 0);
    if(sentSem_ == SEM_FAILED)
      getMessengerLogger()->error("Error opening semaphore %s: %s", sentSemName_.c_str(),
                                  strerror(errno));
    receiveReadySem_ = sem_open(receiveReadySemName_.c_str(), O_CREAT, 0666, 1);
    if(receiveReadySem_ == SEM_FAILED)
      getMessengerLogger()->error("Error opening semaphore %s: %s", receiveReadySemName_.c_str(),
                                  strerror(errno));
  }
  void close(void)
  {
    int rc = sem_close(sentSem_);
    if(rc)
      getMessengerLogger()->error("Error closing semaphore %s: %s", sentSemName_.c_str(),
                                  strerror(errno));
    rc = sem_close(receiveReadySem_);
    if(rc)
      getMessengerLogger()->error("Error closing semaphore %s: %s", receiveReadySemName_.c_str(),
                                  strerror(errno));
  }
  void unlink(void)
  {
    int rc = sem_unlink(sentSemName_.c_str());
    if(rc == -1 && errno != ENOENT)
      getMessengerLogger()->error("Error unlinking semaphore %s: %s", sentSemName_.c_str(),
                                  strerror(errno));
    rc = sem_unlink(receiveReadySemName_.c_str());
    if(rc == -1 && errno != ENOENT)
      getMessengerLogger()->error("Error unlinking semaphore %s: %s", receiveReadySemName_.c_str(),
                                  strerror(errno));
  }
  sem_t* sentSem_;
  sem_t* receiveReadySem_;

private:
  bool isClient_;
  std::string sentSemName_;
  std::string receiveReadySemName_;
};
struct SharedMemoryManager
{
  static bool initShm(const std::string& name, size_t len, grk_handle* shm_fd, char** buffer,
                      bool isCreator)
  {
    if(*shm_fd)
      return true;
    if(len == 0)
    {
      getMessengerLogger()->error("Shared memory size is 0 for %s", name.c_str());
      errno = EINVAL;
      return false;
    }
    // Guard: off_t is signed; ensure bytes fits.
    if(len > static_cast<uint64_t>(std::numeric_limits<off_t>::max()))
    {
      getMessengerLogger()->error("Shared memory size too large (%" PRIu64 ") for %s", len,
                                  name.c_str());
      errno = EOVERFLOW;
      return false;
    }

    if(isCreator)
    {
      // Platform-conditional shm_open flags:
      //
      // dcpomatic (our primary client) calls startThreads() BEFORE launching
      // grk_compress, so the client's outbound thread may create SHM segments
      // before the server even starts. Both sides use O_CREAT | O_RDWR (without
      // O_EXCL), meaning whoever arrives first creates the segment and the other
      // simply opens it — this is the grokPro-era convention.
      //
      // macOS: We must use O_EXCL because macOS shm_open has issues with stale
      // segments from crashed processes that weren't properly unlinked. The
      // unlink-and-retry handles EEXIST gracefully. This works on macOS because
      // dcpomatic doesn't currently target macOS with grok's SHM path.
      //
      // Linux: O_EXCL would break dcpomatic compatibility — if the client
      // already created the segment, O_EXCL fails, the server unlinks it and
      // creates a new one, leaving client and server mapped to different memory.
      //
      // TODO: Unify this once dcpomatic is updated to not pre-create SHM
      // segments before launching the server.
#ifdef __APPLE__
      *shm_fd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
      if(*shm_fd < 0 && errno == EEXIST)
      {
        shm_unlink(name.c_str());
        *shm_fd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
      }
#else
      *shm_fd = shm_open(name.c_str(), O_CREAT | O_RDWR, 0666);
#endif
      if(*shm_fd < 0)
      {
        getMessengerLogger()->error("Error creating shared memory %s: %s", name.c_str(),
                                    strerror(errno));
        *shm_fd = 0;
        return false;
      }
      int rc = ftruncate(*shm_fd, (off_t)len);
      if(rc)
      {
        getMessengerLogger()->error("Error truncating shared memory to %" PRIu64 " bytes: %s",
                                    (uint64_t)len, strerror(errno));
        close(*shm_fd);
        shm_unlink(name.c_str());
        *shm_fd = 0;
        return false;
      }
    }
    else
    {
      // Non-creator: open existing segment, retry briefly if not yet created.
      // Message buffers need longer waits (server subprocess may still be starting),
      // data buffers are typically ready immediately (server creates before sending init).
      const int maxRetries = 100;
      const int retryDelayMs = 100;
      for(int attempt = 0; attempt < maxRetries; ++attempt)
      {
        *shm_fd = shm_open(name.c_str(), O_RDWR, 0666);
        if(*shm_fd >= 0)
          break;
        if(errno != ENOENT || attempt == maxRetries - 1)
        {
          getMessengerLogger()->error("Error opening shared memory %s: %s", name.c_str(),
                                      strerror(errno));
          return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
      }
    }

    *buffer = static_cast<char*>(mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, *shm_fd, 0));
    if(*buffer == MAP_FAILED)
    {
      *buffer = nullptr;
      getMessengerLogger()->error("Error mapping shared memory %s: %s", name.c_str(),
                                  strerror(errno));
      close(*shm_fd);
      if(isCreator)
        shm_unlink(name.c_str());
      *shm_fd = 0;
    }

    return *buffer != nullptr;
  }
  static bool deinitShm(const std::string& name, size_t len, grk_handle& shm_fd, char** buffer)
  {
    if(!*buffer || !shm_fd)
      return true;

    int rc = munmap(*buffer, len);
    *buffer = nullptr;
    if(rc)
      getMessengerLogger()->error("Error unmapping shared memory %s: %s", name.c_str(),
                                  strerror(errno));
    rc = close(shm_fd);
    shm_fd = 0;
    if(rc)
      getMessengerLogger()->error("Error closing shared memory %s: %s", name.c_str(),
                                  strerror(errno));
    rc = shm_unlink(name.c_str());
    // 2 == No such file or directory
    if(rc && errno != 2)
      fprintf(stderr, "Error unlinking shared memory %s : %s\n", name.c_str(), strerror(errno));

    return true;
  }
};
#endif

/*************************** Thread-safe Blocking Queue *******************************/
template<typename Data>
class MessengerBlockingQueue
{
public:
  explicit MessengerBlockingQueue(size_t max) : active_(true), max_size_(max) {}
  MessengerBlockingQueue() : MessengerBlockingQueue(UINT_MAX) {}
  size_t size() const
  {
    return queue_.size();
  }
  // deactivate and clear queue
  void deactivate()
  {
    {
      std::lock_guard<std::mutex> lk(mutex_);
      active_ = false;
      while(!queue_.empty())
        queue_.pop();
    }

    // release all waiting threads
    can_pop_.notify_all();
    can_push_.notify_all();
  }
  void activate()
  {
    std::lock_guard<std::mutex> lk(mutex_);
    active_ = true;
  }
  bool push(Data const& value)
  {
    bool rc;
    {
      std::unique_lock<std::mutex> lk(mutex_);
      rc = push_(value);
    }
    if(rc)
      can_pop_.notify_one();

    return rc;
  }
  bool waitAndPush(Data& value)
  {
    bool rc;
    {
      std::unique_lock<std::mutex> lk(mutex_);
      if(!active_)
        return false;
      // in case of spurious wakeup, loop until predicate in lambda
      // is satisfied.
      can_push_.wait(lk, [this] { return queue_.size() < max_size_ || !active_; });
      rc = push_(value);
    }
    if(rc)
      can_pop_.notify_one();

    return rc;
  }
  bool pop(Data& value)
  {
    bool rc;
    {
      std::unique_lock<std::mutex> lk(mutex_);
      rc = pop_(value);
    }
    if(rc)
      can_push_.notify_one();

    return rc;
  }
  bool waitAndPop(Data& value)
  {
    bool rc;
    {
      std::unique_lock<std::mutex> lk(mutex_);
      if(!active_)
        return false;
      // in case of spurious wakeup, loop until predicate in lambda
      // is satisfied.
      can_pop_.wait(lk, [this] { return !queue_.empty() || !active_; });
      rc = pop_(value);
    }
    if(rc)
      can_push_.notify_one();

    return rc;
  }

private:
  bool push_(Data const& value)
  {
    if(queue_.size() == max_size_ || !active_)
      return false;
    queue_.push(value);

    return true;
  }
  bool pop_(Data& value)
  {
    if(queue_.empty() || !active_)
      return false;
    value = queue_.front();
    queue_.pop();

    return true;
  }
  std::queue<Data> queue_;
  mutable std::mutex mutex_;
  std::condition_variable can_pop_;
  std::condition_variable can_push_;
  bool active_;
  size_t max_size_;
};

/*************************** Buffer Source *******************************/
struct BufferSrc
{
  BufferSrc(void) : BufferSrc("") {}
  explicit BufferSrc(const std::string& file)
      : file_(file), clientFrameId_(0), frameId_(0), framePtr_(nullptr)
  {}
  BufferSrc(size_t clientFrameId, size_t frameId, uint8_t* framePtr)
      : file_(""), clientFrameId_(clientFrameId), frameId_(frameId), framePtr_(framePtr)
  {}
  bool fromDisk(void)
  {
    return !file_.empty() && framePtr_ == nullptr;
  }
  size_t index() const
  {
    return clientFrameId_;
  }
  std::string file_;
  size_t clientFrameId_;
  size_t frameId_;
  uint8_t* framePtr_;
};

/*************************** Messenger *******************************/
struct Messenger;
static void outboundThread(Messenger* messenger, const std::string& sendBuf, Synch* synch);
static void inboundThread(Messenger* messenger, const std::string& receiveBuf, Synch* synch);
static void processorThread(Messenger* messenger, std::function<void(std::string)> processor);

struct Messenger
{
  explicit Messenger(MessengerInit init) : init_(init)
  {
    if(!isClient())
    {
      startThreads();
      initBuffers();

      // server fills queue with pending compressed buffers
      char* ptr = compressed_buffer_;
      for(size_t i = 0; i < init_.numFrames_; ++i)
      {
        availableBuffers_.push(BufferSrc(0, i, (uint8_t*)ptr));
        ptr += init_.compressedFrameSize_;
      }

      initialized_ = true;
    }
  }
  virtual ~Messenger(void)
  {
    running = false;
    sendQueue.deactivate();
    receiveQueue.deactivate();

    if(outboundSynch_)
    {
      outboundSynch_->post(SYNCH_RECEIVE_READY);
      outbound.join();
    }

    if(inboundSynch_)
    {
      inboundSynch_->post(SYNCH_SENT);
      inbound.join();
    }

    for(auto& p : processors_)
      p.join();

    delete outboundSynch_;
    delete inboundSynch_;

    deinitShm();
  }
  void startThreads(void)
  {
    outboundSynch_ =
        new Synch(init_.isClient_, init_.outboundSentSynch, init_.outboundReceiveReadySynch);
    outbound = std::thread(outboundThread, this, init_.outboundMessageBuf, outboundSynch_);

    inboundSynch_ =
        new Synch(init_.isClient_, init_.inboundSentSynch, init_.inboundReceiveReadySynch);
    inbound = std::thread(inboundThread, this, init_.inboundMessageBuf, inboundSynch_);

    for(size_t i = 0; i < init_.numProcessingThreads_; ++i)
      processors_.push_back(std::thread(processorThread, this, init_.processor_));
  }
  size_t serialize(const std::string& dir, size_t clientFrameId, uint8_t* compressedPtr,
                   size_t compressedLength)
  {
    char fname[512];
    if(!compressedPtr || !compressedLength)
      return 0;
    sprintf(fname, "%s/test_%d.j2k", dir.c_str(), (int)clientFrameId);
    auto fp = fopen(fname, "wb");
    if(!fp)
      return 0;
    size_t written = fwrite(compressedPtr, 1, compressedLength, fp);
    if(written != compressedLength)
    {
      fclose(fp);
      return 0;
    }
    fflush(fp);
    fclose(fp);

    return written;
  }
  bool initBuffers(void)
  {
    char temp[512];
    sprintf(temp,
            "Initializing shared memory buffers: num frames %zu, "
            "					uncompressed frame size %zu, "
            "						compressed frame size %zu ",
            init_.numFrames_, init_.uncompressedFrameSize_, init_.compressedFrameSize_);
    getMessengerLogger()->info(temp);
    if(init_.uncompressedFrameSize_)
    {
      bool rc = SharedMemoryManager::initShm(
          grokUncompressedBuf, init_.uncompressedFrameSize_ * init_.numFrames_, &uncompressed_fd_,
          &uncompressed_buffer_, !init_.isClient_);
      if(!rc)
        return false;

      auto msg = std::string("Initialized shared uncompressed memory buffers: ") +
                 (rc ? "success" : "failure");
      getMessengerLogger()->info(msg.c_str());
    }
    if(init_.compressedFrameSize_)
    {
      bool rc = SharedMemoryManager::initShm(
          grokCompressedBuf, init_.compressedFrameSize_ * init_.numFrames_, &compressed_fd_,
          &compressed_buffer_, !init_.isClient_);
      if(!rc)
        return false;

      auto msg = std::string("Initialized shared compressed memory buffers: ") +
                 (rc ? "success" : "failure");
      getMessengerLogger()->info(msg.c_str());
    }

    return true;
  }

  bool deinitShm(void)
  {
    bool rc = SharedMemoryManager::deinitShm(grokUncompressedBuf,
                                             init_.uncompressedFrameSize_ * init_.numFrames_,
                                             uncompressed_fd_, &uncompressed_buffer_);
    rc = rc && SharedMemoryManager::deinitShm(grokCompressedBuf,
                                              init_.compressedFrameSize_ * init_.numFrames_,
                                              compressed_fd_, &compressed_buffer_);

    return rc;
  }
  template<typename... Args>
  void send(const std::string& str, Args... args)
  {
    std::ostringstream oss;
    oss << str;
    int dummy[] = {0, ((void)(oss << ',' << args), 0)...};
    static_cast<void>(dummy);

    sendQueue.push(oss.str());
  }
  bool launch(const std::string& cmd, const std::string& dir)
  {
    std::unique_lock<std::mutex> lk(shutdownMutex_);
    if(async_result_.valid())
      return true;
    if(MessengerInit::firstLaunch(isClient()))
      init_.unlink();
    startThreads();

    // Change the working directory
    if(!dir.empty())
    {
#ifdef _WIN32
      if(_chdir(dir.c_str()) != 0)
      {
#else
      if(chdir(dir.c_str()) != 0)
      {
#endif
        getMessengerLogger()->error("Error: failed to change the working directory");
        return false;
      }
    }
    // Execute the command using std::async and std::system
    cmd_ = cmd;
    async_result_ = std::async(std::launch::async, [this]() { return std::system(cmd_.c_str()); });
    bool success = async_result_.valid();
    if(!success)
      getMessengerLogger()->error("Launch failed");

    return success;
  }
  bool initClient(size_t uncompressedFrameSize, size_t compressedFrameSize, size_t numFrames)
  {
    getMessengerLogger()->info("Initializing shared memory client");
    // client fills queue with pending uncompressed buffers
    init_.uncompressedFrameSize_ = uncompressedFrameSize;
    init_.compressedFrameSize_ = compressedFrameSize;
    init_.numFrames_ = numFrames;
    if(!initBuffers())
      return false;
    auto ptr = uncompressed_buffer_;
    for(size_t i = 0; i < init_.numFrames_; ++i)
    {
      availableBuffers_.push(BufferSrc(0, i, (uint8_t*)ptr));
      ptr += init_.uncompressedFrameSize_;
    }

    std::unique_lock<std::mutex> lk(shutdownMutex_);
    initialized_ = true;
    clientInitializedCondition_.notify_all();
    getMessengerLogger()->info("Initialized shared memory client");
    return true;
  }
  bool waitForClientInit(void)
  {
    if(!isClient() || initialized_)
      return true;

    std::unique_lock<std::mutex> lk(shutdownMutex_);
    if(initialized_)
      return true;
    else if(shutdown_)
      return false;

    while(true)
    {
      if(clientInitializedCondition_.wait_for(lk, std::chrono::seconds(1),
                                              [this] { return initialized_ || shutdown_; }))
      {
        break;
      }
      if(async_result_.valid())
      {
        auto status = async_result_.wait_for(std::chrono::milliseconds(100));
        if(status == std::future_status::ready)
        {
          getMessengerLogger()->error("Server exited unexpectedly during initialization");
          return false;
        }
      }
    }

    return initialized_ && !shutdown_;
  }
  bool isClient(void)
  {
    return init_.isClient_;
  }
  static size_t uncompressedFrameSize(uint32_t w, uint32_t h, uint32_t samplesPerPixel)
  {
    return sizeof(uint16_t) * w * h * samplesPerPixel;
  }
  void reclaimCompressed(size_t frameId)
  {
    availableBuffers_.push(BufferSrc(0, frameId, getCompressedFrame(frameId)));
  }
  void reclaimUncompressed(size_t frameId)
  {
    availableBuffers_.push(BufferSrc(0, frameId, getUncompressedFrame(frameId)));
  }
  uint8_t* getUncompressedFrame(size_t frameId)
  {
    if(frameId >= init_.numFrames_)
      return nullptr;

    return (uint8_t*)(uncompressed_buffer_ + frameId * init_.uncompressedFrameSize_);
  }
  uint8_t* getCompressedFrame(size_t frameId)
  {
    if(frameId >= init_.numFrames_)
      return nullptr;

    return (uint8_t*)(compressed_buffer_ + frameId * init_.compressedFrameSize_);
  }
  std::atomic_bool running{true};
  bool initialized_ = false;
  bool shutdown_ = false;
  MessengerBlockingQueue<std::string> sendQueue;
  MessengerBlockingQueue<std::string> receiveQueue;
  MessengerBlockingQueue<BufferSrc> availableBuffers_;
  MessengerInit init_;
  std::string cmd_;
  std::future<int> async_result_;
  std::mutex shutdownMutex_;
  std::condition_variable shutdownCondition_;

protected:
  std::condition_variable clientInitializedCondition_;

private:
  std::thread outbound;
  Synch* outboundSynch_ = nullptr;

  std::thread inbound;
  Synch* inboundSynch_ = nullptr;

  std::vector<std::thread> processors_;
  char* uncompressed_buffer_ = nullptr;
  char* compressed_buffer_ = nullptr;

  grk_handle uncompressed_fd_ = 0;
  grk_handle compressed_fd_ = 0;
};

/*************************** I/O Threads *******************************/
static void outboundThread(Messenger* messenger, const std::string& sendBuf, Synch* synch)
{
  grk_handle shm_fd = 0;
  char* send_buffer = nullptr;

  if(!SharedMemoryManager::initShm(sendBuf, messageBufferLen, &shm_fd, &send_buffer,
                                   !messenger->isClient()))
    return;
  while(messenger->running)
  {
    synch->wait(SYNCH_RECEIVE_READY);
    if(!messenger->running)
      break;
    std::string message;
    if(!messenger->sendQueue.waitAndPop(message))
      break;
    if(!messenger->running)
      break;
    memcpy(send_buffer, message.c_str(), message.size() + 1);
    synch->post(SYNCH_SENT);
  }
  SharedMemoryManager::deinitShm(sendBuf, messageBufferLen, shm_fd, &send_buffer);
}

static void inboundThread(Messenger* messenger, const std::string& receiveBuf, Synch* synch)
{
  grk_handle shm_fd = 0;
  char* receive_buffer = nullptr;

  if(!SharedMemoryManager::initShm(receiveBuf, messageBufferLen, &shm_fd, &receive_buffer,
                                   !messenger->isClient()))
    return;
  while(messenger->running)
  {
    synch->wait(SYNCH_SENT);
    if(!messenger->running)
      break;
    auto message = std::string(receive_buffer);
    synch->post(SYNCH_RECEIVE_READY);
    messenger->receiveQueue.push(message);
  }
  SharedMemoryManager::deinitShm(receiveBuf, messageBufferLen, shm_fd, &receive_buffer);
}

/*************************** Message Parser *******************************/
struct Msg
{
  explicit Msg(const std::string& msg) : ct_(0)
  {
    std::stringstream ss(msg);
    while(ss.good())
    {
      std::string substr;
      std::getline(ss, substr, ',');
      cs_.push_back(substr);
    }
  }
  std::string next()
  {
    if(ct_ == cs_.size())
    {
      getMessengerLogger()->error("Msg: comma separated list exhausted. returning empty.");
      return "";
    }
    return cs_[ct_++];
  }

  uint32_t nextUint(void)
  {
    return (uint32_t)std::stoi(next());
  }

  std::vector<std::string> cs_;
  size_t ct_;
};

/*************************** Processor Thread *******************************/
static void processorThread(Messenger* messenger, std::function<void(std::string)> processor)
{
  while(messenger->running)
  {
    std::string message;
    if(!messenger->receiveQueue.waitAndPop(message))
      break;
    if(!messenger->running)
      break;
    // pre-process message
    Msg msg(message);
    auto tag = msg.next();
    if(tag == GRK_MSGR_BATCH_COMPRESS_INIT)
    {
      auto width = msg.nextUint();
      auto stride = msg.nextUint();
      (void)stride;
      auto height = msg.nextUint();
      auto samplesPerPixel = msg.nextUint();
      auto depth = msg.nextUint();
      (void)depth;
      messenger->init_.uncompressedFrameSize_ =
          Messenger::uncompressedFrameSize(width, height, samplesPerPixel);
      auto compressedFrameSize = msg.nextUint();
      auto numFrames = msg.nextUint();
      if(!messenger->initClient(messenger->init_.uncompressedFrameSize_, compressedFrameSize,
                                numFrames))
        return;
    }
    else if(tag == GRK_MSGR_BATCH_DECOMPRESS_INIT)
    {
      auto width = msg.nextUint();
      auto stride = msg.nextUint();
      (void)stride;
      auto height = msg.nextUint();
      auto samplesPerPixel = msg.nextUint();
      auto depth = msg.nextUint();
      (void)depth;
      messenger->init_.uncompressedFrameSize_ =
          Messenger::uncompressedFrameSize(width, height, samplesPerPixel);
      auto compressedFrameSize = msg.nextUint();
      auto numFrames = msg.nextUint();
      // for decompress: client manages compressed buffers (input)
      if(!messenger->initClient(messenger->init_.uncompressedFrameSize_, compressedFrameSize,
                                numFrames))
        return;
      // drain uncompressed buffers and fill with compressed buffers
      {
        BufferSrc dummy;
        while(messenger->availableBuffers_.pop(dummy))
          ;
        for(size_t i = 0; i < numFrames; ++i)
          messenger->availableBuffers_.push(BufferSrc(0, i, messenger->getCompressedFrame(i)));
      }
    }
    else if(tag == GRK_MSGR_BATCH_PROCESSED_UNCOMPRESSED)
    {
      messenger->reclaimUncompressed(msg.nextUint());
    }
    else if(tag == GRK_MSGR_BATCH_PROCESSSED_COMPRESSED)
    {
      messenger->reclaimCompressed(msg.nextUint());
    }

    // dispatch to user processor
    processor(message);
  }
}

/*************************** Scheduled Frame Tracking *******************************/
template<typename F>
struct ScheduledFrames
{
  void store(F const& val)
  {
    std::unique_lock<std::mutex> lk(mapMutex_);
    auto it = map_.find(val.index());
    if(it == map_.end())
      map_[val.index()] = val;
  }
  bool retrieve(size_t index, F& out)
  {
    std::unique_lock<std::mutex> lk(mapMutex_);
    auto it = map_.find(index);
    if(it == map_.end())
      return false;

    out = it->second;
    map_.erase(index);

    return true;
  }

private:
  std::mutex mapMutex_;
  std::map<size_t, F> map_;
};

/*************************** Scheduled Messenger *******************************/
template<typename F>
struct ScheduledMessenger : public Messenger
{
  explicit ScheduledMessenger(MessengerInit init)
      : Messenger(init), framesScheduled_(0), framesCompressed_(0)
  {}
  ~ScheduledMessenger(void)
  {
    shutdown();
  }
  bool scheduleCompress(F const& proxy, std::function<void(BufferSrc const&)> converter)
  {
    size_t frameSize = init_.uncompressedFrameSize_;
    assert(frameSize >= init_.uncompressedFrameSize_);
    BufferSrc src;
    if(!availableBuffers_.waitAndPop(src))
      return false;
    converter(src);

    scheduledFrames_.store(proxy);
    framesScheduled_++;

    send(GRK_MSGR_BATCH_SUBMIT_UNCOMPRESSED, proxy.index(), src.frameId_);

    return true;
  }
  void processCompressed(const std::string& message,
                         std::function<void(F, uint8_t*, uint32_t)> processor,
                         bool needsRecompression)
  {
    Msg msg(message);
    msg.next();
    auto clientFrameId = msg.nextUint();
    auto compressedFrameId = msg.nextUint();
    auto compressedFrameLength = msg.nextUint();
    if(!needsRecompression)
    {
      F srcFrame;
      bool success = scheduledFrames_.retrieve(clientFrameId, srcFrame);
      if(!success)
        return;
      processor(srcFrame, getCompressedFrame(compressedFrameId), compressedFrameLength);
    }
    ++framesCompressed_;
    send(GRK_MSGR_BATCH_PROCESSSED_COMPRESSED, compressedFrameId);
    if(shutdown_ && framesCompressed_ == framesScheduled_)
      shutdownCondition_.notify_all();
  }
  void shutdown(void)
  {
    try
    {
      std::unique_lock<std::mutex> lk(shutdownMutex_);
      if(!async_result_.valid())
        return;
      shutdown_ = true;
      if(framesScheduled_)
      {
        uint32_t scheduled = framesScheduled_;
        send(GRK_MSGR_BATCH_FLUSH, scheduled);
        shutdownCondition_.wait(lk, [this] { return framesScheduled_ == framesCompressed_; });
      }
      availableBuffers_.deactivate();
      send(GRK_MSGR_BATCH_SHUTDOWN);
      int result = async_result_.get();
      if(result != 0)
        getMessengerLogger()->error("Accelerator failed with return code: %d\n", result);
    }
    catch(std::exception& ex)
    {
      getMessengerLogger()->error("%s", ex.what());
    }
  }
  bool retrieve(size_t index, F& out)
  {
    return scheduledFrames_.retrieve(index, out);
  }
  void store(F const& val)
  {
    scheduledFrames_.store(val);
  }

private:
  ScheduledFrames<F> scheduledFrames_;
  std::atomic<uint32_t> framesScheduled_;
  std::atomic<uint32_t> framesCompressed_;
};

} // namespace grk_plugin
