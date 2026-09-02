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

// compress_init rejects any component data_type other than GRK_INT_32
#include <cstdint>
#include <cstdio>
#include <vector>

#include "grok.h"

namespace
{
int g_failures = 0;
constexpr uint32_t kWidth = 8;
constexpr uint32_t kHeight = 8;

grk_image* makeImage(grk_data_type type)
{
  grk_image_comp param = {};
  param.w = kWidth;
  param.h = kHeight;
  param.dx = 1;
  param.dy = 1;
  param.prec = 8;
  param.sgnd = false;
  param.data_type = type;
  return grk_image_new(1, &param, GRK_CLRSPC_GRAY, true);
}

grk_object* tryInit(grk_image* image, std::vector<uint8_t>& buf)
{
  grk_cparameters params;
  grk_compress_set_default_params(&params);
  params.cod_format = GRK_FMT_J2K;
  grk_stream_params stream = {};
  stream.buf = buf.data();
  stream.buf_len = buf.size();
  return grk_compress_init(&stream, &params, image);
}

void expectInit(grk_data_type type, bool shouldSucceed)
{
  auto image = makeImage(type);
  if(!image)
  {
    ++g_failures;
    std::fprintf(stderr, "FAIL type %d: image allocation\n", (int)type);
    return;
  }
  std::vector<uint8_t> buf(4096);
  auto codec = tryInit(image, buf);
  bool succeeded = codec != nullptr;
  if(succeeded != shouldSucceed)
  {
    ++g_failures;
    std::fprintf(stderr, "FAIL type %d: compress_init %s, expected %s\n", (int)type,
                 succeeded ? "succeeded" : "failed", shouldSucceed ? "success" : "failure");
  }
  grk_object_unref(codec);
  grk_object_unref(&image->obj);
}

} // namespace

int main()
{
  grk_initialize(nullptr, 0, nullptr);
  expectInit(GRK_INT_32, true);
  expectInit(GRK_INT_16, false);
  expectInit(GRK_INT_8, false);
  expectInit(GRK_FLOAT, false);
  expectInit(GRK_DOUBLE, false);
  grk_deinitialize();

  if(g_failures == 0)
  {
    std::fprintf(stderr, "GrkCompressDataTypeTest: all tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "GrkCompressDataTypeTest: %d failure(s)\n", g_failures);
  return 1;
}
