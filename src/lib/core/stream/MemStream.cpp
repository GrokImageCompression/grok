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

#include "grk_includes.h"
#include "BufferedStream.h"

namespace grk
{

static const char* JP2_RFC3745_MAGIC = "\x00\x00\x00\x0c\x6a\x50\x20\x20\x0d\x0a\x87\x0a";
static const char* CODESTREAM_MAGIC = "\xff\x4f\xff\x51";
static const char* JP2_MAGIC = "\x6a\x70";
static const char* MJ2_MAGIC = "\x6d\x6a";

bool detectFormat(const uint8_t* buffer, GRK_CODEC_FORMAT* fmt)
{
  *fmt = GRK_CODEC_UNK;
  if(memcmp(buffer, JP2_RFC3745_MAGIC, 12) == 0)
  {
    if(memcmp(buffer + 20, JP2_MAGIC, 2) == 0)
      *fmt = GRK_CODEC_JP2;
    else if(memcmp(buffer + 20, MJ2_MAGIC, 2) == 0)
      *fmt = GRK_CODEC_MJ2;
  }
  else if(memcmp(buffer, CODESTREAM_MAGIC, 4) == 0)
  {
    *fmt = GRK_CODEC_J2K;
  }
  else
  {
    grklog.error("No JPEG 2000 code stream detected.");
    return false;
  }
  return true;
}

static void memStreamUserDataFree(void* user_data)
{
  uint8_t* buf = (uint8_t*)user_data;
  delete[] buf;
}

static void memStreamFree(void* user_data)
{
  auto data = (MemStream*)user_data;
  if(data)
    delete data;
}

static size_t memStreamZeroCopyRead(uint8_t** buffer, size_t numBytes, void* src)
{
  size_t nb_read = 0;
  auto srcStream = (MemStream*)src;

  if(((size_t)srcStream->off_ + numBytes) < srcStream->len_)
    nb_read = numBytes;

  *buffer = srcStream->buf_ + srcStream->off_;
  assert(srcStream->off_ + nb_read <= srcStream->len_);
  srcStream->off_ += nb_read;

  return nb_read;
}

static size_t memStreamRead(uint8_t* dest, size_t numBytes, void* src)
{
  size_t nb_read;

  if(!dest)
    return 0;

  auto srcStream = (MemStream*)src;

  if(srcStream->off_ + numBytes < srcStream->len_)
    nb_read = numBytes;
  else
    nb_read = (size_t)(srcStream->len_ - srcStream->off_);

  if(nb_read)
  {
    assert(srcStream->off_ + nb_read <= srcStream->len_);
    // (don't copy buffer into itself)
    if(dest != srcStream->buf_ + srcStream->off_)
      memcpy(dest, srcStream->buf_ + srcStream->off_, nb_read);
    srcStream->off_ += nb_read;
  }

  return nb_read;
}

static size_t memStreamWrite(const uint8_t* src, size_t numBytes, void* dest)
{
  auto destStream = (MemStream*)dest;
  if(destStream->off_ + numBytes >= destStream->len_)
    return 0;

  if(numBytes)
  {
    memcpy(destStream->buf_ + (size_t)destStream->off_, src, numBytes);
    destStream->off_ += numBytes;
  }
  return numBytes;
}

static bool memStreamSeek(uint64_t numBytes, void* src)
{
  auto srcStream = (MemStream*)src;

  if(numBytes < srcStream->len_)
    srcStream->off_ = numBytes;
  else
    srcStream->off_ = srcStream->len_;

  return true;
}

void memStreamSetup(IStream* stream, bool isReadStream)
{
  StreamCallbacks callbacks;
  if(isReadStream)
  {
    callbacks.readCallback_ = memStreamRead;
    callbacks.readZeroCopyCallback_ = memStreamZeroCopyRead;
  }
  else
  {
    callbacks.writeCallback_ = memStreamWrite;
  }
  callbacks.seekCallback_ = memStreamSeek;
  stream->setCallbacks(callbacks);
}

IStream* memStreamCreate(uint8_t* buf, size_t len, bool ownsBuffer,
                         grk_stream_free_user_data_fn freeCallback, GRK_CODEC_FORMAT format,
                         bool isReadStream)
{
  if(!buf || !len)
    return nullptr;

  if(format == GRK_CODEC_UNK)
  {
    if(len < GRK_JPEG_2000_NUM_IDENTIFIER_BYTES)
    {
      grklog.error("Buffer of length %d is invalid\n", len);
      return nullptr;
    }
    if(isReadStream && !detectFormat(buf, &format))
      return nullptr;
  }

  auto memStream = new MemStream(buf, 0, len, ownsBuffer, freeCallback);
  auto stream = new BufferedStream(buf, 0, len, isReadStream);
  if(isReadStream)
    stream->setFormat(format);
  stream->setUserData(memStream, memStreamFree, memStream->len_);
  memStreamSetup(stream, isReadStream);

  return stream;
}

MemStream::MemStream(uint8_t* buffer, size_t initialOff, size_t length, bool ownsBuffer,
                     grk_stream_free_user_data_fn freeCallback)
    : len_(length), fd_(0), off_(0), buf_(buffer), initialOffset_(initialOff),
      freeCallback_(freeCallback ? freeCallback : (ownsBuffer ? memStreamUserDataFree : nullptr))
{}
MemStream::MemStream() : MemStream(nullptr, 0, 0, false, nullptr) {}
MemStream::~MemStream()
{
  if(freeCallback_)
    freeCallback_(buf_);
}

} // namespace grk
