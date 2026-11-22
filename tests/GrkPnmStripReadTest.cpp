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
constexpr uint32_t kWidth = 37;
// grk_compress reads PNM samples 64 rows at a time
constexpr uint32_t kHeight = 150;

struct PnmCase
{
  const char* name;
  int magicNumber;
  const char* pamTupleType;
  uint16_t numcomps;
  uint32_t maxval;
};

uint32_t expectedSample(const PnmCase& pnm, uint32_t x, uint32_t y, uint16_t c)
{
  return (x * 7919u + y * 104729u + c * 1299709u + x * y) % (pnm.maxval + 1);
}

bool writePnm(const PnmCase& pnm, const std::string& path, uint32_t sampleAboveMaxvalAt)
{
  FILE* file = fopen(path.c_str(), "wb");
  if(!file)
    return false;
  bool written =
      pnm.magicNumber == 7
          ? fprintf(file, "P7\nWIDTH %u\nHEIGHT %u\nDEPTH %u\nMAXVAL %u\nTUPLTYPE %s\nENDHDR\n",
                    kWidth, kHeight, pnm.numcomps, pnm.maxval, pnm.pamTupleType) > 0
          : fprintf(file, "P%d\n%u %u\n%u\n", pnm.magicNumber, kWidth, kHeight, pnm.maxval) > 0;
  bool wide = pnm.maxval > 255;
  std::vector<uint8_t> bytes;
  uint32_t sampleIndex = 0;
  for(uint32_t y = 0; y < kHeight; ++y)
  {
    for(uint32_t x = 0; x < kWidth; ++x)
    {
      for(uint16_t c = 0; c < pnm.numcomps; ++c)
      {
        uint32_t value =
            sampleIndex++ == sampleAboveMaxvalAt ? pnm.maxval + 1 : expectedSample(pnm, x, y, c);
        if(wide)
          bytes.push_back((uint8_t)(value >> 8));
        bytes.push_back((uint8_t)value);
      }
    }
  }
  written = written && fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();

  return fclose(file) == 0 && written;
}

bool decodedSamplesMatch(const PnmCase& pnm, const std::string& codestream)
{
  grk_decompress_parameters decompressParams = {};
  grk_stream_params stream = {};
  snprintf(stream.file, sizeof(stream.file), "%s", codestream.c_str());
  auto codec = grk_decompress_init(&stream, &decompressParams);
  grk_header_info header = {};
  if(!codec || !grk_decompress_read_header(codec, &header))
  {
    fprintf(stderr, "%s: header read failed\n", pnm.name);
    grk_object_unref(codec);
    return false;
  }
  grk_image* decoded = nullptr;
  if(grk_decompress(codec, nullptr))
    decoded = grk_decompress_get_image(codec);
  if(!decoded || decoded->numcomps != pnm.numcomps)
  {
    fprintf(stderr, "%s: decode failed or wrong component count\n", pnm.name);
    grk_object_unref(codec);
    return false;
  }
  bool match = true;
  for(uint16_t c = 0; match && c < pnm.numcomps; ++c)
  {
    auto comp = decoded->comps + c;
    bool int16Samples = comp->data_type == GRK_INT_16;
    if(comp->w != kWidth || comp->h != kHeight)
    {
      fprintf(stderr, "%s: component %u is %ux%u\n", pnm.name, c, comp->w, comp->h);
      match = false;
    }
    for(uint32_t y = 0; match && y < kHeight; ++y)
    {
      for(uint32_t x = 0; match && x < kWidth; ++x)
      {
        size_t index = (size_t)y * comp->stride + x;
        int32_t got = int16Samples ? ((const int16_t*)comp->data)[index]
                                   : ((const int32_t*)comp->data)[index];
        if(got != (int32_t)expectedSample(pnm, x, y, c))
        {
          fprintf(stderr, "%s: component %u (%u,%u) is %d, expected %u\n", pnm.name, c, x, y, got,
                  expectedSample(pnm, x, y, c));
          match = false;
        }
      }
    }
  }
  grk_object_unref(codec);

  return match;
}

bool compress(const std::string& image, const std::string& codestream)
{
  const char* argv[] = {"grk_compress", "-i", image.c_str(), "-o", codestream.c_str()};
  return grk_codec_compress(5, argv, nullptr, nullptr) == EXIT_SUCCESS;
}
} // namespace

int main()
{
  const PnmCase cases[] = {
      {"P5 8 bit", 5, "", 1, 255},
      {"P5 maxval 200", 5, "", 1, 200},
      {"P5 16 bit", 5, "", 1, 65535},
      {"P5 maxval 1000", 5, "", 1, 1000},
      {"P6 8 bit", 6, "", 3, 255},
      {"P6 12 bit", 6, "", 3, 4095},
      {"P7 GRAYSCALE 8 bit", 7, "GRAYSCALE", 1, 255},
      {"P7 GRAYSCALE_ALPHA 8 bit", 7, "GRAYSCALE_ALPHA", 2, 255},
      {"P7 RGB 8 bit", 7, "RGB", 3, 255},
      {"P7 RGB_ALPHA 8 bit", 7, "RGB_ALPHA", 4, 255},
      {"P7 GRAYSCALE 16 bit", 7, "GRAYSCALE", 1, 65535},
      {"P7 GRAYSCALE_ALPHA 16 bit", 7, "GRAYSCALE_ALPHA", 2, 65535},
      {"P7 RGB 16 bit", 7, "RGB", 3, 65535},
      {"P7 RGB_ALPHA 16 bit", 7, "RGB_ALPHA", 4, 65535},
  };
  const std::string image = "grk_pnm_strip_read.pnm";
  const std::string codestream = "grk_pnm_strip_read.j2k";
  bool passed = true;
  for(const auto& pnm : cases)
  {
    if(!writePnm(pnm, image, UINT32_MAX))
    {
      fprintf(stderr, "%s: could not write\n", pnm.name);
      return EXIT_FAILURE;
    }
    if(!compress(image, codestream))
    {
      fprintf(stderr, "%s: compress failed\n", pnm.name);
      passed = false;
      continue;
    }
    passed = decodedSamplesMatch(pnm, codestream) && passed;
  }
  const PnmCase aboveMaxval[] = {{"P5 maxval 200", 5, "", 1, 200},
                                 {"P6 maxval 1000", 6, "", 3, 1000}};
  for(const auto& pnm : aboveMaxval)
  {
    // in the second 64 row read
    uint32_t sampleIndex = 100 * kWidth * pnm.numcomps + 5;
    if(!writePnm(pnm, image, sampleIndex))
    {
      fprintf(stderr, "%s: could not write\n", pnm.name);
      return EXIT_FAILURE;
    }
    if(compress(image, codestream))
    {
      fprintf(stderr, "%s: sample above maxval compressed\n", pnm.name);
      passed = false;
    }
  }
  remove(image.c_str());
  remove(codestream.c_str());

  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
