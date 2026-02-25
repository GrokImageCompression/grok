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
#include "buffer.h"
#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "MappedFile.h"
#include "Quantizer.h"
#include "IStream.h"
#include "StreamIO.h"
#include "FetchCommon.h"
#include "TPFetchSeq.h"
#include "MemStream.h"
#include "StreamGenerator.h"
#include "CurlFetcher.h"
#include "BufferedStream.h"

namespace grk
{

IStream* StreamGenerator::createCallbackStream(void)
{
  bool readStream = streamParams_.read_fn;
  size_t doubleBufferLen = getDoubleBufferLength(streamParams_.double_buffer_len);

  if(streamParams_.stream_len && readStream)
  {
    doubleBufferLen = std::min(doubleBufferLen, streamParams_.stream_len);
  }

  size_t initialDoubleBufferLen =
      readStream ? std::max(doubleBufferLen,
                            getInitialDoubleBufferLength(streamParams_.initial_double_buffer_len))
                 : 0;

  if(streamParams_.stream_len && readStream)
  {
    initialDoubleBufferLen = std::min(initialDoubleBufferLen, streamParams_.stream_len);
  }

  auto stream = new BufferedStream(nullptr, initialDoubleBufferLen, doubleBufferLen, readStream);
  uint64_t dataLen = readStream ? streamParams_.stream_len : 0;
  stream->setUserData(streamParams_.user_data, streamParams_.free_user_data_fn, dataLen);
  StreamCallbacks callbacks(&streamParams_);
  stream->setCallbacks(callbacks);

  if(readStream && !validateStream(stream))
  {
    delete stream;
    return nullptr;
  }

  return stream;
}

IStream* StreamGenerator::createFileStream(void)
{
  bool stdin_stdout = !streamParams_.file[0];
  if(streamParams_.is_read_stream && !stdin_stdout)
  {
    return createMappedFileReadStream(&streamParams_);
  }
  FILE* file = nullptr;

  if(stdin_stdout)
  {
    file = streamParams_.is_read_stream ? stdin : stdout;
  }
  else
  {
    const char* mode = streamParams_.is_read_stream ? "rb" : "wb";
    file = fopen(streamParams_.file, mode);
    if(!file)
    {
      grklog.error("Failed to open file %s.", streamParams_.file);
      return nullptr;
    }
  }

  auto stream = new BufferedStream(
      nullptr, getInitialDoubleBufferLength(streamParams_.initial_double_buffer_len),
      getDoubleBufferLength(streamParams_.double_buffer_len), streamParams_.is_read_stream);
  uint64_t dataLen = streamParams_.is_read_stream ? getDataLengthFromFile(file) : 0;
  stream->setUserData(file, stdin_stdout ? nullptr : grkFreeFile, dataLen);
  StreamCallbacks callbacks(grkReadFromFile, nullptr, grkSeekInFile, grkWriteToFile);
  stream->setCallbacks(callbacks);

  if(streamParams_.is_read_stream && !validateStream(stream))
  {
    delete stream;
    return nullptr;
  }

  return stream;
}

// CurlSyncFetch Stream Creation (handles both HTTP/HTTPS and /vsis3/)
IStream* StreamGenerator::createCurlFetchStream(void)
{
  if(!streamParams_.is_read_stream)
  {
    grklog.error("CurlSyncFetch stream is only supported for reading.");
    return nullptr;
  }

#ifdef GRK_ENABLE_LIBCURL
  FetchAuth auth{};
  if(streamParams_.username[0])
    auth.username_ = streamParams_.username;
  if(streamParams_.password[0])
    auth.password_ = streamParams_.password;
  auto fetcher = new S3Fetcher();
  fetcher->init(streamParams_.file, auth);
  uint64_t dataLen = fetcher->size();
  auto initial_double_buffer_len =
      getInitialDoubleBufferLength(streamParams_.initial_double_buffer_len);
  auto double_buffer_len = getDoubleBufferLength(streamParams_.double_buffer_len);
  initial_double_buffer_len = std::min(initial_double_buffer_len, (size_t)dataLen);
  double_buffer_len = std::min(double_buffer_len, (size_t)dataLen);
  auto stream = new BufferedStream(nullptr, initial_double_buffer_len, double_buffer_len,
                                   true // read-only stream
  );
  stream->setUserData(fetcher, [](void* p) { delete static_cast<CurlFetcher*>(p); }, dataLen);
  StreamCallbacks callbacks(
      [](uint8_t* buffer, size_t numBytes, void* user_data) -> size_t {
        auto fetcher = static_cast<CurlFetcher*>(user_data);
        return fetcher->read(buffer, numBytes);
      },
      nullptr,
      [](uint64_t offset, void* user_data) -> bool {
        return static_cast<CurlFetcher*>(user_data)->seek(offset);
      },
      nullptr);
  stream->setCallbacks(callbacks);
  stream->setFetcher(fetcher);

  if(!validateStream(stream))
  {
    delete stream;
    return nullptr;
  }

  return stream;
#else
  grklog.error("CurlSyncFetch stream unavailable: libcurl not enabled.");
  return nullptr;
#endif
}

} // namespace grk