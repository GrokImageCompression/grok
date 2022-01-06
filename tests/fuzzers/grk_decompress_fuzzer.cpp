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
 *
 *
 *    This source code incorporates work covered by the following license:
 *
 * The copyright in this software is being made available under the 2-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2017, IntoPix SA <contact@intopix.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "grok.h"

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv);
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *buf, size_t len);

typedef struct {
  const uint8_t *pabyData;
  size_t nCurPos;
  size_t nLength;
} MemFile;

static void ErrorCallback(const char *msg, void *user_data) {
  //fprintf(stderr, "error: %s\n", msg);
}

static void WarningCallback(const char *msg, void *user_data) {
  //fprintf(stderr, "warning: %s\n", msg);
}

static void InfoCallback(const char *msg, void *user_data) {
  //fprintf(stderr, "info: %s\n", msg);
}

static size_t ReadCallback(void *pBuffer, size_t nBytes, void *pUserData) {
  MemFile *memFile = (MemFile *)pUserData;
  // printf("want to read %u bytes at %u\n", (int)memFile->nCurPos,
  // (int)nBytes);
  if (memFile->nCurPos >= memFile->nLength) {
    return 0;
  }
  if (memFile->nCurPos + nBytes >= memFile->nLength) {
    size_t nToRead = memFile->nLength - memFile->nCurPos;
    memcpy(pBuffer, memFile->pabyData + memFile->nCurPos, nToRead);
    memFile->nCurPos = memFile->nLength;
    return nToRead;
  }
  if (nBytes == 0) {
    return 0;
  }
  memcpy(pBuffer, memFile->pabyData + memFile->nCurPos, nBytes);
  memFile->nCurPos += nBytes;
  return nBytes;
}

static bool SeekCallback(size_t nBytes, void *pUserData) {
  MemFile *memFile = (MemFile *)pUserData;
  // printf("seek to %u\n", (int)nBytes);
  memFile->nCurPos = nBytes;
  return true;
}

struct Initializer {
  Initializer() { grk_initialize(nullptr, 0); }
};

int LLVMFuzzerInitialize(int *argc, char ***argv) {
  static Initializer init;
  return 0;
}

static const unsigned char jpc_header[] = {0xff, 0x4f};
static const unsigned char jp2_box_jp[] = {0x6a, 0x50, 0x20, 0x20}; /* 'jP  ' */

int LLVMFuzzerTestOneInput(const uint8_t *buf, size_t len) {
  GRK_CODEC_FORMAT eCodecFormat;
  if (len >= sizeof(jpc_header) &&
      memcmp(buf, jpc_header, sizeof(jpc_header)) == 0) {
    eCodecFormat = GRK_CODEC_J2K;
  } else if (len >= 4 + sizeof(jp2_box_jp) &&
             memcmp(buf + 4, jp2_box_jp, sizeof(jp2_box_jp)) == 0) {
    eCodecFormat = GRK_CODEC_JP2;
  } else {
    return 0;
  }
  auto pStream = grk_stream_new(1024, true);
  MemFile memFile;
  memFile.pabyData = buf;
  memFile.nLength = len;
  memFile.nCurPos = 0;
  grk_stream_set_user_data_length(pStream, len);
  grk_stream_set_read_function(pStream, ReadCallback);
  grk_stream_set_seek_function(pStream, SeekCallback);
  grk_stream_set_user_data(pStream, &memFile, nullptr);
  auto codec = grk_decompress_create(eCodecFormat, pStream);
  grk_set_info_handler(InfoCallback, nullptr);
  grk_set_warning_handler(WarningCallback, nullptr);
  grk_set_error_handler(ErrorCallback, nullptr);
  grk_decompress_core_params parameters;
  grk_decompress_set_default_params(&parameters);
  grk_decompress_init(codec, &parameters);
  grk_image *psImage = nullptr;
  grk_header_info header_info;
  memset(&header_info,0,sizeof(grk_header_info));
  uint32_t x0, y0, width, height;
  if (!grk_decompress_read_header(codec, &header_info))
    goto cleanup;
  psImage = grk_decompress_get_composited_image(codec);
  width = psImage->x1 - psImage->x0;
  if (width > 1024)
    width = 1024;
  height = psImage->y1 - psImage->y0;
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
  grk_object_unref(pStream);
  grk_object_unref(codec);

  return 0;
}
