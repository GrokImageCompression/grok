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
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "grok.h"

namespace grk
{
bool applyXYZTransform(grk_image* image, uint8_t targetPrec);
}

extern "C" int32_t mercury_encode_dci(const int32_t* const* planes, uint32_t stride, uint32_t width,
                                      uint32_t height, bool four_k, uint32_t precision_shift,
                                      uint64_t max_cs_size, uint64_t max_comp_size,
                                      uint16_t slope_hint, double rate_tolerance, bool apply_xyz,
                                      bool write_tlm,
                                      const uint8_t* comment, uint32_t comment_len, uint8_t* out,
                                      uint64_t out_capacity, uint64_t* out_len,
                                      uint16_t* slope_threshold);

namespace
{
const uint32_t COMPONENTS = 3;
const uint8_t CODED_PRECISION = 12;
const uint8_t SOURCE_PRECISION = 16;
const uint32_t SIXTEEN_TO_TWELVE_SHIFT = 4;
const size_t OUTPUT_CAPACITY = 8u << 20;
const uint32_t RESOLUTIONS_2K = 6;
const uint32_t RESOLUTIONS_4K = 7;
const uint8_t GUARD_BITS_2K = 1;
const uint8_t GUARD_BITS_4K = 2;
const size_t VERSION_FIELD_WIDTH = 16;

struct Case
{
  const char* name;
  bool fourK;
  uint32_t width;
  uint32_t height;
  // noise amplitude in 16-bit units, 0 for a smooth frame that fits the budget whole
  uint32_t noise;
  // added to the threshold an unhinted encode finds, 0 for no hint
  int32_t hintOffset;
  double tolerance;
  bool applyXyz;
};

uint64_t nextRandom(uint64_t& state)
{
  state = state * 6364136223846793005ULL + 1442695040888963407ULL;
  return state >> 33;
}

std::vector<std::vector<int32_t>> makeFrame(const Case& test)
{
  uint64_t state = test.width * 7919ULL + test.height;
  std::vector<std::vector<int32_t>> planes(COMPONENTS,
                                           std::vector<int32_t>((size_t)test.width * test.height));
  for(uint32_t c = 0; c < COMPONENTS; ++c)
  {
    for(uint32_t y = 0; y < test.height; ++y)
    {
      for(uint32_t x = 0; x < test.width; ++x)
      {
        int64_t value = 8000 + (int64_t)x * 40000 / test.width + (int64_t)y * 15000 / test.height +
                        (int64_t)c * 3000;
        // noisier toward the right so blocks spread over many slopes
        if(test.noise)
          value += (int64_t)(nextRandom(state) % (test.noise * (x + 1) / test.width + 1));
        planes[c][(size_t)y * test.width + x] = (int32_t)std::min<int64_t>(value, 65535);
      }
    }
  }
  return planes;
}

std::string defaultComment()
{
  auto version = std::string(grk_version());
  version.resize(std::max(version.size(), VERSION_FIELD_WIDTH), ' ');
  return "Created by Grok version " + version;
}

std::vector<uint8_t> classicEncode(const Case& test, const std::vector<std::vector<int32_t>>& planes,
                                   uint16_t slopeHint)
{
  grk_image_comp comps[COMPONENTS] = {};
  for(auto& comp : comps)
  {
    comp.w = test.width;
    comp.h = test.height;
    comp.dx = 1;
    comp.dy = 1;
    comp.prec = test.applyXyz ? SOURCE_PRECISION : CODED_PRECISION;
    comp.sgnd = false;
  }
  auto image = grk_image_new(COMPONENTS, comps, GRK_CLRSPC_SRGB, true);
  uint32_t shift = test.applyXyz ? 0 : SIXTEEN_TO_TWELVE_SHIFT;
  for(uint32_t c = 0; c < COMPONENTS; ++c)
  {
    auto data = (int32_t*)image->comps[c].data;
    for(uint32_t y = 0; y < test.height; ++y)
      for(uint32_t x = 0; x < test.width; ++x)
        data[(size_t)y * image->comps[c].stride + x] = planes[c][(size_t)y * test.width + x] >> shift;
  }
  // classic library order: X'Y'Z' from 16 bits, then compress
  if(test.applyXyz && !grk::applyXYZTransform(image, CODED_PRECISION))
  {
    grk_object_unref(&image->obj);
    return {};
  }
  grk_cparameters params;
  grk_compress_set_default_params(&params);
  params.cod_format = GRK_FMT_J2K;
  params.rsiz = test.fourK ? GRK_PROFILE_CINEMA_4K : GRK_PROFILE_CINEMA_2K;
  params.framerate = 24;
  params.max_cs_size = GRK_CINEMA_24_CS;
  params.max_comp_size = GRK_CINEMA_24_COMP;
  params.numgbits = test.fourK ? GUARD_BITS_4K : GUARD_BITS_2K;
  params.numresolution = test.fourK ? RESOLUTIONS_4K : RESOLUTIONS_2K;
  params.write_tlm = true;
  params.rate_control_slope_hint = slopeHint;
  params.rate_control_tolerance = test.tolerance;
  std::vector<uint8_t> out(OUTPUT_CAPACITY);
  grk_stream_params stream = {};
  stream.buf = out.data();
  stream.buf_len = out.size();
  auto codec = grk_compress_init(&stream, &params, image);
  uint64_t length = codec ? grk_compress(codec, nullptr) : 0;
  grk_object_unref(codec);
  grk_object_unref(&image->obj);
  out.resize(length);
  return out;
}

std::vector<uint8_t> mercuryEncode(const Case& test, const std::vector<std::vector<int32_t>>& planes,
                                   uint16_t slopeHint, uint16_t* threshold)
{
  const int32_t* pointers[COMPONENTS] = {planes[0].data(), planes[1].data(), planes[2].data()};
  auto comment = defaultComment();
  std::vector<uint8_t> out(OUTPUT_CAPACITY);
  uint64_t length = 0;
  int32_t rc = mercury_encode_dci(pointers, test.width, test.width, test.height, test.fourK,
                                  SIXTEEN_TO_TWELVE_SHIFT, GRK_CINEMA_24_CS, GRK_CINEMA_24_COMP,
                                  slopeHint, test.tolerance, test.applyXyz, true,
                                  (const uint8_t*)comment.data(),
                                  (uint32_t)comment.size(), out.data(), out.size(), &length,
                                  threshold);
  if(rc != 0)
    length = 0;
  out.resize(length);
  return out;
}

bool runCase(const Case& test)
{
  auto planes = makeFrame(test);
  uint16_t slopeHint = 0;
  uint16_t threshold = 0;
  if(test.hintOffset)
  {
    mercuryEncode(test, planes, 0, &threshold);
    slopeHint = (uint16_t)(threshold + test.hintOffset);
  }
  auto classic = classicEncode(test, planes, slopeHint);
  auto mercury = mercuryEncode(test, planes, slopeHint, &threshold);
  if(classic.empty() || mercury.empty())
  {
    fprintf(stderr, "%s: encode failed, classic %zu bytes, mercury %zu bytes\n", test.name,
            classic.size(), mercury.size());
    return false;
  }
  if(classic != mercury)
  {
    size_t at = 0;
    while(at < std::min(classic.size(), mercury.size()) && classic[at] == mercury[at])
      ++at;
    fprintf(stderr, "%s: codestreams differ at byte %zu, classic %zu bytes, mercury %zu bytes\n",
            test.name, at, classic.size(), mercury.size());
    return false;
  }
  printf("%s: %zu bytes identical, threshold %u\n", test.name, mercury.size(), threshold);
  return true;
}

} // namespace

int main()
{
  grk_initialize(nullptr, 0, nullptr);
  const Case cases[] = {
      {"2k noisy", false, 1024, 540, 20000, 0, 0.0, false},
      {"2k smooth", false, 1024, 540, 0, 0, 0.0, false},
      {"2k odd size", false, 999, 317, 30000, 0, 0.0, false},
      {"4k noisy", true, 1500, 800, 20000, 0, 0.0, false},
      {"4k odd size", true, 1283, 611, 30000, 0, 0.0, false},
      {"2k hint above", false, 1024, 540, 20000, 2100, 0.1, false},
      {"4k hint below", true, 1500, 800, 20000, -2100, 0.02, false},
      {"2k xyz", false, 999, 317, 30000, 0, 0.0, true},
      {"4k xyz", true, 1500, 800, 20000, 0, 0.0, true},
  };
  bool ok = true;
  for(const auto& test : cases)
    ok = runCase(test) && ok;
  grk_deinitialize();
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
