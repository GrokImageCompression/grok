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

// A slope hint only narrows the search, so every hint near the true threshold
// must produce the same code stream as no hint at all. A hint that lands the
// true threshold just inside the bracket floor is the case that used to fall
// back to no rate control and emit a code stream far over budget.

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "grok.h"

namespace
{
int g_failures = 0;
bool g_rateControlWarningSeen = false;

constexpr uint32_t kWidth = 2048;
constexpr uint32_t kHeight = 1080;
constexpr uint32_t kNumComps = 3;
constexpr uint32_t kPrecision = 12;
constexpr uint32_t kSeed = 0x9e3779b9u;
constexpr int kBracketFloorOffset = 511;
// both bracket edges and their neighbours, plus the middle
constexpr int kOffsets[] = {-513, -512, -511, -510, -256, -1, 0, 1, 256, 510, 511, 512, 513};
constexpr uint16_t kMinExpectedThreshold = 1024;

const char kRateControlFailure[] = "Unable to perform rate control";

void recordWarning(const char* msg, void* /* client_data */)
{
  if(msg && std::strstr(msg, kRateControlFailure))
    g_rateControlWarningSeen = true;
}

uint32_t xorshift32(uint32_t& state)
{
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

// compress one frame of seeded noise under the 2K cinema profile and return the
// code stream length, or 0 on failure
uint64_t compressNoiseFrame(uint16_t slopeHint, uint8_t* outBuf, size_t outBufLen,
                            uint16_t* threshold)
{
  *threshold = 0;

  grk_cparameters compressParams;
  grk_compress_set_default_params(&compressParams);
  compressParams.cod_format = GRK_FMT_J2K;
  compressParams.rsiz = GRK_PROFILE_CINEMA_2K;
  compressParams.framerate = 24;
  compressParams.rate_control_slope_hint = slopeHint;

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

  uint32_t state = kSeed;
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
  {
    compressedLength = grk_compress(codec, nullptr);
    *threshold = grk_compress_get_slope_threshold(codec);
  }
  grk_object_unref(codec);
  grk_object_unref(&inputImage->obj);

  return compressedLength;
}

void checkHint(const std::string& label, int offset, uint16_t hint, uint64_t expectedLength,
               uint16_t expectedThreshold, uint8_t* outBuf, size_t outBufLen)
{
  uint16_t threshold = 0;
  uint64_t length = compressNoiseFrame(hint, outBuf, outBufLen, &threshold);
  if(length == 0)
  {
    ++g_failures;
    std::fprintf(stderr, "FAIL %s: offset %d hint %u: compression failed\n", label.c_str(), offset,
                 hint);
    return;
  }
  if(length > GRK_CINEMA_24_CS)
  {
    ++g_failures;
    std::fprintf(stderr,
                 "FAIL %s: offset %d hint %u: code stream %" PRIu64
                 " bytes exceeds max_cs_size %d, threshold %u\n",
                 label.c_str(), offset, hint, length, GRK_CINEMA_24_CS, threshold);
    return;
  }
  if(threshold != expectedThreshold || length != expectedLength)
  {
    ++g_failures;
    std::fprintf(stderr,
                 "FAIL %s: offset %d hint %u: length %" PRIu64
                 " threshold %u, expected length %" PRIu64 " threshold %u\n",
                 label.c_str(), offset, hint, length, threshold, expectedLength, expectedThreshold);
  }
}

} // namespace

int main()
{
  // grk_initialize replaces the handlers unless info_callback is already set
  grk_msg_handlers handlers = {};
  handlers.info_callback = recordWarning;
  handlers.warn_callback = recordWarning;
  handlers.error_callback = recordWarning;
  grk_set_msg_handlers(handlers);

  grk_initialize(nullptr, 0, nullptr);

  size_t bufLen = (size_t)kNumComps * 2 * kWidth * kHeight;
  auto buf = std::make_unique<uint8_t[]>(bufLen);

  uint16_t baselineThreshold = 0;
  uint64_t baselineLength = compressNoiseFrame(0, buf.get(), bufLen, &baselineThreshold);
  if(baselineLength == 0 || baselineLength > GRK_CINEMA_24_CS)
  {
    std::fprintf(stderr, "FAIL baseline: length %" PRIu64 " bytes, max_cs_size %d\n",
                 baselineLength, GRK_CINEMA_24_CS);
    grk_deinitialize();
    return 1;
  }
  if(baselineThreshold <= kMinExpectedThreshold)
  {
    std::fprintf(stderr, "FAIL baseline: threshold %u must exceed %u to test the bracket\n",
                 baselineThreshold, kMinExpectedThreshold);
    grk_deinitialize();
    return 1;
  }

  for(int offset : kOffsets)
  {
    int hint = (int)baselineThreshold + offset;
    if(hint < 0 || hint > UINT16_MAX)
      continue;
    checkHint("hint sweep", offset, (uint16_t)hint, baselineLength, baselineThreshold, buf.get(),
              bufLen);
  }

  int floorHint = (int)baselineThreshold + kBracketFloorOffset;
  if(floorHint <= UINT16_MAX)
    checkHint("true threshold one above the bracket floor", kBracketFloorOffset,
              (uint16_t)floorHint, baselineLength, baselineThreshold, buf.get(), bufLen);

  if(g_rateControlWarningSeen)
  {
    ++g_failures;
    std::fprintf(stderr, "FAIL: a slope hint made rate control fail (\"%s\")\n",
                 kRateControlFailure);
  }

  grk_deinitialize();

  if(g_failures == 0)
  {
    std::fprintf(stderr, "GrkSlopeHintBracketTest: all tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "GrkSlopeHintBracketTest: %d failure(s)\n", g_failures);
  return 1;
}
