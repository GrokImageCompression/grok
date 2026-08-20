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
#include <filesystem>
#include <string>
#include <vector>

#include "grok_codec.h"

namespace
{
constexpr uint32_t imageWidth = 64;
constexpr uint32_t imageHeight = 64;
// keeps every synthesized sample below 255 once the offsets below are added
constexpr int baseSampleModulus = 200;

bool writeGrayscalePgm(const std::filesystem::path& path, int sampleOffset)
{
  FILE* file = fopen(path.string().c_str(), "wb");
  if(!file)
  {
    fprintf(stderr, "unable to write %s\n", path.string().c_str());
    return false;
  }
  fprintf(file, "P5\n%u %u\n255\n", imageWidth, imageHeight);
  for(uint32_t y = 0; y < imageHeight; ++y)
  {
    for(uint32_t x = 0; x < imageWidth; ++x)
    {
      auto sample = (unsigned char)((x * 3 + y * 5) % baseSampleModulus + sampleOffset);
      fputc(sample, file);
    }
  }
  return fclose(file) == 0;
}

bool runCase(const char* name, const std::vector<std::string>& arguments, bool expectSuccess)
{
  std::vector<const char*> argv;
  argv.push_back("compare_images");
  for(const auto& argument : arguments)
  {
    argv.push_back(argument.c_str());
  }

  int result = grk_codec_compare_images((int)argv.size(), argv.data());
  bool succeeded = result == EXIT_SUCCESS;
  if(succeeded != expectSuccess)
  {
    fprintf(stderr, "%s: expected %s, got exit code %d\n", name,
            expectSuccess ? "success" : "failure", result);
    return false;
  }
  fprintf(stderr, "%s: ok\n", name);
  return true;
}
} // namespace

int main(int argc, char* argv[])
{
  std::filesystem::path directory =
      (argc > 1) ? std::filesystem::path(argv[1]) : std::filesystem::current_path();
  std::error_code error;
  std::filesystem::create_directories(directory, error);

  auto base = directory / "compare_images_base.pgm";
  auto identical = directory / "compare_images_identical.pgm";
  auto offByOne = directory / "compare_images_off_by_one.pgm";
  auto offByFifty = directory / "compare_images_off_by_fifty.pgm";

  if(!writeGrayscalePgm(base, 0) || !writeGrayscalePgm(identical, 0) ||
     !writeGrayscalePgm(offByOne, 1) || !writeGrayscalePgm(offByFifty, 50))
  {
    return EXIT_FAILURE;
  }

  bool passed = true;
  passed &= runCase("identical images, no tolerance",
                    {"-b", base.string(), "-t", identical.string(), "-n", "1", "-d"}, true);
  passed &= runCase("differing images, no tolerance",
                    {"-b", base.string(), "-t", offByFifty.string(), "-n", "1", "-d"}, false);
  // every sample is off by one, so MSE is 1 and peak absolute error is 1
  passed &= runCase(
      "differing images within tolerance",
      {"-b", base.string(), "-t", offByOne.string(), "-n", "1", "-m", "1.5", "-p", "1"}, true);
  passed &= runCase(
      "differing images beyond tolerance",
      {"-b", base.string(), "-t", offByFifty.string(), "-n", "1", "-m", "1.5", "-p", "1"}, false);

  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
