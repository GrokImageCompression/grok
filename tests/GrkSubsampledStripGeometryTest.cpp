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

// A subsampled ycbcr strip holds whole clump rows, so a strip height that is not a
// multiple of the vertical subsampling makes the writer overrun its strip buffer.
// Check the written file: every strip must carry exactly the clump rows it claims.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "grok_codec.h"

namespace
{
const uint16_t TAG_IMAGE_LENGTH = 257;
const uint16_t TAG_STRIP_BYTE_COUNTS = 279;
const uint16_t TAG_ROWS_PER_STRIP = 278;
const uint16_t TAG_YCBCR_SUBSAMPLING = 530;

bool readWholeFile(const std::string& path, std::vector<uint8_t>& contents)
{
  FILE* file = fopen(path.c_str(), "rb");
  if(!file)
    return false;
  contents.clear();
  uint8_t chunk[65536];
  size_t got = 0;
  while((got = fread(chunk, 1, sizeof(chunk), file)) > 0)
    contents.insert(contents.end(), chunk, chunk + got);
  fclose(file);

  return true;
}

std::string outputNameFor(const std::string& input, const std::string& suffix)
{
  auto slash = input.find_last_of("/\\");
  std::string base = slash == std::string::npos ? input : input.substr(slash + 1);
  auto dot = base.find_last_of('.');
  if(dot != std::string::npos)
    base.erase(dot);

  return base + suffix;
}

int decompressToTiff(const std::string& input, const std::string& output)
{
  const char* argv[] = {"grk_decompress", "-i", input.c_str(), "-o", output.c_str()};
  return grk_codec_decompress(5, argv);
}

struct TiffReader
{
  const std::vector<uint8_t>& bytes;
  bool littleEndian = true;

  uint16_t u16(size_t at) const
  {
    if(at + 2 > bytes.size())
      return 0;
    return littleEndian ? (uint16_t)(bytes[at] | (bytes[at + 1] << 8))
                        : (uint16_t)((bytes[at] << 8) | bytes[at + 1]);
  }
  uint32_t u32(size_t at) const
  {
    if(at + 4 > bytes.size())
      return 0;
    return littleEndian ? (uint32_t)bytes[at] | ((uint32_t)bytes[at + 1] << 8) |
                              ((uint32_t)bytes[at + 2] << 16) | ((uint32_t)bytes[at + 3] << 24)
                        : ((uint32_t)bytes[at] << 24) | ((uint32_t)bytes[at + 1] << 16) |
                              ((uint32_t)bytes[at + 2] << 8) | (uint32_t)bytes[at + 3];
  }
};

// classic tiff only, which is what the writer emits for these images
bool readTagValues(const TiffReader& tiff, size_t ifdOffset, uint16_t tag,
                   std::vector<uint64_t>& values)
{
  uint16_t numEntries = tiff.u16(ifdOffset);
  for(uint16_t i = 0; i < numEntries; ++i)
  {
    size_t entry = ifdOffset + 2 + (size_t)i * 12;
    if(tiff.u16(entry) != tag)
      continue;
    uint16_t type = tiff.u16(entry + 2);
    uint32_t count = tiff.u32(entry + 4);
    size_t elementSize = (type == 3) ? 2 : 4;
    if(type != 3 && type != 4)
      return false;
    size_t at = entry + 8;
    if((size_t)count * elementSize > 4)
      at = tiff.u32(entry + 8);
    values.clear();
    for(uint32_t v = 0; v < count; ++v)
    {
      size_t element = at + (size_t)v * elementSize;
      values.push_back(elementSize == 2 ? tiff.u16(element) : tiff.u32(element));
    }
    return true;
  }
  return false;
}
} // namespace

int main(int argc, char** argv)
{
  if(argc < 2)
  {
    fprintf(stderr, "Usage: %s <subsampled-ycbcr-codestream>\n", argv[0]);
    return EXIT_FAILURE;
  }
  std::string input = argv[1];
  std::string tiffName = outputNameFor(input, "_stripgeom.tif");

  if(decompressToTiff(input, tiffName) != EXIT_SUCCESS)
  {
    fprintf(stderr, "%s: decompress to tif failed\n", input.c_str());
    return EXIT_FAILURE;
  }

  std::vector<uint8_t> bytes;
  if(!readWholeFile(tiffName, bytes) || bytes.size() < 8)
  {
    fprintf(stderr, "%s: could not read back the written tif\n", tiffName.c_str());
    return EXIT_FAILURE;
  }
  TiffReader tiff{bytes, memcmp(bytes.data(), "II", 2) == 0};
  size_t ifdOffset = tiff.u32(4);

  std::vector<uint64_t> imageLength, rowsPerStrip, subsampling, stripByteCounts;
  if(!readTagValues(tiff, ifdOffset, TAG_IMAGE_LENGTH, imageLength) ||
     !readTagValues(tiff, ifdOffset, TAG_ROWS_PER_STRIP, rowsPerStrip) ||
     !readTagValues(tiff, ifdOffset, TAG_YCBCR_SUBSAMPLING, subsampling) ||
     !readTagValues(tiff, ifdOffset, TAG_STRIP_BYTE_COUNTS, stripByteCounts) ||
     imageLength.empty() || rowsPerStrip.empty() || subsampling.size() < 2 ||
     stripByteCounts.empty())
  {
    fprintf(stderr, "%s: missing the tags a subsampled ycbcr tif must carry\n", tiffName.c_str());
    return EXIT_FAILURE;
  }

  uint64_t height = imageLength[0];
  uint64_t stripRows = rowsPerStrip[0];
  uint64_t subsampleY = subsampling[1];
  if(stripRows == 0 || subsampleY == 0)
  {
    fprintf(stderr, "%s: rows per strip %llu, vertical subsampling %llu\n", tiffName.c_str(),
            (unsigned long long)stripRows, (unsigned long long)subsampleY);
    return EXIT_FAILURE;
  }
  if(stripRows % subsampleY != 0 && stripRows != height)
  {
    fprintf(stderr, "%s: rows per strip %llu is not a multiple of vertical subsampling %llu\n",
            tiffName.c_str(), (unsigned long long)stripRows, (unsigned long long)subsampleY);
    return EXIT_FAILURE;
  }

  uint64_t expectedStrips = (height + stripRows - 1) / stripRows;
  if(stripByteCounts.size() != expectedStrips)
  {
    fprintf(stderr, "%s: %zu strips written for %llu expected\n", tiffName.c_str(),
            stripByteCounts.size(), (unsigned long long)expectedStrips);
    return EXIT_FAILURE;
  }

  uint64_t firstStripClumpRows = (stripRows + subsampleY - 1) / subsampleY;
  if(stripByteCounts[0] % firstStripClumpRows != 0)
  {
    fprintf(stderr, "%s: strip 0 holds %llu bytes, not a whole number of clump rows\n",
            tiffName.c_str(), (unsigned long long)stripByteCounts[0]);
    return EXIT_FAILURE;
  }
  uint64_t clumpRowBytes = stripByteCounts[0] / firstStripClumpRows;
  for(size_t i = 0; i < stripByteCounts.size(); ++i)
  {
    uint64_t rowsHere = height - (uint64_t)i * stripRows;
    if(rowsHere > stripRows)
      rowsHere = stripRows;
    uint64_t expected = ((rowsHere + subsampleY - 1) / subsampleY) * clumpRowBytes;
    if(stripByteCounts[i] != expected)
    {
      fprintf(stderr, "%s: strip %zu holds %llu bytes for %llu rows, expected %llu\n",
              tiffName.c_str(), i, (unsigned long long)stripByteCounts[i],
              (unsigned long long)rowsHere, (unsigned long long)expected);
      return EXIT_FAILURE;
    }
  }
  remove(tiffName.c_str());

  return EXIT_SUCCESS;
}
