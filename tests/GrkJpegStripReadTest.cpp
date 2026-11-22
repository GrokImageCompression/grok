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
#include <vector>

#include <jpeglib.h>

#include "grok.h"
#include "grok_codec.h"

namespace
{
constexpr uint32_t kWidth = 37;
// grk_compress reads JPEG scanlines 64 rows at a time
constexpr uint32_t kHeight = 150;

struct JpegCase
{
  const char* name;
  int numcomps;
  bool progressive;
};

bool writeJpeg(const JpegCase& jpeg, const std::string& path)
{
  FILE* file = fopen(path.c_str(), "wb");
  if(!file)
    return false;
  jpeg_compress_struct compress = {};
  jpeg_error_mgr errors = {};
  compress.err = jpeg_std_error(&errors);
  jpeg_create_compress(&compress);
  jpeg_stdio_dest(&compress, file);
  compress.image_width = kWidth;
  compress.image_height = kHeight;
  compress.input_components = jpeg.numcomps;
  compress.in_color_space = jpeg.numcomps == 3 ? JCS_RGB : JCS_GRAYSCALE;
  jpeg_set_defaults(&compress);
  jpeg_set_quality(&compress, 90, TRUE);
  if(jpeg.progressive)
    jpeg_simple_progression(&compress);
  jpeg_start_compress(&compress, TRUE);
  std::vector<JSAMPLE> row((size_t)kWidth * jpeg.numcomps);
  while(compress.next_scanline < kHeight)
  {
    uint32_t y = compress.next_scanline;
    for(uint32_t x = 0; x < kWidth; ++x)
      for(int c = 0; c < jpeg.numcomps; ++c)
        row[(size_t)x * jpeg.numcomps + c] = (JSAMPLE)((x * 7 + y * 13 + c * 29) % 256);
    JSAMPROW rows[] = {row.data()};
    jpeg_write_scanlines(&compress, rows, 1);
  }
  jpeg_finish_compress(&compress);
  jpeg_destroy_compress(&compress);

  return fclose(file) == 0;
}

std::vector<JSAMPLE> decodeWithLibjpeg(const std::string& path)
{
  FILE* file = fopen(path.c_str(), "rb");
  if(!file)
    return {};
  jpeg_decompress_struct decompress = {};
  jpeg_error_mgr errors = {};
  decompress.err = jpeg_std_error(&errors);
  jpeg_create_decompress(&decompress);
  jpeg_stdio_src(&decompress, file);
  jpeg_read_header(&decompress, TRUE);
  jpeg_start_decompress(&decompress);
  size_t rowSamples = (size_t)decompress.output_width * decompress.output_components;
  std::vector<JSAMPLE> samples(rowSamples * decompress.output_height);
  while(decompress.output_scanline < decompress.output_height)
  {
    JSAMPROW rows[] = {samples.data() + decompress.output_scanline * rowSamples};
    jpeg_read_scanlines(&decompress, rows, 1);
  }
  jpeg_finish_decompress(&decompress);
  jpeg_destroy_decompress(&decompress);
  fclose(file);

  return samples;
}

bool decodedSamplesMatch(const JpegCase& jpeg, const std::vector<JSAMPLE>& expected,
                         const std::string& codestream)
{
  grk_decompress_parameters decompressParams = {};
  grk_stream_params stream = {};
  snprintf(stream.file, sizeof(stream.file), "%s", codestream.c_str());
  auto codec = grk_decompress_init(&stream, &decompressParams);
  grk_header_info header = {};
  if(!codec || !grk_decompress_read_header(codec, &header))
  {
    fprintf(stderr, "%s: header read failed\n", jpeg.name);
    grk_object_unref(codec);
    return false;
  }
  grk_image* decoded = nullptr;
  if(grk_decompress(codec, nullptr))
    decoded = grk_decompress_get_image(codec);
  if(!decoded || decoded->numcomps != (uint16_t)jpeg.numcomps)
  {
    fprintf(stderr, "%s: decode failed or wrong component count\n", jpeg.name);
    grk_object_unref(codec);
    return false;
  }
  bool match = true;
  for(uint16_t c = 0; match && c < jpeg.numcomps; ++c)
  {
    auto comp = decoded->comps + c;
    bool int16Samples = comp->data_type == GRK_INT_16;
    for(uint32_t y = 0; match && y < kHeight; ++y)
    {
      for(uint32_t x = 0; match && x < kWidth; ++x)
      {
        size_t index = (size_t)y * comp->stride + x;
        int32_t got = int16Samples ? ((const int16_t*)comp->data)[index]
                                   : ((const int32_t*)comp->data)[index];
        int32_t want = expected[((size_t)y * kWidth + x) * jpeg.numcomps + c];
        if(got != want)
        {
          fprintf(stderr, "%s: component %u (%u,%u) is %d, expected %d\n", jpeg.name, c, x, y, got,
                  want);
          match = false;
        }
      }
    }
  }
  grk_object_unref(codec);

  return match;
}
} // namespace

int main()
{
  const JpegCase cases[] = {
      {"gray baseline", 1, false},
      {"rgb baseline", 3, false},
      {"rgb progressive", 3, true},
  };
  const std::string image = "grk_jpeg_strip_read.jpg";
  const std::string codestream = "grk_jpeg_strip_read.j2k";
  bool passed = true;
  for(const auto& jpeg : cases)
  {
    if(!writeJpeg(jpeg, image))
    {
      fprintf(stderr, "%s: could not write\n", jpeg.name);
      return EXIT_FAILURE;
    }
    auto expected = decodeWithLibjpeg(image);
    const char* argv[] = {"grk_compress", "-i", image.c_str(), "-o", codestream.c_str()};
    if(expected.empty() || grk_codec_compress(5, argv, nullptr, nullptr) != EXIT_SUCCESS)
    {
      fprintf(stderr, "%s: libjpeg decode or compress failed\n", jpeg.name);
      passed = false;
      continue;
    }
    passed = decodedSamplesMatch(jpeg, expected, codestream) && passed;
  }
  remove(image.c_str());
  remove(codestream.c_str());

  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
