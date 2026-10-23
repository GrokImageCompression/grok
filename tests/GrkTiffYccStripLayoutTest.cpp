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
constexpr uint16_t kPlanarContiguous = 1;
constexpr uint16_t kPlanarSeparate = 2;

struct YccLayout
{
  const char* name;
  uint32_t width;
  uint32_t height;
  uint32_t rowsPerStrip;
  uint16_t planarConfig;
  bool supported;
};

// every layout uses 2x2 chroma subsampling with 8 bit samples
const YccLayout kLayouts[] = {
    {"rows per strip a multiple of 2", 2, 6, 2, kPlanarContiguous, true},
    {"rows per strip not a multiple of 2", 2, 6, 3, kPlanarContiguous, false},
    {"separate planes", 8, 8, 8, kPlanarSeparate, false},
};

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

// strip sizes match libtiff, which ignores subsampling for separate planes
std::vector<uint8_t> yccTiff(const YccLayout& layout)
{
  constexpr uint16_t kShort = 3;
  constexpr uint16_t kLong = 4;
  constexpr uint16_t kEntryCount = 11;
  constexpr uint32_t kIfdOffset = 8;
  constexpr uint32_t kBitsPerSampleOffset = kIfdOffset + 2 + kEntryCount * 12 + 4;
  constexpr uint32_t kStripOffsetsOffset = kBitsPerSampleOffset + 3 * 2;
  bool separate = layout.planarConfig == kPlanarSeparate;
  uint32_t stripsPerPlane = (layout.height + layout.rowsPerStrip - 1) / layout.rowsPerStrip;
  uint32_t stripCount = separate ? 3 * stripsPerPlane : stripsPerPlane;
  uint32_t stripBytes = separate ? layout.width * layout.rowsPerStrip
                                 : ((layout.width + 1) / 2) * ((layout.rowsPerStrip + 1) / 2) * 6;
  uint32_t stripByteCountsOffset = kStripOffsetsOffset + stripCount * 4;
  uint32_t pixelOffset = stripByteCountsOffset + stripCount * 4;

  std::vector<uint8_t> out = {'I', 'I'};
  putShort(out, 42);
  putLong(out, kIfdOffset);
  putShort(out, kEntryCount);
  putEntry(out, 256, kShort, 1, layout.width);
  putEntry(out, 257, kShort, 1, layout.height);
  putEntry(out, 258, kShort, 3, kBitsPerSampleOffset);
  putEntry(out, 259, kShort, 1, 1);
  putEntry(out, 262, kShort, 1, 6);
  putEntry(out, 273, kLong, stripCount, kStripOffsetsOffset);
  putEntry(out, 277, kShort, 1, 3);
  putEntry(out, 278, kShort, 1, layout.rowsPerStrip);
  putEntry(out, 279, kLong, stripCount, stripByteCountsOffset);
  putEntry(out, 284, kShort, 1, layout.planarConfig);
  putEntry(out, 530, kShort, 2, 2U | (2U << 16));
  putLong(out, 0);
  for(int i = 0; i < 3; ++i)
    putShort(out, 8);
  for(uint32_t i = 0; i < stripCount; ++i)
    putLong(out, pixelOffset + i * stripBytes);
  for(uint32_t i = 0; i < stripCount; ++i)
    putLong(out, stripBytes);
  for(uint32_t i = 0; i < stripCount * stripBytes; ++i)
    out.push_back((uint8_t)(i * 7 + 16));

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
} // namespace

int main()
{
  const std::string tiff = "grk_tiff_ycc_strip_layout.tif";
  const std::string codestream = "grk_tiff_ycc_strip_layout.j2k";
  bool allMatch = true;
  for(const auto& layout : kLayouts)
  {
    if(!writeFile(tiff, yccTiff(layout)))
    {
      fprintf(stderr, "%s: could not write\n", tiff.c_str());
      return EXIT_FAILURE;
    }
    const char* argv[] = {"grk_compress", "-i", tiff.c_str(), "-o", codestream.c_str()};
    bool compressed = grk_codec_compress(5, argv, nullptr, nullptr) == EXIT_SUCCESS;
    if(compressed != layout.supported)
    {
      fprintf(stderr, "%s: compress %s, expected %s\n", layout.name,
              compressed ? "succeeded" : "failed", layout.supported ? "success" : "failure");
      allMatch = false;
    }
    remove(tiff.c_str());
    remove(codestream.c_str());
  }

  return allMatch ? EXIT_SUCCESS : EXIT_FAILURE;
}
