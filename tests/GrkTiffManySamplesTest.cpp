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
#include <cstring>
#include <string>
#include <vector>

#include "grok.h"
#include "grok_codec.h"

namespace
{
constexpr uint16_t kWidth = 4;
// fewer than four rows in the strip, so the reader converts them one by one
constexpr uint16_t kHeight = 2;
constexpr uint16_t kSamplesPerPixel = 20;

uint8_t sampleValue(uint16_t component, uint32_t y, uint32_t x)
{
  return (uint8_t)(component * 13 + y * 4 + x * 3 + 7);
}

void putShort(std::vector<uint8_t>& out, uint16_t value)
{
  out.push_back((uint8_t)value);
  out.push_back((uint8_t)(value >> 8));
}

void putLong(std::vector<uint8_t>& out, uint32_t value)
{
  putShort(out, (uint16_t)value);
  putShort(out, (uint16_t)(value >> 16));
}

void putEntry(std::vector<uint8_t>& out, uint16_t tag, uint16_t type, uint32_t count,
              uint32_t value)
{
  constexpr uint16_t kTypeShort = 3;
  putShort(out, tag);
  putShort(out, type);
  putLong(out, count);
  if(type == kTypeShort && count == 1)
  {
    putShort(out, (uint16_t)value);
    putShort(out, 0);
  }
  else
  {
    putLong(out, value);
  }
}

std::vector<uint8_t> manySamplesTiff()
{
  constexpr uint16_t kShort = 3;
  constexpr uint16_t kLong = 4;
  constexpr uint16_t kEntryCount = 11;
  constexpr uint32_t kIfdOffset = 8;
  constexpr uint32_t kBitsPerSampleOffset = kIfdOffset + 2 + kEntryCount * 12 + 4;
  constexpr uint32_t kExtraSamplesOffset = kBitsPerSampleOffset + kSamplesPerPixel * 2;
  constexpr uint32_t kPixelOffset = kExtraSamplesOffset + (kSamplesPerPixel - 1) * 2;
  constexpr uint32_t kPixelBytes = (uint32_t)kWidth * kHeight * kSamplesPerPixel;

  std::vector<uint8_t> out = {'I', 'I'};
  putShort(out, 42);
  putLong(out, kIfdOffset);
  putShort(out, kEntryCount);
  putEntry(out, 256, kShort, 1, kWidth);
  putEntry(out, 257, kShort, 1, kHeight);
  putEntry(out, 258, kShort, kSamplesPerPixel, kBitsPerSampleOffset);
  putEntry(out, 259, kShort, 1, 1);
  putEntry(out, 262, kShort, 1, 1);
  putEntry(out, 273, kLong, 1, kPixelOffset);
  putEntry(out, 277, kShort, 1, kSamplesPerPixel);
  putEntry(out, 278, kShort, 1, kHeight);
  putEntry(out, 279, kLong, 1, kPixelBytes);
  putEntry(out, 284, kShort, 1, 1);
  putEntry(out, 338, kShort, kSamplesPerPixel - 1, kExtraSamplesOffset);
  putLong(out, 0);
  for(uint16_t i = 0; i < kSamplesPerPixel; ++i)
    putShort(out, 8);
  for(uint16_t i = 0; i + 1 < kSamplesPerPixel; ++i)
    putShort(out, 0);
  for(uint32_t y = 0; y < kHeight; ++y)
    for(uint32_t x = 0; x < kWidth; ++x)
      for(uint16_t c = 0; c < kSamplesPerPixel; ++c)
        out.push_back(sampleValue(c, y, x));

  return out;
}

bool writeFile(const std::string& path, const std::vector<uint8_t>& contents)
{
  FILE* file = fopen(path.c_str(), "wb");
  if(!file)
    return false;
  bool written = fwrite(contents.data(), 1, contents.size(), file) == contents.size();

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
  bool match = true;
  if(decoded->numcomps != kSamplesPerPixel)
  {
    fprintf(stderr, "decoded %u components, expected %u\n", decoded->numcomps, kSamplesPerPixel);
    match = false;
  }
  for(uint16_t c = 0; match && c < kSamplesPerPixel; ++c)
  {
    auto comp = decoded->comps + c;
    bool int16Samples = comp->data_type == GRK_INT_16;
    if(comp->w != kWidth || comp->h != kHeight || (!int16Samples && comp->data_type != GRK_INT_32))
    {
      fprintf(stderr, "component %u: %ux%u data type %d, expected %ux%u int16 or int32\n", c,
              comp->w, comp->h, (int)comp->data_type, kWidth, kHeight);
      match = false;
      break;
    }
    for(uint32_t y = 0; y < kHeight; ++y)
    {
      for(uint32_t x = 0; x < kWidth; ++x)
      {
        size_t index = (size_t)y * comp->stride + x;
        int32_t got = int16Samples ? ((const int16_t*)comp->data)[index]
                                   : ((const int32_t*)comp->data)[index];
        if(got != sampleValue(c, y, x))
        {
          fprintf(stderr, "component %u (%u,%u) is %d, expected %u\n", c, x, y, got,
                  sampleValue(c, y, x));
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
  const std::string tiff = "grk_tiff_many_samples.tif";
  const std::string codestream = "grk_tiff_many_samples.j2k";
  if(!writeFile(tiff, manySamplesTiff()))
  {
    fprintf(stderr, "%s: could not write\n", tiff.c_str());
    return EXIT_FAILURE;
  }
  const char* argv[] = {"grk_compress", "-i", tiff.c_str(), "-o", codestream.c_str()};
  bool compressed = grk_codec_compress(5, argv, nullptr, nullptr) == EXIT_SUCCESS;
  if(!compressed)
    fprintf(stderr, "%s: compress failed\n", tiff.c_str());
  bool match = compressed && decodedSamplesMatch(codestream);
  remove(tiff.c_str());
  remove(codestream.c_str());

  return match ? EXIT_SUCCESS : EXIT_FAILURE;
}
