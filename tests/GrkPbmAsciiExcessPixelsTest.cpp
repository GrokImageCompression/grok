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
constexpr uint32_t kWidth = 3;
constexpr uint32_t kHeight = 2;
const char* kPixels = "101\n011\n";
// pixel characters past the declared 3x2 geometry; the reader must stop at the area
constexpr size_t kExcessPixelCount = 200000;

bool writeBitmap(const std::string& path)
{
  FILE* file = fopen(path.c_str(), "wb");
  if(!file)
    return false;
  bool written = fprintf(file, "P1\n%u %u\n%s", kWidth, kHeight, kPixels) > 0;
  for(size_t i = 0; written && i < kExcessPixelCount; ++i)
    written = fputc('0', file) != EOF;

  return fclose(file) == 0 && written;
}

// a P1 '1' is black, which the reader stores as 0
int32_t expectedSample(uint32_t y, uint32_t x)
{
  return kPixels[y * (kWidth + 1) + x] == '1' ? 0 : 1;
}

bool decodedSamplesMatch(const std::string& codestream)
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
  bool match = decoded->numcomps == 1 && comp->w == kWidth && comp->h == kHeight &&
               (int16Samples || comp->data_type == GRK_INT_32);
  if(!match)
    fprintf(stderr, "decoded %u comps, first %ux%u\n", decoded->numcomps, comp->w, comp->h);
  for(uint32_t y = 0; match && y < kHeight; ++y)
  {
    for(uint32_t x = 0; x < kWidth; ++x)
    {
      size_t index = (size_t)y * comp->stride + x;
      int32_t got = int16Samples ? ((const int16_t*)comp->data)[index]
                                 : ((const int32_t*)comp->data)[index];
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
  const std::string bitmap = "grk_pbm_ascii_excess.pbm";
  const std::string codestream = "grk_pbm_ascii_excess.j2k";
  if(!writeBitmap(bitmap))
  {
    fprintf(stderr, "%s: could not write\n", bitmap.c_str());
    return EXIT_FAILURE;
  }
  const char* argv[] = {"grk_compress", "-i", bitmap.c_str(), "-o", codestream.c_str()};
  bool compressed = grk_codec_compress(5, argv, nullptr, nullptr) == EXIT_SUCCESS;
  if(!compressed)
    fprintf(stderr, "%s: compress failed\n", bitmap.c_str());
  bool match = compressed && decodedSamplesMatch(codestream);
  remove(bitmap.c_str());
  remove(codestream.c_str());

  return match ? EXIT_SUCCESS : EXIT_FAILURE;
}
