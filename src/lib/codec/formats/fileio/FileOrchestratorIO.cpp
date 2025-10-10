/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

#ifndef _WIN32
#include <unistd.h>
#endif

#include "FileOrchestratorIO.h"
#include "spdlogwrapper.h"
#include "common.h"
#define IO_MAX 2147483647U

FileOrchestratorIO::FileOrchestratorIO(void)
    :
#ifndef _WIN32
      fd_(-1),
#endif
      numPooledRequests_(0), max_pooled_requests_(0), asynchActive_(false), off_(0),
      reclaim_callback_(nullptr), reclaim_user_data_(nullptr)
{}
void FileOrchestratorIO::setMaxPooledRequests(uint32_t maxRequests)
{
  max_pooled_requests_ = maxRequests;
}
void FileOrchestratorIO::registerGrkReclaimCallback([[maybe_unused]] grk_io_init io_init,
                                                    grk_io_callback reclaim_callback,
                                                    void* user_data)
{
  reclaim_callback_ = reclaim_callback;
  reclaim_user_data_ = user_data;
}
grk_io_callback FileOrchestratorIO::getIOReclaimCallback(void)
{
  return reclaim_callback_;
}
void* FileOrchestratorIO::getIOReclaimUserData(void)
{
  return reclaim_user_data_;
}
#ifdef _WIN32

bool FileOrchestratorIO::open(const std::string& name, const std::string& mode,
                              [[maybe_unused]] bool asynch)
{
  return fileStreamIO.open(name, mode);
}
bool FileOrchestratorIO::close(void)
{
  return fileStreamIO.close();
}
size_t FileOrchestratorIO::write(uint8_t* buf, size_t size)
{
  return (size_t)fileStreamIO.write(buf, 0, size, size, false);
}
uint64_t FileOrchestratorIO::seek(int64_t off, int whence)
{
  return fileStreamIO.seek(off, whence);
}

#else
int FileOrchestratorIO::getFd(void)
{
  return fd_;
}
int FileOrchestratorIO::getMode(std::string mode)
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
      m = O_WRONLY | O_CREAT | O_TRUNC;
      break;
    case 'a':
      m = O_WRONLY | O_CREAT;
      break;
    default:
      spdlog::error("Bad mode {}", mode);
      break;
  }
  return (m);
}

bool FileOrchestratorIO::open(const std::string& name, const std::string& mode,
                              [[maybe_unused]] bool asynch)
{
  bool useStdio = grk::useStdio(name);
  bool doRead = mode[0] == 'r';
  int fd = 0;
  if(useStdio)
  {
    fd = doRead ? STDIN_FILENO : STDOUT_FILENO;
  }
  else
  {
    int m = getMode(mode);
    if(m == -1)
      return false;

    fd = ::open(name.c_str(), m, 0666);
    if(fd < 0)
    {
      if(errno > 0 && strerror(errno) != NULL)
        spdlog::error("{}: {}", name, strerror(errno));
      else
        spdlog::error("Cannot open {}", name);
      return false;
    }
  }
  fd_ = fd;
  filename_ = name;
  mode_ = mode;

  return true;
}
bool FileOrchestratorIO::close(void)
{
  if(fd_ < 0)
    return true;

  int rc = ::close(fd_);
  fd_ = -1;

  return rc == 0;
}
uint64_t FileOrchestratorIO::seek(int64_t off, int32_t whence)
{
  if(asynchActive_)
    return off_;
  off_t rc = lseek(getFd(), off, whence);
  if(rc == (off_t)-1)
  {
    if(strerror(errno) != NULL)
      spdlog::error("{}", strerror(errno));
    else
      spdlog::error("I/O error");
    return (uint64_t)-1;
  }

  return (uint64_t)rc;
}
size_t FileOrchestratorIO::write(uint8_t* buf, size_t bytes_total)
{
  // synchronous write
  ssize_t count = 0;
  size_t bytes_written = 0;
  for(; bytes_written < bytes_total; bytes_written += (size_t)count)
  {
    const char* buf_offset = (char*)buf + bytes_written;
    size_t io_size = (size_t)(bytes_total - bytes_written);
    if(io_size > IO_MAX)
      io_size = IO_MAX;
    count = ::write(fd_, buf_offset, io_size);
    if(count <= 0)
      break;
    off_ += (uint64_t)count;
  }

  return (size_t)count;
}
#endif // #ifndef _WIN32

void FileOrchestratorIO::incrementPooled(void)
{
  // write method will take care of incrementing numPixelRequests if uring is enabled
  numPooledRequests_++;
}

uint32_t FileOrchestratorIO::getNumPooledRequests(void)
{
  return numPooledRequests_;
}
uint64_t FileOrchestratorIO::getOffset(void)
{
  return off_;
}
bool FileOrchestratorIO::allPooledRequestsComplete(void)
{
  return numPooledRequests_ == max_pooled_requests_;
}
