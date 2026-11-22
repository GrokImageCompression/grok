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
// grk_compress reads RAW samples 64 rows at a time
constexpr uint32_t kHeight = 150;

struct RawCase
{
  const char* name;
  uint16_t numcomps;
  uint8_t precision;
  bool sgnd;
  bool bigEndian;
};

int32_t expectedSample(const RawCase& raw, uint16_t compno, uint32_t x, uint32_t y)
{
  int32_t range = 1 << raw.precision;
  int32_t value =
      (int32_t)((x * 7919u + y * 104729u + x * y + compno * 15485863u) % (uint32_t)range);
  return raw.sgnd ? value - range / 2 : value;
}

bool writeRaw(const RawCase& raw, const std::string& path, size_t droppedBytes)
{
  std::vector<uint8_t> bytes;
  for(uint16_t compno = 0; compno < raw.numcomps; ++compno)
  {
    for(uint32_t y = 0; y < kHeight; ++y)
    {
      for(uint32_t x = 0; x < kWidth; ++x)
      {
        auto value = (uint32_t)expectedSample(raw, compno, x, y);
        if(raw.precision <= 8)
        {
          bytes.push_back((uint8_t)value);
        }
        else if(raw.bigEndian)
        {
          bytes.push_back((uint8_t)(value >> 8));
          bytes.push_back((uint8_t)value);
        }
        else
        {
          bytes.push_back((uint8_t)value);
          bytes.push_back((uint8_t)(value >> 8));
        }
      }
    }
  }
  bytes.resize(bytes.size() - droppedBytes);
  FILE* file = fopen(path.c_str(), "wb");
  if(!file)
    return false;
  bool written = fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();

  return fclose(file) == 0 && written;
}

bool decodedSamplesMatch(const RawCase& raw, const std::string& codestream)
{
  grk_decompress_parameters decompressParams = {};
  grk_stream_params stream = {};
  snprintf(stream.file, sizeof(stream.file), "%s", codestream.c_str());
  auto codec = grk_decompress_init(&stream, &decompressParams);
  grk_header_info header = {};
  if(!codec || !grk_decompress_read_header(codec, &header))
  {
    fprintf(stderr, "%s: header read failed\n", raw.name);
    grk_object_unref(codec);
    return false;
  }
  grk_image* decoded = nullptr;
  if(grk_decompress(codec, nullptr))
    decoded = grk_decompress_get_image(codec);
  if(!decoded || decoded->numcomps != raw.numcomps)
  {
    fprintf(stderr, "%s: decode failed or wrong component count\n", raw.name);
    grk_object_unref(codec);
    return false;
  }
  bool match = true;
  for(uint16_t compno = 0; match && compno < raw.numcomps; ++compno)
  {
    auto comp = decoded->comps + compno;
    bool int16Samples = comp->data_type == GRK_INT_16;
    match = comp->sgnd == raw.sgnd && comp->prec == raw.precision;
    if(!match)
      fprintf(stderr, "%s: component %u decoded sgnd %d precision %u\n", raw.name, compno,
              comp->sgnd, comp->prec);
    for(uint32_t y = 0; match && y < kHeight; ++y)
    {
      for(uint32_t x = 0; match && x < kWidth; ++x)
      {
        size_t index = (size_t)y * comp->stride + x;
        int32_t got = int16Samples ? ((const int16_t*)comp->data)[index]
                                   : ((const int32_t*)comp->data)[index];
        if(got != expectedSample(raw, compno, x, y))
        {
          fprintf(stderr, "%s: component %u (%u,%u) is %d, expected %d\n", raw.name, compno, x, y,
                  got, expectedSample(raw, compno, x, y));
          match = false;
        }
      }
    }
  }
  grk_object_unref(codec);

  return match;
}

std::string rawFormatArgument(const RawCase& raw)
{
  return std::to_string(kWidth) + "," + std::to_string(kHeight) + "," +
         std::to_string(raw.numcomps) + "," + std::to_string(raw.precision) + "," +
         (raw.sgnd ? "s" : "u");
}

bool compress(const RawCase& raw, const std::string& image, const std::string& codestream)
{
  auto format = rawFormatArgument(raw);
  const char* argv[] = {"grk_compress",     "-i", image.c_str(), "-o",
                        codestream.c_str(), "-F", format.c_str()};
  return grk_codec_compress(7, argv, nullptr, nullptr) == EXIT_SUCCESS;
}

bool compressFromStdin(const RawCase& raw, const std::string& image, const std::string& codestream)
{
  if(!freopen(image.c_str(), "rb", stdin))
    return false;
  auto format = rawFormatArgument(raw);
  const char* argv[] = {
      "grk_compress", "-K",          raw.bigEndian ? "raw" : "rawl", "-o", codestream.c_str(),
      "-F",           format.c_str()};
  return grk_codec_compress(7, argv, nullptr, nullptr) == EXIT_SUCCESS;
}

std::string imagePath(const RawCase& raw)
{
  return raw.bigEndian ? "grk_raw_strip_read.raw" : "grk_raw_strip_read.rawl";
}
} // namespace

int main()
{
  const RawCase cases[] = {
      {"gray unsigned 10 bit", 1, 10, false, true}, {"rgb unsigned 8 bit", 3, 8, false, true},
      {"rgb signed 8 bit", 3, 8, true, false},      {"rgb unsigned 12 bit LE", 3, 12, false, false},
      {"rgb signed 16 bit BE", 3, 16, true, true},
  };
  const std::string codestream = "grk_raw_strip_read.j2k";
  bool passed = true;
  for(const auto& raw : cases)
  {
    auto image = imagePath(raw);
    if(!writeRaw(raw, image, 0))
    {
      fprintf(stderr, "%s: could not write\n", raw.name);
      return EXIT_FAILURE;
    }
    bool compressed = compress(raw, image, codestream);
    remove(image.c_str());
    if(!compressed)
    {
      fprintf(stderr, "%s: compress failed\n", raw.name);
      passed = false;
      continue;
    }
    passed = decodedSamplesMatch(raw, codestream) && passed;
  }

  const RawCase truncated = {"truncated", 3, 12, false, true};
  auto truncatedImage = imagePath(truncated);
  if(!writeRaw(truncated, truncatedImage, 1))
  {
    fprintf(stderr, "%s: could not write\n", truncated.name);
    return EXIT_FAILURE;
  }
  if(compress(truncated, truncatedImage, codestream))
  {
    fprintf(stderr, "truncated file compressed\n");
    passed = false;
  }
  remove(truncatedImage.c_str());

  const RawCase stdinCases[] = {
      {"gray unsigned 10 bit from stdin", 1, 10, false, true},
      {"rgb unsigned 12 bit from stdin", 3, 12, false, false},
  };
  for(const auto& raw : stdinCases)
  {
    auto image = imagePath(raw);
    if(!writeRaw(raw, image, 0))
    {
      fprintf(stderr, "%s: could not write\n", raw.name);
      return EXIT_FAILURE;
    }
    bool compressed = compressFromStdin(raw, image, codestream);
    remove(image.c_str());
    if(!compressed)
    {
      fprintf(stderr, "%s: compress failed\n", raw.name);
      passed = false;
      continue;
    }
    passed = decodedSamplesMatch(raw, codestream) && passed;
  }
  remove(codestream.c_str());

  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
