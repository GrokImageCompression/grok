/*
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
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
 */
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "grok.h"

static const unsigned char codeStreamHeader[] = {0xff, 0x4f};
static const unsigned char fileFormatHeader[] = {0x6a, 0x50, 0x20, 0x20};
extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv);
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *buf, size_t len);
typedef struct {
  const uint8_t *data;
  size_t offset;
  size_t len;
} MemoryBuf;
static size_t ReadCB(void *pBuffer, size_t numBytes, void *userData) {
 auto memBuf = (MemoryBuf *)userData;
  if (memBuf->offset >= memBuf->len)
    return 0;
  if (memBuf->offset + numBytes >= memBuf->len) {
    size_t bytesToRead = memBuf->len - memBuf->offset;
    memcpy(pBuffer, memBuf->data + memBuf->offset, bytesToRead);
    memBuf->offset = memBuf->len;

    return bytesToRead;
  }
  if (numBytes == 0)
    return 0;
  memcpy(pBuffer, memBuf->data + memBuf->offset, numBytes);
  memBuf->offset += numBytes;

  return numBytes;
}
static bool SeekCB(size_t numBytes, void *userData) {
  auto memBuf = (MemoryBuf *)userData;
  memBuf->offset = numBytes;

  return true;
}
struct Initializer {
  Initializer() { grk_initialize(nullptr, 0); }
};
int LLVMFuzzerInitialize(int *argc, char ***argv) {
  static Initializer init;
  return 0;
}
int LLVMFuzzerTestOneInput(const uint8_t *buf, size_t len) {
  GRK_CODEC_FORMAT eCodecFormat;
  if (len >= sizeof(codeStreamHeader) &&
      memcmp(buf, codeStreamHeader, sizeof(codeStreamHeader)) == 0)
    eCodecFormat = GRK_CODEC_J2K;
  else if (len >= 4 + sizeof(fileFormatHeader) &&
             memcmp(buf + 4, fileFormatHeader, sizeof(fileFormatHeader)) == 0)
    eCodecFormat = GRK_CODEC_JP2;
  else
    return 0;

  auto stream = grk_stream_new(1024, true);
  MemoryBuf memBuf;
  memBuf.data = buf;
  memBuf.len = len;
  memBuf.offset = 0;
  grk_stream_set_user_data_length(stream, len);
  grk_stream_set_read_function(stream, ReadCB);
  grk_stream_set_seek_function(stream, SeekCB);
  grk_stream_set_user_data(stream, &memBuf, nullptr);
  auto codec = grk_decompress_create(eCodecFormat, stream);
  grk_set_msg_handlers(nullptr, nullptr,
					  nullptr, nullptr,
					  nullptr, nullptr);
  grk_decompress_core_params parameters;
  grk_decompress_set_default_params(&parameters);
  grk_decompress_init(codec, &parameters);
  grk_image *image = nullptr;
  grk_header_info headerInfo;
  memset(&headerInfo,0,sizeof(grk_header_info));
  uint32_t x0, y0, width, height;
  if (!grk_decompress_read_header(codec, &headerInfo))
    goto cleanup;
  image = grk_decompress_get_composited_image(codec);
  width = image->x1 - image->x0;
  if (width > 1024)
    width = 1024;
  height = image->y1 - image->y0;
  if (height > 1024)
    height = 1024;
  x0 = 10;
  if (x0 >= width)
    x0 = 0;
  y0 = 10;
  if (y0 >= height)
    y0 = 0;
  if (grk_decompress_set_window(codec, x0, y0, width, height)) {
    if (!grk_decompress(codec, nullptr))
      goto cleanup;
  }
  grk_decompress_end(codec);
cleanup:
  grk_object_unref(stream);
  grk_object_unref(codec);

  return 0;
}
