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
#include <memory>
#include <vector>

#include "grok.h"

namespace
{
int g_failures = 0;

constexpr uint32_t kWidth = 1024;
constexpr uint32_t kHeight = 1024;
constexpr uint16_t kNumComps = 3;
constexpr uint8_t kPrecision = 8;
constexpr int32_t kMaxSample = (1 << kPrecision) - 1;
constexpr double kNoiseRectangleRatio = 40.0;
constexpr double kFaintNoiseRatio = 20.0;
constexpr double kPsnrToleranceDecibels = 0.25;
constexpr uint32_t kNoiseOriginX = kWidth / 2;
constexpr uint32_t kNoiseOriginY = kHeight / 2;
constexpr uint32_t kNoiseSeed = 0x9e3779b9u;
constexpr int32_t kComponentOffset = 16;
constexpr int32_t kFaintNoiseAmplitude = 2;
constexpr uint8_t kQuantStepShift = 1;
constexpr double kQuantStepShiftToleranceDecibels = 1.0;
constexpr double kRateControlTolerance = 0.02;
constexpr uint8_t kHardEdgePrecision = 12;
constexpr int32_t kHardEdgeMaxSample = (1 << kHardEdgePrecision) - 1;
constexpr double kHardEdgeRatio = 60.0;
constexpr int32_t kHardEdgeGrainAmplitude = 16;

uint32_t xorshift32(uint32_t& state)
{
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

using Planes = std::vector<std::vector<int32_t>>;

struct Source
{
  Planes planes;
  uint8_t precision;
};

int32_t clampSample(int32_t value, int32_t maxSample = kMaxSample)
{
  if(value < 0)
    return 0;
  return value > maxSample ? maxSample : value;
}

Planes makeGradient()
{
  Planes planes(kNumComps, std::vector<int32_t>((size_t)kWidth * kHeight));
  for(uint32_t y = 0; y < kHeight; ++y)
  {
    for(uint32_t x = 0; x < kWidth; ++x)
    {
      int32_t gradient = (int32_t)((x * (uint64_t)kMaxSample / (kWidth - 1) +
                                    y * (uint64_t)kMaxSample / (kHeight - 1)) /
                                   2);
      for(uint16_t compno = 0; compno < kNumComps; ++compno)
        planes[compno][(size_t)y * kWidth + x] = clampSample(gradient + compno * kComponentOffset);
    }
  }
  return planes;
}

Planes makeNoiseRectangleSource()
{
  Planes planes = makeGradient();
  uint32_t state = kNoiseSeed;
  for(uint32_t y = kNoiseOriginY; y < kHeight; ++y)
    for(uint32_t x = kNoiseOriginX; x < kWidth; ++x)
      for(uint16_t compno = 0; compno < kNumComps; ++compno)
        planes[compno][(size_t)y * kWidth + x] =
            (int32_t)(xorshift32(state) & (uint32_t)kMaxSample);

  return planes;
}

Planes makeFaintNoiseSource()
{
  Planes planes = makeGradient();
  uint32_t state = kNoiseSeed;
  const uint32_t span = (uint32_t)(2 * kFaintNoiseAmplitude + 1);
  for(uint32_t y = 0; y < kHeight; ++y)
    for(uint32_t x = 0; x < kWidth; ++x)
      for(uint16_t compno = 0; compno < kNumComps; ++compno)
      {
        int32_t offset = (int32_t)(xorshift32(state) % span) - kFaintNoiseAmplitude;
        auto& sample = planes[compno][(size_t)y * kWidth + x];
        sample = clampSample(sample + offset);
      }

  return planes;
}

Planes makeHardEdgedShapes()
{
  Planes planes(kNumComps, std::vector<int32_t>((size_t)kWidth * kHeight));
  for(uint16_t compno = 0; compno < kNumComps; ++compno)
  {
    double centreX = kWidth * (0.3 + 0.2 * compno);
    double centreY = kHeight * (0.6 - 0.15 * compno);
    double radius = kWidth * (0.18 + 0.05 * compno);
    int32_t background = (int32_t)(kHardEdgeMaxSample * (0.15 + 0.1 * compno));
    for(uint32_t y = 0; y < kHeight; ++y)
    {
      for(uint32_t x = 0; x < kWidth; ++x)
      {
        double dx = x - centreX;
        double dy = y - centreY;
        bool insideDisc = dx * dx + dy * dy < radius * radius;
        bool insideBar = y > kHeight * 0.1 && y < kHeight * 0.2 && x > kWidth * 0.1 * (compno + 1);
        planes[compno][(size_t)y * kWidth + x] =
            insideDisc || insideBar ? kHardEdgeMaxSample - background : background;
      }
    }
  }

  uint32_t state = kNoiseSeed;
  const uint32_t span = (uint32_t)(2 * kHardEdgeGrainAmplitude + 1);
  for(uint32_t y = 0; y < kHeight; ++y)
    for(uint32_t x = 0; x < kWidth; ++x)
      for(uint16_t compno = 0; compno < kNumComps; ++compno)
      {
        int32_t grain = (int32_t)(xorshift32(state) % span) - kHardEdgeGrainAmplitude;
        auto& sample = planes[compno][(size_t)y * kWidth + x];
        sample = clampSample(sample + grain, kHardEdgeMaxSample);
      }

  return planes;
}

grk_image* makeImage(const Source& source)
{
  auto components = std::make_unique<grk_image_comp[]>(kNumComps);
  for(uint16_t i = 0; i < kNumComps; ++i)
  {
    auto c = &components[i];
    c->w = kWidth;
    c->h = kHeight;
    c->dx = 1;
    c->dy = 1;
    c->prec = source.precision;
    c->sgnd = false;
  }
  auto image = grk_image_new(kNumComps, components.get(), GRK_CLRSPC_SRGB, true);
  if(!image)
    return nullptr;
  for(uint16_t compno = 0; compno < kNumComps; ++compno)
  {
    auto comp = image->comps + compno;
    auto data = (int32_t*)comp->data;
    for(uint32_t y = 0; y < kHeight; ++y)
    {
      for(uint32_t x = 0; x < kWidth; ++x)
        data[x] = source.planes[compno][(size_t)y * kWidth + x];
      data += comp->stride;
    }
  }
  return image;
}

struct RoundTrip
{
  uint64_t codeStreamLength = 0;
  uint16_t slopeThreshold = 0;
  Planes decoded;
};

Planes capture(grk_image* image)
{
  Planes planes(kNumComps, std::vector<int32_t>((size_t)kWidth * kHeight));
  for(uint16_t compno = 0; compno < kNumComps; ++compno)
  {
    auto comp = image->comps + compno;
    for(uint32_t y = 0; y < kHeight; ++y)
    {
      for(uint32_t x = 0; x < kWidth; ++x)
      {
        size_t index = (size_t)y * comp->stride + x;
        planes[compno][(size_t)y * kWidth + x] = comp->data_type == GRK_INT_16
                                                     ? ((int16_t*)comp->data)[index]
                                                     : ((int32_t*)comp->data)[index];
      }
    }
  }
  return planes;
}

RoundTrip roundTrip(const Source& source, double compressionRatio, bool progressiveRateControl,
                    uint16_t slopeHint = 0, uint8_t quantStepShift = 0,
                    double rateControlTolerance = 0.0)
{
  RoundTrip result;
  auto image = makeImage(source);
  if(!image)
  {
    std::fprintf(stderr, "could not build the source image\n");
    return result;
  }

  grk_cparameters parameters;
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.irreversible = true;
  parameters.allocation_by_rate_distortion = true;
  parameters.numlayers = 1;
  parameters.layer_rate[0] = compressionRatio;
  parameters.progressive_rate_control = progressiveRateControl;
  parameters.rate_control_slope_hint = slopeHint;
  parameters.quant_step_shift = quantStepShift;
  parameters.rate_control_tolerance = rateControlTolerance;

  std::vector<uint8_t> stream((size_t)kWidth * kHeight * kNumComps + 4096);
  grk_stream_params streamParams = {};
  streamParams.buf = stream.data();
  streamParams.buf_len = stream.size();

  auto compressor = grk_compress_init(&streamParams, &parameters, image);
  uint64_t length = compressor ? grk_compress(compressor, nullptr) : 0;
  if(length)
    result.slopeThreshold = grk_compress_get_slope_threshold(compressor);
  grk_object_unref(compressor);
  grk_object_unref(&image->obj);
  if(!length)
  {
    std::fprintf(stderr, "grk_compress failed with progressive rate control %s\n",
                 progressiveRateControl ? "on" : "off");
    return result;
  }

  grk_decompress_parameters decompressParameters = {};
  grk_stream_params decodeStream = {};
  decodeStream.buf = stream.data();
  decodeStream.buf_len = length;
  auto decompressor = grk_decompress_init(&decodeStream, &decompressParameters);
  grk_header_info header = {};
  if(!decompressor || !grk_decompress_read_header(decompressor, &header) ||
     !grk_decompress(decompressor, nullptr))
  {
    std::fprintf(stderr, "grk_decompress failed with progressive rate control %s\n",
                 progressiveRateControl ? "on" : "off");
    grk_object_unref(decompressor);
    return result;
  }

  result.decoded = capture(grk_decompress_get_image(decompressor));
  result.codeStreamLength = length;
  grk_object_unref(decompressor);
  return result;
}

double peakSignalToNoiseRatio(const Source& source, const Planes& decoded)
{
  double squaredError = 0;
  size_t count = 0;
  for(uint16_t compno = 0; compno < kNumComps; ++compno)
  {
    for(size_t i = 0; i < source.planes[compno].size(); ++i)
    {
      double difference = (double)source.planes[compno][i] - (double)decoded[compno][i];
      squaredError += difference * difference;
      ++count;
    }
  }
  if(squaredError == 0)
    return INFINITY;

  double peak = (double)((1 << source.precision) - 1);
  double meanSquaredError = squaredError / (double)count;
  return 10.0 * std::log10(peak * peak / meanSquaredError);
}

void check(const char* name, const Source& source, double compressionRatio,
           const RoundTrip& reference, const RoundTrip& candidate,
           double toleranceDecibels = kPsnrToleranceDecibels)
{
  if(candidate.codeStreamLength == 0)
  {
    ++g_failures;
    std::fprintf(stderr, "FAIL %s: compression or decompression failed\n", name);
    return;
  }

  uint64_t targetBytes =
      (uint64_t)((double)kWidth * kHeight * kNumComps * source.precision / 8.0 / compressionRatio);
  if(candidate.codeStreamLength > targetBytes)
  {
    ++g_failures;
    std::fprintf(stderr, "FAIL %s: the code stream is %llu bytes, over the %llu byte target\n",
                 name, (unsigned long long)candidate.codeStreamLength,
                 (unsigned long long)targetBytes);
  }

  double referencePsnr = peakSignalToNoiseRatio(source, reference.decoded);
  double candidatePsnr = peakSignalToNoiseRatio(source, candidate.decoded);
  if(referencePsnr - candidatePsnr > toleranceDecibels)
  {
    ++g_failures;
    std::fprintf(stderr, "FAIL %s: PSNR dropped from %.3f dB to %.3f dB, more than %.2f dB\n", name,
                 referencePsnr, candidatePsnr, toleranceDecibels);
  }

  std::printf("%s: %llu bytes %.3f dB, reference %.3f dB, target %llu bytes, threshold %u\n", name,
              (unsigned long long)candidate.codeStreamLength, candidatePsnr, referencePsnr,
              (unsigned long long)targetBytes, (unsigned)candidate.slopeThreshold);
}

} // namespace

int main()
{
  grk_initialize(nullptr, 0, nullptr);

  Source noiseRectangle{makeNoiseRectangleSource(), kPrecision};
  Source faintNoise{makeFaintNoiseSource(), kPrecision};
  Source hardEdgedShapes{makeHardEdgedShapes(), kHardEdgePrecision};

  auto rectangleReference = roundTrip(noiseRectangle, kNoiseRectangleRatio, false);
  auto rectangleProgressive = roundTrip(noiseRectangle, kNoiseRectangleRatio, true);
  check("noise rectangle", noiseRectangle, kNoiseRectangleRatio, rectangleReference,
        rectangleProgressive);

  auto faintReference = roundTrip(faintNoise, kFaintNoiseRatio, false);
  auto faintProgressive = roundTrip(faintNoise, kFaintNoiseRatio, true);
  check("faint noise", faintNoise, kFaintNoiseRatio, faintReference, faintProgressive);

  uint16_t hint = rectangleProgressive.slopeThreshold;
  if(hint == 0)
  {
    ++g_failures;
    std::fprintf(stderr, "FAIL: the encoder reported no rate control slope threshold\n");
  }
  else
  {
    auto hinted = roundTrip(noiseRectangle, kNoiseRectangleRatio, true, hint);
    check("noise rectangle, own hint", noiseRectangle, kNoiseRectangleRatio, rectangleReference,
          hinted);

    // a scene cut: the hint belongs to a completely different image
    auto foreignHinted = roundTrip(faintNoise, kFaintNoiseRatio, true, hint);
    check("faint noise, foreign hint", faintNoise, kFaintNoiseRatio, faintReference, foreignHinted);

    auto coarseQuant =
        roundTrip(noiseRectangle, kNoiseRectangleRatio, false, hint, kQuantStepShift);
    check("noise rectangle, coarse quantization", noiseRectangle, kNoiseRectangleRatio,
          rectangleReference, coarseQuant, kQuantStepShiftToleranceDecibels);
    if(coarseQuant.codeStreamLength == rectangleReference.codeStreamLength)
    {
      ++g_failures;
      std::fprintf(stderr,
                   "FAIL noise rectangle, coarse quantization: the shift changed nothing\n");
    }
  }

  // without a hint the shift must not touch the output
  auto unhintedShift = roundTrip(noiseRectangle, kNoiseRectangleRatio, false, 0, kQuantStepShift);
  check("noise rectangle, shift without hint", noiseRectangle, kNoiseRectangleRatio,
        rectangleReference, unhintedShift, 0.0);
  if(unhintedShift.codeStreamLength != rectangleReference.codeStreamLength)
  {
    ++g_failures;
    std::fprintf(stderr, "FAIL noise rectangle, shift without hint: %llu bytes, reference %llu\n",
                 (unsigned long long)unhintedShift.codeStreamLength,
                 (unsigned long long)rectangleReference.codeStreamLength);
  }

  auto toleranced =
      roundTrip(noiseRectangle, kNoiseRectangleRatio, false, 0, 0, kRateControlTolerance);
  check("noise rectangle, rate control tolerance", noiseRectangle, kNoiseRectangleRatio,
        rectangleReference, toleranced);

  auto hardEdgeReference = roundTrip(hardEdgedShapes, kHardEdgeRatio, false);
  auto hardEdgeProgressive = roundTrip(hardEdgedShapes, kHardEdgeRatio, true);
  check("hard edged shapes", hardEdgedShapes, kHardEdgeRatio, hardEdgeReference,
        hardEdgeProgressive);

  grk_deinitialize();
  return g_failures == 0 ? 0 : 1;
}
