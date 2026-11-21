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

// Rate control must respect max_cs_size exactly: every byte of the code
// stream counts against the budget, including the first tile-part's SOT and
// SOD markers. Dense noise pushes the allocator to its ceiling, so any
// header bytes missing from the reservation surface as a code stream a few
// bytes over budget.

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include "grok.h"

namespace
{
int g_failures = 0;

constexpr uint32_t kWidth = 2048;
constexpr uint32_t kHeight = 1080;
constexpr uint32_t kNumComps = 3;
constexpr uint32_t kPrecision = 12;
constexpr uint32_t kNumFrames = 16;

uint32_t xorshift32(uint32_t& state)
{
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

// compress one frame of seeded noise under the 2K cinema profile and return
// the code stream length, or 0 on failure
uint64_t compressNoiseFrame(uint32_t seed, uint8_t* outBuf, size_t outBufLen)
{
  grk_cparameters compressParams;
  grk_compress_set_default_params(&compressParams);
  compressParams.cod_format = GRK_FMT_J2K;
  compressParams.rsiz = GRK_PROFILE_CINEMA_2K;
  compressParams.framerate = 24;

  grk_stream_params outputStreamParams = {};
  outputStreamParams.buf = outBuf;
  outputStreamParams.buf_len = outBufLen;

  auto components = std::make_unique<grk_image_comp[]>(kNumComps);
  for(uint32_t i = 0; i < kNumComps; ++i)
  {
    auto c = &components[i];
    c->w = kWidth;
    c->h = kHeight;
    c->dx = 1;
    c->dy = 1;
    c->prec = kPrecision;
    c->sgnd = false;
  }
  auto inputImage = grk_image_new(kNumComps, components.get(), GRK_CLRSPC_SRGB, true);
  if(!inputImage)
    return 0;

  uint32_t state = seed;
  for(uint16_t compno = 0; compno < inputImage->numcomps; ++compno)
  {
    auto comp = inputImage->comps + compno;
    auto compData = (int32_t*)comp->data;
    for(uint32_t j = 0; j < comp->h; ++j)
    {
      for(uint32_t i = 0; i < comp->w; ++i)
        compData[i] = (int32_t)(xorshift32(state) & 0xfff);
      compData += comp->stride;
    }
  }

  uint64_t compressedLength = 0;
  auto codec = grk_compress_init(&outputStreamParams, &compressParams, inputImage);
  if(codec)
    compressedLength = grk_compress(codec, nullptr);
  grk_object_unref(codec);
  grk_object_unref(&inputImage->obj);

  return compressedLength;
}

} // namespace

int main()
{
  grk_initialize(nullptr, 0, nullptr);

  size_t bufLen = (size_t)kNumComps * 2 * kWidth * kHeight;
  auto buf = std::make_unique<uint8_t[]>(bufLen);

  for(uint32_t frame = 0; frame < kNumFrames; ++frame)
  {
    uint64_t len = compressNoiseFrame(0x9e3779b9u + frame, buf.get(), bufLen);
    if(len == 0)
    {
      ++g_failures;
      std::fprintf(stderr, "FAIL frame %u: compression failed\n", frame);
      continue;
    }
    if(len > GRK_CINEMA_24_CS)
    {
      char path[64];
      std::snprintf(path, sizeof(path), "/tmp/grk_over_frame_%u.j2c", frame);
      if(auto fp = std::fopen(path, "wb"))
      {
        std::fwrite(buf.get(), 1, len, fp);
        std::fclose(fp);
      }
      ++g_failures;
      std::fprintf(stderr, "FAIL frame %u: code stream %" PRIu64 " bytes exceeds max_cs_size %d\n",
                   frame, len, GRK_CINEMA_24_CS);
    }
  }

  grk_deinitialize();

  if(g_failures == 0)
  {
    std::fprintf(stderr, "GrkRateControlTest: all tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "GrkRateControlTest: %d failure(s)\n", g_failures);
  return 1;
}
