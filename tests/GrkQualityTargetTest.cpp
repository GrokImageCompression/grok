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

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "grok_codec.h"

namespace
{
constexpr uint32_t kSize = 256;
constexpr double kTargetPsnr = 40.0;
constexpr double kTolerance = 1.5;

std::vector<uint8_t> makeTexturedImage()
{
  std::vector<uint8_t> samples(kSize * kSize);
  uint32_t noise = 12345;
  for(uint32_t y = 0; y < kSize; ++y)
  {
    for(uint32_t x = 0; x < kSize; ++x)
    {
      noise = noise * 1103515245u + 12345u;
      double value = 128.0 + 60.0 * std::sin(x / 7.0) * std::cos(y / 11.0) +
                     (double)((noise >> 16) & 0x1F) - 16.0;
      samples[y * kSize + x] = (uint8_t)std::lround(std::fmin(255.0, std::fmax(0.0, value)));
    }
  }

  return samples;
}

bool writePgm(const std::string& path, const std::vector<uint8_t>& samples)
{
  FILE* file = fopen(path.c_str(), "wb");
  if(!file)
    return false;
  bool written = fprintf(file, "P5\n%u %u\n255\n", kSize, kSize) > 0 &&
                 fwrite(samples.data(), 1, samples.size(), file) == samples.size();

  return fclose(file) == 0 && written;
}

bool readPgmSamples(const std::string& path, std::vector<uint8_t>& samples)
{
  FILE* file = fopen(path.c_str(), "rb");
  if(!file)
    return false;
  samples.resize(kSize * kSize);
  // grok writes a comment line into PGM headers
  bool read = fseek(file, -(long)samples.size(), SEEK_END) == 0 &&
              fread(samples.data(), 1, samples.size(), file) == samples.size();
  fclose(file);

  return read;
}
} // namespace

int main()
{
  const std::string image = "grk_quality_target.pgm";
  const std::string codestream = "grk_quality_target.j2k";
  const std::string decoded = "grk_quality_target_out.pgm";
  auto original = makeTexturedImage();
  if(!writePgm(image, original))
  {
    fprintf(stderr, "%s: could not write\n", image.c_str());
    return EXIT_FAILURE;
  }
  const char* compressArgs[] = {"grk_compress",     "-i", image.c_str(), "-o",
                                codestream.c_str(), "-I", "-q",          "40"};
  const char* decompressArgs[] = {"grk_decompress", "-i", codestream.c_str(), "-o",
                                  decoded.c_str()};
  std::vector<uint8_t> roundTrip;
  const char* failedStep = nullptr;
  if(grk_codec_compress(8, compressArgs, nullptr, nullptr) != EXIT_SUCCESS)
    failedStep = "compress";
  else if(grk_codec_decompress(5, decompressArgs) != EXIT_SUCCESS)
    failedStep = "decompress";
  else if(!readPgmSamples(decoded, roundTrip))
    failedStep = "read decoded image";
  remove(image.c_str());
  remove(codestream.c_str());
  remove(decoded.c_str());
  if(failedStep)
  {
    fprintf(stderr, "%s failed\n", failedStep);
    return EXIT_FAILURE;
  }
  double squaredError = 0;
  for(size_t i = 0; i < original.size(); ++i)
  {
    double difference = (double)original[i] - roundTrip[i];
    squaredError += difference * difference;
  }
  double psnr = 10.0 * std::log10(255.0 * 255.0 * (double)original.size() / squaredError);
  if(std::fabs(psnr - kTargetPsnr) > kTolerance)
  {
    fprintf(stderr, "psnr %.2f dB, target %.2f dB\n", psnr, kTargetPsnr);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
