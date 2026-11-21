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

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "grok.h"
#include "grok_codec.h"

namespace
{
constexpr uint32_t kWidth = 8;
constexpr uint32_t kHeight = 6;
constexpr uint32_t kOffsetX = 3;
constexpr uint32_t kOffsetY = 5;

int32_t expectedSample(uint32_t y, uint32_t x)
{
  return (int32_t)((x * 7 + y * 13) & 0xFF);
}

bool writePgx(const std::string& path)
{
  FILE* file = fopen(path.c_str(), "wb");
  if(!file)
    return false;
  bool written = fprintf(file, "PG ML + 8 %u %u\n", kWidth, kHeight) > 0;
  for(uint32_t y = 0; y < kHeight; ++y)
    for(uint32_t x = 0; x < kWidth; ++x)
      written = written && fputc(expectedSample(y, x), file) != EOF;

  return fclose(file) == 0 && written;
}

bool decodedImageMatches(const std::string& codestream)
{
  grk_decompress_parameters decompressParams = {};
  grk_stream_params stream = {};
  snprintf(stream.file, sizeof(stream.file), "%s", codestream.c_str());
  auto codec = grk_decompress_init(&stream, &decompressParams);
  grk_header_info header = {};
  if(!codec || !grk_decompress_read_header(codec, &header))
  {
    fprintf(stderr, "%s: header read failed\n", codestream.c_str());
    grk_object_unref(codec);
    return false;
  }
  grk_image* decoded = nullptr;
  if(grk_decompress(codec, nullptr))
    decoded = grk_decompress_get_image(codec);
  if(!decoded)
  {
    fprintf(stderr, "%s: decode failed\n", codestream.c_str());
    grk_object_unref(codec);
    return false;
  }
  auto comp = decoded->comps;
  bool int16Samples = comp->data_type == GRK_INT_16;
  bool match = decoded->x0 == kOffsetX && decoded->y0 == kOffsetY &&
               decoded->x1 == kOffsetX + kWidth && decoded->y1 == kOffsetY + kHeight &&
               comp->w == kWidth && comp->h == kHeight &&
               (int16Samples || comp->data_type == GRK_INT_32);
  if(!match)
    fprintf(stderr, "decoded grid (%u,%u)-(%u,%u), component %ux%u\n", decoded->x0, decoded->y0,
            decoded->x1, decoded->y1, comp->w, comp->h);
  for(uint32_t y = 0; match && y < kHeight; ++y)
  {
    for(uint32_t x = 0; x < kWidth; ++x)
    {
      size_t index = (size_t)y * comp->stride + x;
      int32_t got =
          int16Samples ? ((const int16_t*)comp->data)[index] : ((const int32_t*)comp->data)[index];
      if(got != expectedSample(y, x))
      {
        fprintf(stderr, "(%u,%u) is %d, expected %d\n", x, y, got, expectedSample(y, x));
        match = false;
      }
    }
  }
  grk_object_unref(codec);

  return match;
}
} // namespace

int main()
{
  const std::string pgx = "grk_pgx_image_offset.pgx";
  const std::string codestream = "grk_pgx_image_offset.j2k";
  if(!writePgx(pgx))
  {
    fprintf(stderr, "%s: could not write\n", pgx.c_str());
    return EXIT_FAILURE;
  }
  const std::string offset = std::to_string(kOffsetX) + "," + std::to_string(kOffsetY);
  const char* argv[] = {"grk_compress",     "-i", pgx.c_str(),   "-o",
                        codestream.c_str(), "-d", offset.c_str()};
  bool compressed = grk_codec_compress(7, argv, nullptr, nullptr) == EXIT_SUCCESS;
  if(!compressed)
    fprintf(stderr, "%s: compress failed\n", pgx.c_str());
  bool match = compressed && decodedImageMatches(codestream);
  remove(pgx.c_str());
  remove(codestream.c_str());

  return match ? EXIT_SUCCESS : EXIT_FAILURE;
}
