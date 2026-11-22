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
constexpr uint32_t kWidth = 37;
// grk_compress reads BMP rows 64 at a time
constexpr uint32_t kHeight = 150;
constexpr uint32_t kUncompressed = 0;
constexpr uint32_t kRle8 = 1;
constexpr uint32_t kRle4 = 2;
constexpr uint32_t kBitfields = 3;

struct BmpCase
{
  const char* name;
  uint16_t bitCount;
  uint32_t compression;
  bool topDown;
  uint16_t numcomps;
  uint8_t precisions[4];
  uint8_t shifts[4];
};

uint32_t expectedSample(const BmpCase& bmp, uint32_t x, uint32_t y, uint16_t c)
{
  return (x * 7919u + y * 104729u + x * y + c * 29u) % (1u << bmp.precisions[c]);
}

void putLittleEndian(std::vector<uint8_t>& bytes, uint32_t value, int byteCount)
{
  for(int i = 0; i < byteCount; ++i)
    bytes.push_back((uint8_t)(value >> (8 * i)));
}

std::vector<uint8_t> packedRow(const BmpCase& bmp, uint32_t y)
{
  uint32_t rowBytes = ((kWidth * bmp.bitCount + 31) / 32) * 4;
  std::vector<uint8_t> row(rowBytes, 0);
  uint32_t bytesPerPixel = bmp.bitCount / 8;
  for(uint32_t x = 0; x < kWidth; ++x)
  {
    if(bmp.bitCount <= 8)
    {
      uint32_t bit = x * bmp.bitCount;
      row[bit / 8] |= (uint8_t)(expectedSample(bmp, x, y, 0) << (8 - bmp.bitCount - bit % 8));
    }
    else if(bmp.bitCount == 24)
    {
      // bmp stores blue, green, red
      row[3 * x] = (uint8_t)expectedSample(bmp, x, y, 2);
      row[3 * x + 1] = (uint8_t)expectedSample(bmp, x, y, 1);
      row[3 * x + 2] = (uint8_t)expectedSample(bmp, x, y, 0);
    }
    else
    {
      uint32_t value = 0;
      for(uint16_t c = 0; c < bmp.numcomps; ++c)
        value |= expectedSample(bmp, x, y, c) << bmp.shifts[c];
      for(uint32_t i = 0; i < bytesPerPixel; ++i)
        row[bytesPerPixel * x + i] = (uint8_t)(value >> (8 * i));
    }
  }
  return row;
}

std::vector<uint8_t> runLengthRow(const BmpCase& bmp, uint32_t y)
{
  std::vector<uint8_t> row;
  for(uint32_t x = 0; x < kWidth; ++x)
  {
    auto index = (uint8_t)expectedSample(bmp, x, y, 0);
    row.push_back(1);
    row.push_back(bmp.compression == kRle4 ? (uint8_t)(index << 4) : index);
  }
  // end of line
  row.push_back(0);
  row.push_back(0);
  return row;
}

std::vector<uint8_t> pixelBytes(const BmpCase& bmp)
{
  std::vector<uint8_t> pixels;
  for(uint32_t fileRow = 0; fileRow < kHeight; ++fileRow)
  {
    uint32_t y = bmp.topDown ? fileRow : kHeight - 1 - fileRow;
    bool runLengthEncoded = bmp.compression == kRle8 || bmp.compression == kRle4;
    auto row = runLengthEncoded ? runLengthRow(bmp, y) : packedRow(bmp, y);
    pixels.insert(pixels.end(), row.begin(), row.end());
  }
  return pixels;
}

bool writeBitmap(const BmpCase& bmp, const std::string& path, size_t droppedBytes)
{
  bool bitfields = bmp.compression == kBitfields;
  uint32_t infoHeaderSize = bitfields ? 56 : 40;
  uint32_t paletteEntries = bmp.bitCount <= 8 ? 1u << bmp.bitCount : 0;
  uint32_t pixelOffset = 14 + infoHeaderSize + paletteEntries * 4;
  auto pixels = pixelBytes(bmp);
  std::vector<uint8_t> bytes;
  bytes.push_back('B');
  bytes.push_back('M');
  putLittleEndian(bytes, pixelOffset + (uint32_t)pixels.size(), 4);
  putLittleEndian(bytes, 0, 4);
  putLittleEndian(bytes, pixelOffset, 4);
  putLittleEndian(bytes, infoHeaderSize, 4);
  putLittleEndian(bytes, kWidth, 4);
  putLittleEndian(bytes, bmp.topDown ? (uint32_t)-(int32_t)kHeight : kHeight, 4);
  putLittleEndian(bytes, 1, 2);
  putLittleEndian(bytes, bmp.bitCount, 2);
  putLittleEndian(bytes, bmp.compression, 4);
  putLittleEndian(bytes, (uint32_t)pixels.size(), 4);
  putLittleEndian(bytes, 2835, 4);
  putLittleEndian(bytes, 2835, 4);
  putLittleEndian(bytes, paletteEntries, 4);
  putLittleEndian(bytes, 0, 4);
  if(bitfields)
  {
    for(uint16_t c = 0; c < 4; ++c)
    {
      uint32_t mask = c < bmp.numcomps ? ((1u << bmp.precisions[c]) - 1) << bmp.shifts[c] : 0;
      putLittleEndian(bytes, mask, 4);
    }
  }
  for(uint32_t i = 0; i < paletteEntries; ++i)
  {
    uint32_t gray = i * 255 / (paletteEntries - 1);
    putLittleEndian(bytes, (gray << 16) | (gray << 8) | gray, 4);
  }
  bytes.insert(bytes.end(), pixels.begin(), pixels.end());
  bytes.resize(bytes.size() - droppedBytes);
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
    fprintf(stderr, "%s: header read failed\n", bmp.name);
    grk_object_unref(codec);
    return false;
  }
  grk_image* decoded = nullptr;
  if(grk_decompress(codec, nullptr))
    decoded = grk_decompress_get_image(codec);
  if(!decoded || decoded->numcomps != bmp.numcomps)
  {
    fprintf(stderr, "%s: decode failed or wrong component count\n", bmp.name);
    grk_object_unref(codec);
    return false;
  }
  bool match = true;
  for(uint16_t c = 0; match && c < bmp.numcomps; ++c)
  {
    auto comp = decoded->comps + c;
    bool int16Samples = comp->data_type == GRK_INT_16;
    match = comp->prec == bmp.precisions[c];
    if(!match)
      fprintf(stderr, "%s: component %u precision %u, expected %u\n", bmp.name, c, comp->prec,
              bmp.precisions[c]);
    for(uint32_t y = 0; match && y < kHeight; ++y)
    {
      for(uint32_t x = 0; match && x < kWidth; ++x)
      {
        size_t index = (size_t)y * comp->stride + x;
        int32_t got = int16Samples ? ((const int16_t*)comp->data)[index]
                                   : ((const int32_t*)comp->data)[index];
        uint32_t expected = expectedSample(bmp, x, y, c);
        if(got != (int32_t)expected)
        {
          fprintf(stderr, "%s: component %u (%u,%u) is %d, expected %u\n", bmp.name, c, x, y, got,
                  expected);
          match = false;
        }
      }
    }
  }
  grk_object_unref(codec);

  return match;
}

bool compress(const std::string& bitmap, const std::string& codestream)
{
  const char* argv[] = {"grk_compress", "-i", bitmap.c_str(), "-o", codestream.c_str()};
  return grk_codec_compress(5, argv, nullptr, nullptr) == EXIT_SUCCESS;
}
} // namespace

int main()
{
  std::vector<BmpCase> cases;
  for(bool topDown : {false, true})
  {
    cases.push_back({"1 bpp", 1, kUncompressed, topDown, 1, {1}, {}});
    cases.push_back({"4 bpp", 4, kUncompressed, topDown, 1, {4}, {}});
    cases.push_back({"8 bpp", 8, kUncompressed, topDown, 1, {8}, {}});
    cases.push_back({"16 bpp", 16, kUncompressed, topDown, 3, {5, 5, 5}, {10, 5, 0}});
    cases.push_back({"24 bpp", 24, kUncompressed, topDown, 3, {8, 8, 8}, {}});
    cases.push_back({"32 bpp", 32, kUncompressed, topDown, 3, {8, 8, 8}, {16, 8, 0}});
    cases.push_back({"16 bpp 565 bitfields", 16, kBitfields, topDown, 3, {5, 6, 5}, {11, 5, 0}});
    cases.push_back({"32 bpp 10-10-10-2 bitfields",
                     32,
                     kBitfields,
                     topDown,
                     4,
                     {10, 10, 10, 2},
                     {20, 10, 0, 30}});
  }
  cases.push_back({"rle8", 8, kRle8, false, 1, {8}, {}});
  cases.push_back({"rle4", 4, kRle4, false, 1, {4}, {}});

  const std::string bitmap = "grk_bmp_row_order.bmp";
  const std::string codestream = "grk_bmp_row_order.j2k";
  bool passed = true;
  for(const auto& bmp : cases)
  {
    if(!writeBitmap(bmp, bitmap, 0))
    {
      fprintf(stderr, "%s: could not write\n", bmp.name);
      return EXIT_FAILURE;
    }
    if(!compress(bitmap, codestream))
    {
      fprintf(stderr, "%s %s: compress failed\n", bmp.name, bmp.topDown ? "top down" : "bottom up");
      passed = false;
      continue;
    }
    if(!decodedSamplesMatch(bmp, codestream))
    {
      fprintf(stderr, "%s %s: samples differ\n", bmp.name, bmp.topDown ? "top down" : "bottom up");
      passed = false;
    }
  }
  const BmpCase truncated = {"truncated", 24, kUncompressed, false, 3, {8, 8, 8}, {}};
  if(!writeBitmap(truncated, bitmap, 100))
  {
    fprintf(stderr, "%s: could not write\n", truncated.name);
    return EXIT_FAILURE;
  }
  if(compress(bitmap, codestream))
  {
    fprintf(stderr, "truncated file compressed\n");
    passed = false;
  }
  remove(bitmap.c_str());
  remove(codestream.c_str());

  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
