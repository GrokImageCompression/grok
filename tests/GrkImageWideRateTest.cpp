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
#include <sys/stat.h>

#include "grok_codec.h"

namespace
{
constexpr uint32_t kSize = 512;
constexpr double kRatio = 8.0;
constexpr double kMinimumFill = 0.97;

// the left half is blank, so its tiles need almost none of their share of the budget
bool writeHalfBlankImage(const std::string& path)
{
  FILE* file = fopen(path.c_str(), "wb");
  if(!file)
    return false;
  bool written = fprintf(file, "P5\n%u %u\n255\n", kSize, kSize) > 0;
  uint32_t noise = 12345;
  for(uint32_t y = 0; y < kSize; ++y)
  {
    for(uint32_t x = 0; x < kSize; ++x)
    {
      noise = noise * 1103515245u + 12345u;
      int sample = x < kSize / 2 ? 0 : (int)((noise >> 16) & 0xFF);
      written = written && fputc(sample, file) != EOF;
    }
  }

  return fclose(file) == 0 && written;
}
} // namespace

int main()
{
  const std::string image = "grk_image_wide_rate.pgm";
  const std::string codestream = "grk_image_wide_rate.j2k";
  if(!writeHalfBlankImage(image))
  {
    fprintf(stderr, "%s: could not write\n", image.c_str());
    return EXIT_FAILURE;
  }
  const char* argv[] = {"grk_compress", "-i",      image.c_str(), "-o", codestream.c_str(),
                        "-t",           "128,128", "-I",          "-r", "8"};
  bool compressed = grk_codec_compress(10, argv, nullptr, nullptr) == EXIT_SUCCESS;
  struct stat info = {};
  bool sized = compressed && stat(codestream.c_str(), &info) == 0;
  remove(image.c_str());
  remove(codestream.c_str());
  if(!sized)
  {
    fprintf(stderr, "compress failed\n");
    return EXIT_FAILURE;
  }
  double target = (double)kSize * kSize / kRatio;
  double size = (double)info.st_size;
  if(size > target || size < target * kMinimumFill)
  {
    fprintf(stderr, "codestream is %.0f bytes, target %.0f\n", size, target);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
