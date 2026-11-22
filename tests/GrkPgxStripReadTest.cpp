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
// grk_compress reads PGX samples 64 rows at a time
constexpr uint32_t kHeight = 150;

struct PgxCase
{
  const char* name;
  uint8_t precision;
  bool sgnd;
  bool bigEndian;
};

int32_t expectedSample(const PgxCase& pgx, uint32_t x, uint32_t y)
{
  int32_t range = 1 << pgx.precision;
  int32_t value = (int32_t)((x * 7919u + y * 104729u + x * y) % (uint32_t)range);
  return pgx.sgnd ? value - range / 2 : value;
}

bool writePgx(const PgxCase& pgx, const std::string& path, size_t droppedBytes)
{
  FILE* file = fopen(path.c_str(), "wb");
  if(!file)
    return false;
  bool written = fprintf(file, "PG %s %c%u %u %u\n", pgx.bigEndian ? "ML" : "LM",
                         pgx.sgnd ? '-' : '+', pgx.precision, kWidth, kHeight) > 0;
  std::vector<uint8_t> bytes;
  for(uint32_t y = 0; y < kHeight; ++y)
  {
    for(uint32_t x = 0; x < kWidth; ++x)
    {
      auto value = (uint32_t)expectedSample(pgx, x, y);
      if(pgx.precision <= 8)
      {
        bytes.push_back((uint8_t)value);
      }
      else if(pgx.bigEndian)
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
  bytes.resize(bytes.size() - droppedBytes);
  written = written && fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();

  return fclose(file) == 0 && written;
}

bool decodedSamplesMatch(const PgxCase& pgx, const std::string& codestream)
{
  grk_decompress_parameters decompressParams = {};
  grk_stream_params stream = {};
  snprintf(stream.file, sizeof(stream.file), "%s", codestream.c_str());
  auto codec = grk_decompress_init(&stream, &decompressParams);
  grk_header_info header = {};
  if(!codec || !grk_decompress_read_header(codec, &header))
  {
    fprintf(stderr, "%s: header read failed\n", pgx.name);
    grk_object_unref(codec);
    return false;
  }
  grk_image* decoded = nullptr;
  if(grk_decompress(codec, nullptr))
    decoded = grk_decompress_get_image(codec);
  if(!decoded || decoded->numcomps != 1)
  {
    fprintf(stderr, "%s: decode failed or wrong component count\n", pgx.name);
    grk_object_unref(codec);
    return false;
  }
  auto comp = decoded->comps;
  bool int16Samples = comp->data_type == GRK_INT_16;
  bool match = comp->sgnd == pgx.sgnd && comp->prec == pgx.precision;
  if(!match)
    fprintf(stderr, "%s: decoded sgnd %d precision %u\n", pgx.name, comp->sgnd, comp->prec);
  for(uint32_t y = 0; match && y < kHeight; ++y)
  {
    for(uint32_t x = 0; match && x < kWidth; ++x)
    {
      size_t index = (size_t)y * comp->stride + x;
      int32_t got =
          int16Samples ? ((const int16_t*)comp->data)[index] : ((const int32_t*)comp->data)[index];
      if(got != expectedSample(pgx, x, y))
      {
        fprintf(stderr, "%s: (%u,%u) is %d, expected %d\n", pgx.name, x, y, got,
                expectedSample(pgx, x, y));
        match = false;
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
  const PgxCase cases[] = {
      {"unsigned 4 bit", 4, false, true},       {"signed 4 bit", 4, true, true},
      {"unsigned 8 bit", 8, false, true},       {"signed 8 bit", 8, true, true},
      {"unsigned 12 bit ML", 12, false, true},  {"signed 12 bit LM", 12, true, false},
      {"unsigned 16 bit LM", 16, false, false}, {"signed 16 bit ML", 16, true, true},
  };
  const std::string image = "grk_pgx_strip_read.pgx";
  const std::string codestream = "grk_pgx_strip_read.j2k";
  bool passed = true;
  for(const auto& pgx : cases)
  {
    if(!writePgx(pgx, image, 0))
    {
      fprintf(stderr, "%s: could not write\n", pgx.name);
      return EXIT_FAILURE;
    }
    if(!compress(image, codestream))
    {
      fprintf(stderr, "%s: compress failed\n", pgx.name);
      passed = false;
      continue;
    }
    passed = decodedSamplesMatch(pgx, codestream) && passed;
  }
  const PgxCase truncated = {"truncated", 12, false, true};
  if(!writePgx(truncated, image, 1))
  {
    fprintf(stderr, "%s: could not write\n", truncated.name);
    return EXIT_FAILURE;
  }
  if(compress(image, codestream))
  {
    fprintf(stderr, "truncated file compressed\n");
    passed = false;
  }
  remove(image.c_str());
  remove(codestream.c_str());

  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
