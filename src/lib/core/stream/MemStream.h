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

#pragma once

#include <cstdint>
#include <cstddef>

namespace grk
{
#ifdef _WIN32
typedef void* grk_handle;
#else
typedef int32_t grk_handle;
#endif

struct MemStream
{
  MemStream(uint8_t* buffer, size_t initialOffset, size_t length);
  MemStream();
  size_t len_;
  grk_handle fd_; // for file mapping
  size_t off_;
  uint8_t* buf_;
  size_t initialOffset_; // mapping: buf is shifted by initialOffset
                         // and will be shifted back when unmapping
};

void memStreamSetup(IStream* stream, bool isReadStream);

/** Create stream from buffer the caller keeps ownership of
 *
 * @param buf           buffer
 * @param len    		length of buffer
 * @param format        codec format, or GRK_CODEC_UNK to detect it from the buffer
 * @param readStream  whether the stream is a read stream (true) or not (false)
 */
IStream* memStreamCreate(uint8_t* buf, size_t len, GRK_CODEC_FORMAT format, bool readStream);

} // namespace grk
