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
// without the width bound a 33 sample row unpacks to 64 samples, past the 48 sample stride
constexpr uint32_t kWidth = 33;
constexpr uint32_t kHeight = 3;
constexpr uint32_t kRowBytes = ((kWidth + 31) / 32) * 4;
constexpr uint32_t kPaletteBytes = 2 * 4;
constexpr uint32_t kPixelOffset = 14 + 40 + kPaletteBytes;

int32_t expectedSample(uint32_t y, uint32_t x)
{
  return (x + y) % 3 == 0 ? 1 : 0;
}

void putLittleEndian(std::vector<uint8_t>& bytes, uint32_t value, int byteCount)
{
  for(int i = 0; i < byteCount; ++i)
    bytes.push_back((uint8_t)(value >> (8 * i)));
}

bool writeBitmap(const std::string& path)
{
  std::vector<uint8_t> bytes;
  bytes.push_back('B');
  bytes.push_back('M');
  putLittleEndian(bytes, kPixelOffset + kRowBytes * kHeight, 4);
  putLittleEndian(bytes, 0, 4);
  putLittleEndian(bytes, kPixelOffset, 4);
  putLittleEndian(bytes, 40, 4);
  putLittleEndian(bytes, kWidth, 4);
  putLittleEndian(bytes, kHeight, 4);
  putLittleEndian(bytes, 1, 2);
  putLittleEndian(bytes, 1, 2);
  putLittleEndian(bytes, 0, 4);
  putLittleEndian(bytes, kRowBytes * kHeight, 4);
  putLittleEndian(bytes, 2835, 4);
  putLittleEndian(bytes, 2835, 4);
  putLittleEndian(bytes, 2, 4);
  putLittleEndian(bytes, 0, 4);
  // black then white, as blue green red reserved
  putLittleEndian(bytes, 0x00000000, 4);
  putLittleEndian(bytes, 0x00FFFFFF, 4);
  // bmp rows are stored bottom up
  for(uint32_t row = 0; row < kHeight; ++row)
  {
    uint32_t y = kHeight - 1 - row;
    std::vector<uint8_t> packed(kRowBytes, 0);
    for(uint32_t x = 0; x < kWidth; ++x)
    {
      if(expectedSample(y, x))
        packed[x / 8] |= (uint8_t)(0x80 >> (x % 8));
    }
    bytes.insert(bytes.end(), packed.begin(), packed.end());
  }
  FILE* file = fopen(path.c_str(), "wb");
  if(!file)
    return false;
  bool written = fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();

  return fclose(file) == 0 && written;
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
  const std::string bitmap = "grk_bmp_1bit_row_overrun.bmp";
  const std::string codestream = "grk_bmp_1bit_row_overrun.j2k";
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
