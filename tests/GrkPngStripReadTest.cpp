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

#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <png.h>

#include "grok.h"
#include "grok_codec.h"

namespace
{
constexpr uint32_t kWidth = 37;
// grk_compress reads PNG rows 64 at a time
constexpr uint32_t kHeight = 150;
constexpr uint32_t kPaletteEntries = 16;

struct PngCase
{
  const char* name;
  int colorType;
  int bitDepth;
  bool interlaced;
};

uint16_t pngComponents(const PngCase& png)
{
  switch(png.colorType)
  {
    case PNG_COLOR_TYPE_GRAY:
    case PNG_COLOR_TYPE_PALETTE:
      return 1;
    case PNG_COLOR_TYPE_GRAY_ALPHA:
      return 2;
    case PNG_COLOR_TYPE_RGB:
      return 3;
    default:
      return 4;
  }
}

uint32_t storedSample(const PngCase& png, uint16_t compno, uint32_t x, uint32_t y)
{
  uint32_t range = 1u << png.bitDepth;
  if(png.colorType == PNG_COLOR_TYPE_PALETTE)
    range = kPaletteEntries;
  return (x * 7919u + y * 104729u + x * y + compno * 15485863u) % range;
}

uint8_t paletteValue(uint32_t index, uint16_t compno)
{
  return (uint8_t)(index * 37u + compno * 101u);
}

int32_t expectedSample(const PngCase& png, uint16_t compno, uint32_t x, uint32_t y)
{
  if(png.colorType == PNG_COLOR_TYPE_PALETTE)
    return paletteValue(storedSample(png, 0, x, y), compno);
  return (int32_t)storedSample(png, compno, x, y);
}

uint16_t decodedComponents(const PngCase& png)
{
  return png.colorType == PNG_COLOR_TYPE_PALETTE ? 3 : pngComponents(png);
}

uint8_t decodedPrecision(const PngCase& png)
{
  return png.colorType == PNG_COLOR_TYPE_PALETTE ? 8 : (uint8_t)png.bitDepth;
}

std::vector<uint8_t> packRow(const PngCase& png, uint32_t y)
{
  uint16_t numcomps = pngComponents(png);
  std::vector<uint8_t> row((kWidth * numcomps * png.bitDepth + 7) / 8);
  uint32_t bit = 0;
  for(uint32_t x = 0; x < kWidth; ++x)
  {
    for(uint16_t compno = 0; compno < numcomps; ++compno)
    {
      uint32_t value = storedSample(png, compno, x, y);
      if(png.bitDepth == 16)
      {
        row[bit / 8] = (uint8_t)(value >> 8);
        row[bit / 8 + 1] = (uint8_t)value;
      }
      else
      {
        row[bit / 8] |= (uint8_t)(value << (8 - png.bitDepth - bit % 8));
      }
      bit += (uint32_t)png.bitDepth;
    }
  }
  return row;
}

bool writePngRows(png_structp writer, png_infop info, const PngCase& png, FILE* file,
                  png_bytep* rowPointers)
{
  if(setjmp(png_jmpbuf(writer)))
    return false;
  png_init_io(writer, file);
  png_set_IHDR(writer, info, kWidth, kHeight, png.bitDepth, png.colorType,
               png.interlaced ? PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_color palette[kPaletteEntries];
  for(uint32_t i = 0; i < kPaletteEntries; ++i)
    palette[i] = {paletteValue(i, 0), paletteValue(i, 1), paletteValue(i, 2)};
  if(png.colorType == PNG_COLOR_TYPE_PALETTE)
    png_set_PLTE(writer, info, palette, kPaletteEntries);
  png_write_info(writer, info);
  png_write_image(writer, rowPointers);
  png_write_end(writer, nullptr);
  return true;
}

bool writePng(const PngCase& png, const std::string& path)
{
  std::vector<std::vector<uint8_t>> rows;
  std::vector<png_bytep> rowPointers;
  for(uint32_t y = 0; y < kHeight; ++y)
    rows.push_back(packRow(png, y));
  for(auto& row : rows)
    rowPointers.push_back(row.data());
  FILE* file = fopen(path.c_str(), "wb");
  if(!file)
    return false;
  auto writer = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  auto info = writer ? png_create_info_struct(writer) : nullptr;
  bool written = info && writePngRows(writer, info, png, file, rowPointers.data());
  png_destroy_write_struct(&writer, &info);

  return fclose(file) == 0 && written;
}

bool truncateFile(const std::string& path, long droppedBytes)
{
  FILE* file = fopen(path.c_str(), "rb");
  if(!file)
    return false;
  std::vector<uint8_t> bytes(1 << 20);
  bytes.resize(fread(bytes.data(), 1, bytes.size(), file));
  fclose(file);
  bytes.resize(bytes.size() - (size_t)droppedBytes);
  file = fopen(path.c_str(), "wb");
  if(!file)
    return false;
  bool written = fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();

  return fclose(file) == 0 && written;
}

bool decodedSamplesMatch(const PngCase& png, const std::string& codestream)
{
  grk_decompress_parameters decompressParams = {};
  grk_stream_params stream = {};
  snprintf(stream.file, sizeof(stream.file), "%s", codestream.c_str());
  auto codec = grk_decompress_init(&stream, &decompressParams);
  grk_header_info header = {};
  if(!codec || !grk_decompress_read_header(codec, &header))
  {
    fprintf(stderr, "%s: header read failed\n", png.name);
    grk_object_unref(codec);
    return false;
  }
  grk_image* decoded = nullptr;
  if(grk_decompress(codec, nullptr))
    decoded = grk_decompress_get_image(codec);
  if(!decoded || decoded->numcomps != decodedComponents(png))
  {
    fprintf(stderr, "%s: decode failed or wrong component count\n", png.name);
    grk_object_unref(codec);
    return false;
  }
  bool match = true;
  for(uint16_t compno = 0; match && compno < decoded->numcomps; ++compno)
  {
    auto comp = decoded->comps + compno;
    bool int16Samples = comp->data_type == GRK_INT_16;
    match = !comp->sgnd && comp->prec == decodedPrecision(png);
    if(!match)
      fprintf(stderr, "%s: component %u decoded sgnd %d precision %u\n", png.name, compno,
              comp->sgnd, comp->prec);
    for(uint32_t y = 0; match && y < kHeight; ++y)
    {
      for(uint32_t x = 0; match && x < kWidth; ++x)
      {
        size_t index = (size_t)y * comp->stride + x;
        int32_t got = int16Samples ? ((const int16_t*)comp->data)[index]
                                   : ((const int32_t*)comp->data)[index];
        if(got != expectedSample(png, compno, x, y))
        {
          fprintf(stderr, "%s: component %u (%u,%u) is %d, expected %d\n", png.name, compno, x, y,
                  got, expectedSample(png, compno, x, y));
          match = false;
        }
      }
    }
  }
  grk_object_unref(codec);

  return match;
}

bool compress(const std::string& image, const std::string& codestream)
{
  const char* argv[] = {"grk_compress", "-i", image.c_str(), "-o", codestream.c_str()};
  return grk_codec_compress(5, argv, nullptr, nullptr) == EXIT_SUCCESS;
}
} // namespace

int main()
{
  const PngCase cases[] = {
      {"gray 1 bit", PNG_COLOR_TYPE_GRAY, 1, false},
      {"gray 2 bit", PNG_COLOR_TYPE_GRAY, 2, false},
      {"gray 4 bit interlaced", PNG_COLOR_TYPE_GRAY, 4, true},
      {"gray 8 bit", PNG_COLOR_TYPE_GRAY, 8, false},
      {"gray 16 bit interlaced", PNG_COLOR_TYPE_GRAY, 16, true},
      {"gray alpha 8 bit", PNG_COLOR_TYPE_GRAY_ALPHA, 8, false},
      {"rgb 8 bit", PNG_COLOR_TYPE_RGB, 8, false},
      {"rgb 8 bit interlaced", PNG_COLOR_TYPE_RGB, 8, true},
      {"rgba 16 bit", PNG_COLOR_TYPE_RGB_ALPHA, 16, false},
      {"palette 4 bit", PNG_COLOR_TYPE_PALETTE, 4, false},
  };
  const std::string image = "grk_png_strip_read.png";
  const std::string codestream = "grk_png_strip_read.j2k";
  bool passed = true;
  for(const auto& png : cases)
  {
    if(!writePng(png, image))
    {
      fprintf(stderr, "%s: could not write\n", png.name);
      return EXIT_FAILURE;
    }
    if(!compress(image, codestream))
    {
      fprintf(stderr, "%s: compress failed\n", png.name);
      passed = false;
      continue;
    }
    passed = decodedSamplesMatch(png, codestream) && passed;
  }
  const PngCase truncated = {"truncated", PNG_COLOR_TYPE_RGB, 8, false};
  if(!writePng(truncated, image) || !truncateFile(image, 200))
  {
    fprintf(stderr, "%s: could not write\n", truncated.name);
    return EXIT_FAILURE;
  }
  if(compress(image, codestream))
  {
    fprintf(stderr, "truncated file compressed\n");
    passed = false;
  }
  remove(image.c_str());
  remove(codestream.c_str());

  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
