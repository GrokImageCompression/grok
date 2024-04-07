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

#pragma once

#include "grk_apps_config.h"
#ifdef GROK_HAVE_URING

#include "IFileIO.h"
#include <liburing.h>
#include <liburing/io_uring.h>
#include <mutex>

struct io_data
{
   io_data() : iov{0, 0} {}
   GrkIOBuf buf;
   iovec iov;
};

class FileUringIO : public IFileIO
{
 public:
   FileUringIO();
   void registerGrkReclaimCallback(grk_io_callback reclaim_callback, void* user_data);
   bool open(const std::string& fileName, const std::string& mode) override;
   bool attach(std::string fileName, std::string mode, int fd);
   bool close(void) override;
   uint64_t write(uint8_t* buf, uint64_t offset, size_t len, size_t maxLen, bool pooled) override;
   uint64_t write(GrkIOBuf buffer) override;
   bool read(uint8_t* buf, size_t len) override;
   uint64_t seek(int64_t pos, int whence) override;
   io_data* retrieveCompletion(bool peek, bool& success);

 private:
   io_uring ring;
   int fd_;
   bool ownsDescriptor;
   std::string fileName_;
   size_t requestsSubmitted;
   size_t requestsCompleted;
   int getMode(const char* mode);
   void enqueue(io_uring* ring, io_data* data, bool readop, int fd);
   bool initQueue(void);

   const uint32_t QD = 1024;
   grk_io_callback reclaim_callback_;
   void* reclaim_user_data_;
};

#endif
