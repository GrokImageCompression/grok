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

// A tile can carry a later tile part anywhere in the codestream, so a tile row can
// finish long after the rows below it.  The incremental band writer throttles the
// parser against the row it is draining, and that row is only reachable by parsing
// on, so the decode used to stop dead.  Run under a ctest timeout: a stall fails.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "grok_codec.h"

namespace
{
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

// a decompress region turns off the incremental band write, so the same pixels
// travel through the whole-image writer instead
int decompressRegionToTiff(const std::string& input, const std::string& region,
                           const std::string& output)
{
  const char* argv[] = {"grk_decompress", "-i", input.c_str(), "-o",
                        output.c_str(),   "-d", region.c_str()};
  return grk_codec_decompress(7, argv);
}
} // namespace

int main(int argc, char** argv)
{
  if(argc < 3)
  {
    fprintf(stderr, "Usage: %s <codestream> <x0,y0,x1,y1 covering the whole image>\n", argv[0]);
    return EXIT_FAILURE;
  }
  std::string input = argv[1];
  std::string region = argv[2];
  std::string bandTiff = outputNameFor(input, "_band.tif");
  std::string wholeTiff = outputNameFor(input, "_whole.tif");

  if(decompressToTiff(input, bandTiff) != EXIT_SUCCESS)
  {
    fprintf(stderr, "%s: incremental band write decompress failed\n", input.c_str());
    return EXIT_FAILURE;
  }
  if(decompressRegionToTiff(input, region, wholeTiff) != EXIT_SUCCESS)
  {
    fprintf(stderr, "%s: whole image decompress failed\n", input.c_str());
    return EXIT_FAILURE;
  }

  std::vector<uint8_t> band;
  std::vector<uint8_t> whole;
  if(!readWholeFile(bandTiff, band) || !readWholeFile(wholeTiff, whole))
  {
    fprintf(stderr, "could not read back the written tif files\n");
    return EXIT_FAILURE;
  }
  if(band != whole)
  {
    fprintf(stderr, "%s and %s differ: the incremental band write lost or reordered rows\n",
            bandTiff.c_str(), wholeTiff.c_str());
    return EXIT_FAILURE;
  }
  remove(bandTiff.c_str());
  remove(wholeTiff.c_str());

  return EXIT_SUCCESS;
}
