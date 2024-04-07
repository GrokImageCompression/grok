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

#include <FileStreamIO.h>
#include "spdlog/spdlog.h"
#include "common.h"

FileStreamIO::FileStreamIO() : fileHandle_(nullptr) {}

bool FileStreamIO::open(const std::string& fileName, const std::string& mode)
{
   bool useStdio = grk::useStdio(fileName);
   switch(mode[0])
   {
	  case 'r':
		 if(useStdio)
		 {
			if(!grk::grk_set_binary_mode(stdin))
			   return false;
			fileHandle_ = stdin;
		 }
		 else
		 {
			fileHandle_ = fopen(fileName.c_str(), "rb");
			if(!fileHandle_)
			{
			   spdlog::error("Failed to open {} for reading", fileName);
			   return false;
			}
		 }
		 break;
	  case 'w':
		 if(!grk::grk_open_for_output(&fileHandle_, fileName.c_str(), useStdio))
			return false;
		 break;
   }
   fileName_ = fileName;

   return true;
}
bool FileStreamIO::close(void)
{
   bool rc = true;
   if(!grk::useStdio(fileName_) && fileHandle_)
	  rc = grk::safe_fclose(fileHandle_);
   fileHandle_ = nullptr;
   return rc;
}
uint64_t FileStreamIO::write(uint8_t* buf, [[maybe_unused]] uint64_t offset, size_t len,
							 [[maybe_unused]] size_t maxLen, [[maybe_unused]] bool pooled)
{
   auto actual = fwrite(buf, 1, len, fileHandle_);
   if(actual < len)
	  spdlog::error("wrote fewer bytes {} than expected number of bytes {}.", actual, len);

   return (uint64_t)actual;
}
uint64_t FileStreamIO::write(GrkIOBuf buffer)
{
   auto actual = fwrite(buffer.data_, 1, buffer.len_, fileHandle_);
   if(actual < buffer.len_)
	  spdlog::error("wrote fewer bytes {} than expected number of bytes {}.", actual, buffer.len_);

   return buffer.len_;
}
bool FileStreamIO::read(uint8_t* buf, size_t len)
{
   auto actual = fread(buf, 1, len, fileHandle_);
   if(actual < len)
	  spdlog::error("read fewer bytes {} than expected number of bytes {}.", actual, len);

   return actual == len;
}
uint64_t FileStreamIO::seek(int64_t off, int whence)
{
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
   return GRK_FSEEK(fileHandle_, off, whence);
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif
}

FILE* FileStreamIO::getFileStream()
{
   return fileHandle_;
}
