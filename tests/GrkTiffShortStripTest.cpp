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

#include "grok_codec.h"

namespace
{
constexpr uint32_t kWidth = 16;
constexpr uint32_t kHeight = 16;
constexpr uint32_t kStripBytes = kWidth * kHeight;

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

void putEntry(std::vector<uint8_t>& out, uint16_t tag, uint16_t type, uint32_t value)
{
  constexpr uint16_t kTypeShort = 3;
  putShort(out, tag);
  putShort(out, type);
  putLong(out, 1);
  if(type == kTypeShort)
  {
    putShort(out, (uint16_t)value);
    putShort(out, 0);
  }
  else
  {
    putLong(out, value);
  }
}

// one uncompressed 8 bit grey strip whose byte count and stored bytes can disagree with its rows
bool writeStripTiff(const std::string& path, uint32_t declaredBytes, uint32_t storedBytes)
{
  constexpr uint16_t kShort = 3;
  constexpr uint16_t kLong = 4;
  constexpr uint16_t kEntryCount = 9;
  constexpr uint32_t kIfdOffset = 8;
  constexpr uint32_t kPixelOffset = kIfdOffset + 2 + kEntryCount * 12 + 4;

  std::vector<uint8_t> out = {'I', 'I'};
  putShort(out, 42);
  putLong(out, kIfdOffset);
  putShort(out, kEntryCount);
  putEntry(out, 256, kShort, kWidth);
  putEntry(out, 257, kShort, kHeight);
  putEntry(out, 258, kShort, 8);
  putEntry(out, 259, kShort, 1);
  putEntry(out, 262, kShort, 1);
  putEntry(out, 273, kLong, kPixelOffset);
  putEntry(out, 277, kShort, 1);
  putEntry(out, 278, kShort, kHeight);
  putEntry(out, 279, kLong, declaredBytes);
  putLong(out, 0);
  out.resize(out.size() + storedBytes, 0x80);

  FILE* file = fopen(path.c_str(), "wb");
  if(!file)
    return false;
  bool written = fwrite(out.data(), 1, out.size(), file) == out.size();

  return fclose(file) == 0 && written;
}
} // namespace

int main()
{
#ifdef _WIN32
  const char* nullDevice = "NUL";
#else
  const char* nullDevice = "/dev/null";
#endif
  struct StripLayout
  {
    const char* name;
    uint32_t declaredBytes;
    uint32_t storedBytes;
  };
  const StripLayout layouts[] = {
      {"byte count shorter than the strip rows", kStripBytes / 2, kStripBytes / 2},
      {"byte count past the end of the file", kStripBytes, kStripBytes / 2},
  };
  const std::string tiff = "grk_tiff_short_strip.tif";
  bool allRejected = true;
  for(const auto& layout : layouts)
  {
    if(!writeStripTiff(tiff, layout.declaredBytes, layout.storedBytes))
    {
      fprintf(stderr, "%s: could not write\n", tiff.c_str());
      return EXIT_FAILURE;
    }
    const char* argv[] = {"image_canon_dump", tiff.c_str(), nullDevice};
    if(grk_codec_image_canon_dump(3, argv) == 0)
    {
      fprintf(stderr, "%s: read succeeded\n", layout.name);
      allRejected = false;
    }
    remove(tiff.c_str());
  }

  return allRejected ? EXIT_SUCCESS : EXIT_FAILURE;
}
