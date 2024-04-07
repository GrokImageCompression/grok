/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
 *
 */
#include "grk_apps_config.h"

#ifdef GROK_HAVE_URING

#include "FileUringIO.h"
#include "common.h"
#include <strings.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/times.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <chrono>
#include "MemManager.h"

const static bool debugUring = false;

FileUringIO::FileUringIO()
	: fd_(0), ownsDescriptor(false), requestsSubmitted(0), requestsCompleted(0),
	  reclaim_callback_(nullptr), reclaim_user_data_(nullptr)
{
   memset(&ring, 0, sizeof(ring));
}

void FileUringIO::registerGrkReclaimCallback(grk_io_callback reclaim_callback, void* user_data)
{
   reclaim_callback_ = reclaim_callback;
   reclaim_user_data_ = user_data;
}
bool FileUringIO::attach(std::string fileName, std::string mode, int fd)
{
   fileName_ = fileName;
   bool useStdio = grk::useStdio(fileName_);
   bool doRead = mode[0] == -'r';
   if(useStdio)
	  fd_ = doRead ? STDIN_FILENO : STDOUT_FILENO;
   else
	  fd_ = fd;
   ownsDescriptor = false;

   return (doRead ? true : initQueue());
}

bool FileUringIO::open(const std::string& fileName, const std::string& mode)
{
   fileName_ = fileName;
   bool useStdio = grk::useStdio(fileName_);
   bool doRead = mode[0] == -'r';
   if(useStdio)
   {
	  fd_ = doRead ? STDIN_FILENO : STDOUT_FILENO;
	  ownsDescriptor = false;
	  return true;
   }
   auto name = fileName_.c_str();
   fd_ = ::open(name, getMode(mode.c_str()), 0666);
   if(fd_ < 0)
   {
	  if(errno > 0 && strerror(errno) != nullptr)
		 spdlog::error("{}: {}", name, strerror(errno));
	  else
		 spdlog::error("{}: Cannot open", name);
	  return false;
   }
   ownsDescriptor = true;

   return (doRead ? true : initQueue());
}

bool FileUringIO::initQueue(void)
{
   int ret = io_uring_queue_init(QD, &ring, 0);
   if(ret < 0)
   {
	  spdlog::error("queue_init: {}\n", strerror(-ret));
	  close();
	  return false;
   }

   return true;
}

int FileUringIO::getMode(const char* mode)
{
   int m = -1;

   switch(mode[0])
   {
	  case 'r':
		 m = O_RDONLY;
		 if(mode[1] == '+')
			m = O_RDWR;
		 break;
	  case 'w':
	  case 'a':
		 m = O_RDWR | O_CREAT;
		 if(mode[0] == 'w')
			m |= O_TRUNC;
		 break;
	  default:
		 spdlog::error("Bad mode {}", mode);
		 break;
   }
   return m;
}

void FileUringIO::enqueue(io_uring* ring, io_data* data, bool readop, int fd)
{
   auto sqe = io_uring_get_sqe(ring);
   assert(sqe);

   // grk::ChronoTimer timer("uring: time to enque");
   // timer.start();
   assert(data->buf.data_ == data->iov.iov_base);
   if(readop)
	  io_uring_prep_readv(sqe, fd, &data->iov, 1, data->buf.offset_);
   else
	  io_uring_prep_writev(sqe, fd, &data->iov, 1, data->buf.offset_);
   io_uring_sqe_set_data(sqe, data);
   [[maybe_unused]] int ret = io_uring_submit(ring);
   // if (debugUring)
   //	spdlog::info("Enqueued {}, length {}, offset {}", fmt::ptr(data->buf.data),
   // data->buf.dataLen, data->buf.offset); timer.finish();
   assert(ret == 1);
   requestsSubmitted++;

   while(true)
   {
	  bool success;
	  auto data = retrieveCompletion(true, success);
	  if(!success || !data)
		 break;
	  if(data->buf.pooled_)
	  {
		 if(reclaim_callback_)
		 {
			reclaim_callback_(0, data->buf, reclaim_user_data_);
		 }
		 else
		 {
			grk_bin::grk_aligned_free((uint8_t*)data->iov.iov_base);
		 }
	  }
	  else
	  {
		 grk_bin::grk_aligned_free((uint8_t*)data->iov.iov_base);
	  }
	  delete data;
   }
   // if (debugUring && canReclaim && *num_reclaimed)
   //	spdlog::info("Reclaimed : {}", *num_reclaimed);
}

io_data* FileUringIO::retrieveCompletion(bool peek, bool& success)
{
   io_uring_cqe* cqe;
   int ret;

   if(peek)
	  ret = io_uring_peek_cqe(&ring, &cqe);
   else
	  ret = io_uring_wait_cqe(&ring, &cqe);
   success = true;

   if(ret < 0)
   {
	  if(!peek)
	  {
		 spdlog::error("io_uring_wait_cqe returned an error.");
		 success = false;
	  }
	  return nullptr;
   }
   if(cqe->res < 0)
   {
	  spdlog::error("The system call invoked asynchronously has failed with the following error:"
					" \n{}",
					strerror(cqe->res));
	  success = false;
	  return nullptr;
   }

   auto data = (io_data*)io_uring_cqe_get_data(cqe);
   if(data)
   {
	  io_uring_cqe_seen(&ring, cqe);
	  requestsCompleted++;
   }

   return data;
}

bool FileUringIO::close(void)
{
   if(!fd_)
	  return true;
   if(ring.ring_fd)
   {
	  // grk::ChronoTimer timer("uring: time to close");
	  // timer.start();

	  // process pending requests
	  size_t count = requestsSubmitted - requestsCompleted;
	  for(uint32_t i = 0; i < count; ++i)
	  {
		 bool success;
		 auto data = retrieveCompletion(false, success);
		 if(!success)
			break;
		 if(data)
		 {
			// if (debugUring)
			//	printf("Close: deallocating  %p\n", data->iov.iov_base);
			grk_bin::grk_aligned_free(data->iov.iov_base);
			delete data;
		 }
	  }
	  io_uring_queue_exit(&ring);
	  memset(&ring, 0, sizeof(ring));

	  // timer.finish();
   }
   requestsSubmitted = 0;
   requestsCompleted = 0;
   bool rc = !ownsDescriptor || (fd_ && ::close(fd_) == 0);
   fd_ = 0;
   ownsDescriptor = false;

   return rc;
}

uint64_t FileUringIO::write(uint8_t* buf, uint64_t offset, size_t len, size_t maxLen, bool pooled)
{
   GrkIOBuf b = GrkIOBuf(buf, offset, len, maxLen, pooled);

   return write(b);
}
uint64_t FileUringIO::write(GrkIOBuf buffer)
{
   io_data* data = new io_data();
   if(!buffer.pooled_)
   {
	  auto b = (uint8_t*)grk_bin::grk_aligned_malloc(buffer.len_);
	  if(!b)
		 return false;
	  memcpy(b, buffer.data_, buffer.len_);
	  buffer.data_ = b;
   }
   data->buf = buffer;
   data->iov.iov_base = buffer.data_;
   data->iov.iov_len = buffer.len_;
   enqueue(&ring, data, false, fd_);

   return buffer.len_;
}
bool FileUringIO::read([[maybe_unused]] uint8_t* buf, [[maybe_unused]] size_t len)
{
   throw new std::runtime_error("uring read");

   return false;
}
uint64_t FileUringIO::seek([[maybe_unused]] int64_t pos, [[maybe_unused]] int whence)
{
   throw new std::runtime_error("uring seek");

   return 0;
}

#endif
