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

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include "grok.h"

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv);
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* buf, size_t len);

static int temp_file_descriptor = -1;
static char temp_file_path[] = "/tmp/grk_mercury_fuzz_XXXXXX";

struct Initializer
{
  Initializer()
  {
    setenv("GRK_MERCURY", "1", 1);
    temp_file_descriptor = mkstemp(temp_file_path);
    grk_initialize(nullptr, 0, nullptr);
  }
};
int LLVMFuzzerInitialize(int* argc, char*** argv)
{
  static Initializer init;
  return 0;
}
int LLVMFuzzerTestOneInput(const uint8_t* buf, size_t len)
{
  grk_header_info headerInfo = {};
  grk_decompress_parameters parameters = {};
  grk_object* codec = nullptr;
  grk_stream_params stream_params = {};

  if(temp_file_descriptor < 0)
    return 0;
  if(ftruncate(temp_file_descriptor, 0) != 0)
    return 0;
  if(lseek(temp_file_descriptor, 0, SEEK_SET) != 0)
    return 0;
  size_t written = 0;
  while(written < len)
  {
    ssize_t count = write(temp_file_descriptor, buf + written, len - written);
    if(count <= 0)
      return 0;
    written += (size_t)count;
  }

  strncpy(stream_params.file, temp_file_path, sizeof(stream_params.file) - 1);
  codec = grk_decompress_init(&stream_params, &parameters);
  if(!codec)
    goto cleanup;
  if(!grk_decompress_read_header(codec, &headerInfo))
    goto cleanup;
  if(!grk_decompress(codec, nullptr))
    goto cleanup;
cleanup:
  grk_object_unref(codec);

  return 0;
}
