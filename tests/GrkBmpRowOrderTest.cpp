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

#include "grok.h"
#include "grok_codec.h"

namespace
{
constexpr uint32_t kWidth = 7;
constexpr uint32_t kHeight = 5;

struct BmpCase
{
  uint16_t bitCount;
  bool topDown;
};

uint16_t componentCount(uint16_t bitCount)
{
  return bitCount <= 8 ? 1 : 3;
}

uint32_t maxSample(uint16_t bitCount)
{
  if(bitCount <= 8)
    return (1u << bitCount) - 1;
  return bitCount == 16 ? 31 : 255;
}

uint32_t expectedSample(uint16_t bitCount, uint32_t x, uint32_t y, uint16_t c)
{
  return (x * 7 + y * 13 + y / 2 + c * 29) % (maxSample(bitCount) + 1);
}

void putLittleEndian(std::vector<uint8_t>& bytes, uint32_t value, int byteCount)
{
  for(int i = 0; i < byteCount; ++i)
    bytes.push_back((uint8_t)(value >> (8 * i)));
}

std::vector<uint8_t> packedRow(uint16_t bitCount, uint32_t y)
{
  uint32_t rowBytes = ((kWidth * bitCount + 31) / 32) * 4;
  std::vector<uint8_t> row(rowBytes, 0);
  for(uint32_t x = 0; x < kWidth; ++x)
  {
    uint32_t r = expectedSample(bitCount, x, y, 0);
    switch(bitCount)
    {
      case 1:
      case 4:
      case 8: {
        uint32_t bit = x * bitCount;
        row[bit / 8] |= (uint8_t)(r << (8 - bitCount - bit % 8));
        break;
      }
      case 16: {
        uint32_t value = (r << 10) | (expectedSample(bitCount, x, y, 1) << 5) |
                         expectedSample(bitCount, x, y, 2);
        row[2 * x] = (uint8_t)value;
        row[2 * x + 1] = (uint8_t)(value >> 8);
        break;
      }
      default: {
        uint32_t bytesPerPixel = bitCount / 8;
        // bmp stores blue, green, red
        row[bytesPerPixel * x] = (uint8_t)expectedSample(bitCount, x, y, 2);
        row[bytesPerPixel * x + 1] = (uint8_t)expectedSample(bitCount, x, y, 1);
        row[bytesPerPixel * x + 2] = (uint8_t)r;
        break;
      }
    }
  }
  return row;
}

bool writeBitmap(const BmpCase& bmp, const std::string& path)
{
  uint32_t paletteEntries = bmp.bitCount <= 8 ? 1u << bmp.bitCount : 0;
  uint32_t rowBytes = ((kWidth * bmp.bitCount + 31) / 32) * 4;
  uint32_t pixelOffset = 14 + 40 + paletteEntries * 4;
  std::vector<uint8_t> bytes;
  bytes.push_back('B');
  bytes.push_back('M');
  putLittleEndian(bytes, pixelOffset + rowBytes * kHeight, 4);
  putLittleEndian(bytes, 0, 4);
  putLittleEndian(bytes, pixelOffset, 4);
  putLittleEndian(bytes, 40, 4);
  putLittleEndian(bytes, kWidth, 4);
  putLittleEndian(bytes, bmp.topDown ? (uint32_t)-(int32_t)kHeight : kHeight, 4);
  putLittleEndian(bytes, 1, 2);
  putLittleEndian(bytes, bmp.bitCount, 2);
  putLittleEndian(bytes, 0, 4);
  putLittleEndian(bytes, rowBytes * kHeight, 4);
  putLittleEndian(bytes, 2835, 4);
  putLittleEndian(bytes, 2835, 4);
  putLittleEndian(bytes, paletteEntries, 4);
  putLittleEndian(bytes, 0, 4);
  for(uint32_t i = 0; i < paletteEntries; ++i)
  {
    uint32_t gray = i * 255 / (paletteEntries - 1);
    putLittleEndian(bytes, (gray << 16) | (gray << 8) | gray, 4);
  }
  for(uint32_t row = 0; row < kHeight; ++row)
  {
    uint32_t y = bmp.topDown ? row : kHeight - 1 - row;
    auto packed = packedRow(bmp.bitCount, y);
    bytes.insert(bytes.end(), packed.begin(), packed.end());
  }
  FILE* file = fopen(path.c_str(), "wb");
  if(!file)
    return false;
  bool written = fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();

  return fclose(file) == 0 && written;
}

bool decodedSamplesMatch(const BmpCase& bmp, const std::string& codestream)
{
  grk_decompress_parameters decompressParams = {};
  grk_stream_params stream = {};
  snprintf(stream.file, sizeof(stream.file), "%s", codestream.c_str());
  auto codec = grk_decompress_init(&stream, &decompressParams);
  grk_header_info header = {};
  if(!codec || !grk_decompress_read_header(codec, &header))
  {
    fprintf(stderr, "%u bpp: header read failed\n", bmp.bitCount);
    grk_object_unref(codec);
    return false;
  }
  grk_image* decoded = nullptr;
  if(grk_decompress(codec, nullptr))
    decoded = grk_decompress_get_image(codec);
  uint16_t numcomps = componentCount(bmp.bitCount);
  if(!decoded || decoded->numcomps != numcomps)
  {
    fprintf(stderr, "%u bpp: decode failed or wrong component count\n", bmp.bitCount);
    grk_object_unref(codec);
    return false;
  }
  bool match = true;
  for(uint16_t c = 0; match && c < numcomps; ++c)
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
        uint32_t expected = expectedSample(bmp.bitCount, x, y, c);
        if(got != (int32_t)expected)
        {
          fprintf(stderr, "%u bpp %s: component %u (%u,%u) is %d, expected %u\n", bmp.bitCount,
                  bmp.topDown ? "top down" : "bottom up", c, x, y, got, expected);
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
  const std::string bitmap = "grk_bmp_row_order.bmp";
  const std::string codestream = "grk_bmp_row_order.j2k";
  bool passed = true;
  for(uint16_t bitCount : {1, 4, 8, 16, 24, 32})
  {
    for(bool topDown : {false, true})
    {
      BmpCase bmp{bitCount, topDown};
      if(!writeBitmap(bmp, bitmap))
      {
        fprintf(stderr, "%u bpp: could not write\n", bitCount);
        return EXIT_FAILURE;
      }
      const char* argv[] = {"grk_compress", "-i", bitmap.c_str(), "-o", codestream.c_str()};
      if(grk_codec_compress(5, argv, nullptr, nullptr) != EXIT_SUCCESS)
      {
        fprintf(stderr, "%u bpp: compress failed\n", bitCount);
        passed = false;
        continue;
      }
      passed = decodedSamplesMatch(bmp, codestream) && passed;
    }
  }
  remove(bitmap.c_str());
  remove(codestream.c_str());

  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
