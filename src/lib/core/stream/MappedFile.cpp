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

#include "grk_fseek.h"
#include "MemAdvisor.h"

#ifdef _WIN32
#include <windows.h>
#else /* _WIN32 */
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#endif
#include <fcntl.h>

#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "grk_includes.h"
#include "IStream.h"
#include "StreamIO.h"
#include "FetchCommon.h"
#include "TPFetchSeq.h"
#include "MemStream.h"
#include "StreamGenerator.h"
#include "BufferedStream.h"

namespace grk
{
static int32_t get_file_open_mode(const char* mode)
{
  int32_t m = -1;
  switch(mode[0])
  {
    case 'r':
      m = O_RDONLY;
      if(mode[1] == '+')
        m = O_RDWR;
      break;
    case 'w':
    case 'a':
      m = O_WRONLY | O_CREAT;
      if(mode[0] == 'w')
        m |= O_TRUNC;
      break;
    default:
      break;
  }
  return m;
}

#ifdef _WIN32

static uint64_t size_proc(grk_handle fd)
{
  LARGE_INTEGER filesize = {0};
  if(GetFileSizeEx(fd, &filesize))
    return (uint64_t)filesize.QuadPart;
  return 0;
}

static void* grk_map(grk_handle fd, size_t len, bool do_read)
{
  void* ptr = nullptr;
  HANDLE hMapFile = nullptr;

  if(!fd || !len)
    return nullptr;

  /* Passing in 0 for the maximum file size indicates that we
  would like to create a file mapping object for the full file size */
  hMapFile =
      CreateFileMapping(fd, nullptr, do_read ? PAGE_READONLY : PAGE_READWRITE, 0, 0, nullptr);
  if(hMapFile == nullptr)
  {
    return nullptr;
  }
  ptr = MapViewOfFile(hMapFile, do_read ? FILE_MAP_READ : FILE_MAP_WRITE, 0, 0, 0);
  CloseHandle(hMapFile);
  return ptr;
}

static int32_t unmap(void* ptr, [[maybe_unused]] size_t len)
{
  int32_t rc = -1;
  if(ptr)
    rc = UnmapViewOfFile(ptr) ? 0 : -1;
  return rc;
}

static grk_handle open_fd(const char* fname, const char* mode)
{
  void* fd = nullptr;
  int32_t m = -1;
  DWORD dwMode = 0;

  if(!fname)
    return (grk_handle)-1;

  m = get_file_open_mode(mode);
  switch(m)
  {
    case O_RDONLY:
      dwMode = OPEN_EXISTING;
      break;
    case O_RDWR:
    case O_WRONLY | O_CREAT:
      dwMode = OPEN_ALWAYS;
      break;
    case O_WRONLY | O_TRUNC:
    case O_WRONLY | O_CREAT | O_TRUNC:
      dwMode = CREATE_ALWAYS;
      break;
    default:
      return nullptr;
  }

  fd = (grk_handle)CreateFileA(
      fname, (m == O_RDONLY) ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE),
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, dwMode,
      (m == O_RDONLY) ? FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_NORMAL, nullptr);
  if(fd == INVALID_HANDLE_VALUE)
    return (grk_handle)-1;

  return fd;
}

static int32_t close_fd(grk_handle fd)
{
  int32_t rc = -1;
  if(fd)
    rc = CloseHandle(fd) ? 0 : -1;

  return rc;
}

#else

static uint64_t size_proc(grk_handle fd)
{
  struct stat sb;
  if(!fd)
    return 0;

  if(fstat(fd, &sb) < 0)
    return (0);
  else
    return ((uint64_t)sb.st_size);
}

static void* grk_map(grk_handle fd, [[maybe_unused]] size_t len, bool do_read)
{
  if(!fd)
    return nullptr;

  // Ensure write mappings have read permissions as well for compatibility
  int prot = do_read ? PROT_READ : (PROT_READ | PROT_WRITE);

  void* ptr = mmap(nullptr, len, prot, MAP_SHARED, fd, 0);
  return ptr == MAP_FAILED ? nullptr : ptr;
}

static int32_t unmap(void* ptr, size_t len)
{
  int32_t rc = -1;
  if(ptr)
    rc = munmap(ptr, len);
  return rc;
}

static grk_handle open_fd(const char* fname, const char* mode)
{
  grk_handle fd = 0;
  int32_t m = -1;
  if(!fname)
    return (grk_handle)-1;
  m = get_file_open_mode(mode);
  if(m == -1)
    return (grk_handle)-1;
  fd = open(fname, m, 0666);
  if(fd < 0)
  {
    if(errno > 0 && strerror(errno) != nullptr)
    {
      grklog.error("%s: %s", fname, strerror(errno));
    }
    else
    {
      grklog.error("%s: Cannot open", fname);
    }
    return (grk_handle)-1;
  }
  return fd;
}

static int32_t close_fd(grk_handle fd)
{
  if(!fd)
    return 0;
  return close(fd);
}

#endif

static void mem_map_free(void* user_data)
{
  if(user_data)
  {
    auto stream = (MemStream*)user_data;
    int32_t rc =
        unmap(stream->buf_ - stream->initialOffset_, stream->len_ + stream->initialOffset_);
    if(rc)
      grklog.error("Unmapping memory mapped file failed with error %u", rc);
    rc = close_fd(stream->fd_);
    if(rc)
      grklog.error("Closing memory mapped file failed with error %u", rc);
    delete stream;
  }
}

IStream* createMappedFileReadStream(grk_stream_params* stream_param)
{
  auto fname = stream_param->file;
  grk_handle fd = open_fd(fname, "r");
  if(fd == (grk_handle)-1)
  {
    grklog.error("Unable to open memory mapped file %s", fname);
    return nullptr;
  }
  size_t len = (size_t)size_proc(fd);
  if(len < GRK_JPEG_2000_NUM_IDENTIFIER_BYTES)
  {
    grklog.error("File length %lu too short.", len);
    return nullptr;
  }
  if(stream_param->initial_offset > len)
  {
    grklog.error("File offset %lu must be less than file length %lu.", stream_param->initial_offset,
                 len);
    return nullptr;
  }

  auto memStream = new MemStream();
  memStream->fd_ = fd;
  auto mapped_view = grk_map(fd, len, true);
  if(!mapped_view)
  {
    grklog.error("Unable to map memory mapped file %s", fname);
    mem_map_free(memStream);
    return nullptr;
  }
  memStream->buf_ = (uint8_t*)mapped_view + stream_param->initial_offset;
  memStream->initialOffset_ = stream_param->initial_offset;
  memStream->len_ = len - stream_param->initial_offset;

  GRK_CODEC_FORMAT fmt;
  if(!detectFormat(memStream->buf_, &fmt))
  {
    grklog.error("Unable to detect codec format.");
    return nullptr;
  }

  // now treat mapped file like any other memory stream
  auto stream = new BufferedStream(memStream->buf_, 0, memStream->len_, true);
  stream->setFormat(fmt);
  stream->setUserData(memStream, (grk_stream_free_user_data_fn)mem_map_free, memStream->len_);
#ifndef _WIN32
  stream->setMemAdvisor(new MemAdvisor((uint8_t*)mapped_view, len, stream_param->initial_offset));
#endif
  memStreamSetup(stream, true);

  return stream;
}

} // namespace grk
