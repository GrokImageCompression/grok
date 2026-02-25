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

#include <string_view>

#include "grk_config_private.h"
#include "grok.h"
#include "S3Fetcher.h"
#include "GSFetcher.h"
#include "AZFetcher.h"
#include "ADLSFetcher.h"
#include "HTTPFetcher.h"

namespace grk
{

/**
 * Detect JPEG 2000 format from buffer
 * Format is either GRK_FMT_J2K or GRK_FMT_JP2
 *
 * @param buffer buffer
 * @param fmt pointer to detected format
 *
 * @return true if format was detected, otherwise false
 *
 */
bool detectFormat(const uint8_t* buffer, GRK_CODEC_FORMAT* fmt);

#define GRK_JPEG_2000_NUM_IDENTIFIER_BYTES 22

class StreamGenerator
{
public:
  StreamGenerator(grk_stream_params* src)
  {
    streamParams_.initial_offset = src->initial_offset;
    streamParams_.double_buffer_len = src->double_buffer_len;
    streamParams_.initial_double_buffer_len = src->initial_double_buffer_len;
    streamParams_.from_network = src->from_network;
    streamParams_.is_read_stream = src->is_read_stream;

    safe_strcpy(streamParams_.file, src->file);
    streamParams_.use_stdio = src->use_stdio;

    streamParams_.buf = src->buf; // Shallow copy; allocate if deep copy needed
    streamParams_.buf_len = src->buf_len;
    streamParams_.buf_compressed_len = src->buf_compressed_len;

    streamParams_.read_fn = src->read_fn;
    streamParams_.write_fn = src->write_fn;
    streamParams_.seek_fn = src->seek_fn;
    streamParams_.free_user_data_fn = src->free_user_data_fn;
    streamParams_.user_data = src->user_data; // Shallow copy
    streamParams_.stream_len = src->stream_len;

    safe_strcpy(streamParams_.username, src->username);
    safe_strcpy(streamParams_.password, src->password);
    safe_strcpy(streamParams_.bearer_token, src->bearer_token);
    safe_strcpy(streamParams_.custom_header, src->custom_header);
    safe_strcpy(streamParams_.region, src->region);
  }
  IStream* create(void)
  {
    if(streamParams_.buf && streamParams_.buf_len)
    {
      return createBufferStream(streamParams_.is_read_stream);
    }

    if(streamParams_.read_fn || streamParams_.write_fn)
    {
      return createCallbackStream();
    }

    if(streamParams_.file[0] || streamParams_.use_stdio)
    {
      if(streamParams_.is_read_stream && streamParams_.file[0])
      {
        std::string_view file{streamParams_.file};
        bool isNetwork = file.starts_with("http://") || file.starts_with("https://") ||
                         file.starts_with("/vsis3/");
        if(isNetwork && !streamParams_.read_fn)
          return createCurlFetchStream();
      }
      return createFileStream();
    }

    grklog.error("Invalid stream parameters: no valid stream source specified.");
    return nullptr;
  }

private:
  static constexpr size_t DEFAULT_BUFFER_LEN = 4096;
  static constexpr size_t DEFAULT_INITIAL_BUFFER_LEN = 512 * 1024;
  static constexpr bool useCallbacks = false;

  size_t getDoubleBufferLength(size_t configuredLength)
  {
    return configuredLength ? configuredLength : DEFAULT_BUFFER_LEN;
  }

  size_t getInitialDoubleBufferLength(size_t configuredLength)
  {
    return configuredLength ? configuredLength : DEFAULT_INITIAL_BUFFER_LEN;
  }

  // CurlSyncFetch Stream Creation (handles both HTTP/HTTPS and /vsis3/)
  IStream* createCurlFetchStream(void);

  IStream* createFileStream(void);

  IStream* createCallbackStream(void);

  IStream* createBufferStream(bool isReadStream)
  {
    auto stream = memStreamCreate(streamParams_.buf, streamParams_.buf_len, false, nullptr,
                                  GRK_CODEC_FORMAT::GRK_CODEC_UNK, isReadStream);
    if(!stream)
    {
      grklog.error("Unable to create memory stream.");
    }
    return stream;
  }

  bool validateStream(IStream* stream)
  {
    uint8_t buf[GRK_JPEG_2000_NUM_IDENTIFIER_BYTES];
    stream->seek(0);
    if(!stream->read(buf, nullptr, GRK_JPEG_2000_NUM_IDENTIFIER_BYTES))
    {
      return false;
    }

    GRK_CODEC_FORMAT fmt;
    if(!detectFormat(buf, &fmt))
    {
      grklog.error("Unable to detect codec format.");
      return false;
    }

    stream->seek(0);
    stream->setFormat(fmt);
    return true;
  }

  // File operation helpers
  static size_t grkReadFromFile(uint8_t* buffer, size_t numBytes, void* p_file)
  {
    return fread(buffer, 1, numBytes, (FILE*)p_file);
  }

  static uint64_t getDataLengthFromFile(void* filePtr)
  {
    auto file = (FILE*)filePtr;
    GRK_FSEEK(file, 0, SEEK_END);
    int64_t file_length = (int64_t)GRK_FTELL(file);
    GRK_FSEEK(file, 0, SEEK_SET);
    return (uint64_t)file_length;
  }

  static size_t grkWriteToFile(const uint8_t* buffer, size_t numBytes, void* p_file)
  {
    return fwrite(buffer, 1, numBytes, (FILE*)p_file);
  }

  static bool grkSeekInFile(uint64_t numBytes, void* p_user_data)
  {
    if(numBytes > INT64_MAX)
      return false;
    return GRK_FSEEK((FILE*)p_user_data, (int64_t)numBytes, SEEK_SET) ? false : true;
  }

  static void grkFreeFile(void* p_user_data)
  {
    if(p_user_data)
      fclose((FILE*)p_user_data);
  }

private:
  grk_stream_params streamParams_{};

  template<size_t N>
  void safe_strcpy(char (&dest)[N], const char* src)
  {
    if(!src || !src[0])
      return;
    size_t len = strnlen(src, N - 1);
    memcpy(dest, src, len);
    dest[len] = '\0';
  }
};

} // namespace grk