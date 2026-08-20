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

int compressFromTiff(const std::string& input, const std::string& output)
{
  const char* argv[] = {"grk_compress", "-i", input.c_str(), "-o", output.c_str()};
  return grk_codec_compress(5, argv, nullptr, nullptr);
}
} // namespace

int main(int argc, char** argv)
{
  if(argc < 2)
  {
    fprintf(stderr, "Usage: %s <ycbcr-codestream> [j2k|jp2]\n", argv[0]);
    return EXIT_FAILURE;
  }
  std::string input = argv[1];
  // a raw j2k codestream has no colour space field, so a ycbcr image only keeps its
  // photometric tag across the round trip when the intermediate is jp2 or when the
  // chroma subsampling itself identifies the image as ycbcr
  std::string intermediateExtension = argc > 2 ? argv[2] : "j2k";
  std::string firstTiff = outputNameFor(input, "_readback.tif");
  std::string reCompressed = outputNameFor(input, "_readback." + intermediateExtension);
  std::string secondTiff = outputNameFor(input, "_readback2.tif");

  if(decompressToTiff(input, firstTiff) != EXIT_SUCCESS)
  {
    fprintf(stderr, "%s: decompress to tif failed\n", input.c_str());
    return EXIT_FAILURE;
  }
  // grok's own tif reader rejects the file when the clump samples are not written at
  // the BitsPerSample the header declares
  if(compressFromTiff(firstTiff, reCompressed) != EXIT_SUCCESS)
  {
    fprintf(stderr, "%s: grok could not read back the subsampled ycbcr tif it wrote\n",
            firstTiff.c_str());
    return EXIT_FAILURE;
  }
  if(decompressToTiff(reCompressed, secondTiff) != EXIT_SUCCESS)
  {
    fprintf(stderr, "%s: decompress of the re-compressed codestream failed\n",
            reCompressed.c_str());
    return EXIT_FAILURE;
  }

  std::vector<uint8_t> first;
  std::vector<uint8_t> second;
  if(!readWholeFile(firstTiff, first) || !readWholeFile(secondTiff, second))
  {
    fprintf(stderr, "could not read back the written tif files\n");
    return EXIT_FAILURE;
  }
  if(first != second)
  {
    fprintf(stderr, "%s and %s differ: the tif reader and writer disagree on sample packing\n",
            firstTiff.c_str(), secondTiff.c_str());
    return EXIT_FAILURE;
  }
  remove(firstTiff.c_str());
  remove(reCompressed.c_str());
  remove(secondTiff.c_str());

  return EXIT_SUCCESS;
}
