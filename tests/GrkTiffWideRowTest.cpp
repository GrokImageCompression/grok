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
// width times 16 bits per sample is 2^32, which wraps to zero in 32 bit arithmetic
constexpr uint32_t kWidth = 1U << 28;
constexpr uint32_t kRowBytes = kWidth * 2;
constexpr uint16_t kSampleFormatUnsigned = 1;
constexpr uint16_t kSampleFormatSigned = 2;

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

std::vector<uint8_t> wideRowHeader(uint16_t sampleFormat)
{
  constexpr uint16_t kShort = 3;
  constexpr uint16_t kLong = 4;
  constexpr uint16_t kEntryCount = 11;
  constexpr uint32_t kIfdOffset = 8;
  constexpr uint32_t kPixelOffset = kIfdOffset + 2 + kEntryCount * 12 + 4;

  std::vector<uint8_t> out = {'I', 'I'};
  putShort(out, 42);
  putLong(out, kIfdOffset);
  putShort(out, kEntryCount);
  putEntry(out, 256, kLong, kWidth);
  putEntry(out, 257, kShort, 1);
  putEntry(out, 258, kShort, 16);
  putEntry(out, 259, kShort, 1);
  putEntry(out, 262, kShort, 1);
  putEntry(out, 273, kLong, kPixelOffset);
  putEntry(out, 277, kShort, 1);
  putEntry(out, 278, kShort, 1);
  putEntry(out, 279, kLong, kRowBytes);
  putEntry(out, 284, kShort, 1);
  putEntry(out, 339, kShort, sampleFormat);
  putLong(out, 0);

  return out;
}

// seeking over the zero row keeps the file sparse where the filesystem supports it
bool writeWideRowTiff(const std::string& path, uint16_t sampleFormat)
{
  auto header = wideRowHeader(sampleFormat);
  FILE* file = fopen(path.c_str(), "wb");
  if(!file)
    return false;
  bool written = fwrite(header.data(), 1, header.size(), file) == header.size() &&
                 fseek(file, (long)(kRowBytes - 1), SEEK_CUR) == 0 && fputc(0, file) != EOF;

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
  const std::string tiff = "grk_tiff_wide_row.tif";
  bool allRead = true;
  for(uint16_t sampleFormat : {kSampleFormatUnsigned, kSampleFormatSigned})
  {
    if(!writeWideRowTiff(tiff, sampleFormat))
    {
      fprintf(stderr, "%s: could not write\n", tiff.c_str());
      return EXIT_FAILURE;
    }
    const char* argv[] = {"image_canon_dump", tiff.c_str(), nullDevice};
    if(grk_codec_image_canon_dump(3, argv) != 0)
    {
      fprintf(stderr, "sample format %u: read failed\n", sampleFormat);
      allRead = false;
    }
    remove(tiff.c_str());
  }

  return allRead ? EXIT_SUCCESS : EXIT_FAILURE;
}
