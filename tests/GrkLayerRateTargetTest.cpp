// The last quality layer must reach its rate, not stop near the previous
// layer's budget: with several layers and close final rates (9,8 or 11,8) the
// code stream lands within a few percent under the last layer's budget, the
// same as a single layer at that rate. The bytes of earlier layers do not grow
// with this layer's pass count, so an estimate that scales them with it stops
// the search far below the budget (issue #443).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <vector>
#include "grok.h"

namespace
{
constexpr uint32_t kWidth = 1024;
constexpr uint32_t kHeight = 1024;
constexpr uint32_t kNumComps = 3;
constexpr uint64_t kRawBytes = (uint64_t)kWidth * kHeight * kNumComps;
// how far under the last layer's budget a stream may land
constexpr double kMaxShortfall = 0.03;

uint32_t xorshift32(uint32_t& state)
{
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

// smooth gradients with noise whose strength varies across the image, so the
// code blocks spread over a wide range of slopes
grk_image* makeImage(void)
{
  grk_image_comp components[kNumComps] = {};
  for(auto& c : components)
  {
    c.w = kWidth;
    c.h = kHeight;
    c.dx = 1;
    c.dy = 1;
    c.prec = 8;
    c.sgnd = false;
  }
  auto image = grk_image_new(kNumComps, components, GRK_CLRSPC_SRGB, true);
  uint32_t state = 0x9E3779B9;
  for(uint16_t compno = 0; compno < kNumComps; ++compno)
  {
    auto comp = image->comps + compno;
    auto data = (int32_t*)comp->data;
    for(uint32_t y = 0; y < kHeight; ++y)
    {
      for(uint32_t x = 0; x < kWidth; ++x)
      {
        double smooth = 128.0 + 60.0 * std::sin((x + 40.0 * compno) / 37.0) *
                                    std::cos((y - 25.0 * compno) / 53.0);
        double amplitude = 40.0 * (double)x / kWidth * (double)y / kHeight;
        double noise = ((double)(xorshift32(state) & 0xFF) - 127.5) / 127.5 * amplitude;
        int32_t v = (int32_t)std::lround(smooth + noise);
        data[(size_t)y * comp->stride + x] = v < 0 ? 0 : (v > 255 ? 255 : v);
      }
    }
  }
  return image;
}

// returns the code stream length, 0 on failure
uint64_t compress(std::initializer_list<double> rates, bool tiled)
{
  auto image = makeImage();
  grk_cparameters params;
  grk_compress_set_default_params(&params);
  params.cod_format = GRK_FMT_J2K;
  params.irreversible = true;
  params.allocation_by_rate_distortion = true;
  params.numlayers = 0;
  for(double rate : rates)
    params.layer_rate[params.numlayers++] = rate;
  if(tiled)
  {
    params.tile_size_on = true;
    params.t_width = 512;
    params.t_height = 512;
    params.prog_order = GRK_RPCL;
  }

  std::vector<uint8_t> stream(kRawBytes + (1u << 20));
  grk_stream_params streamParams = {};
  streamParams.buf = stream.data();
  streamParams.buf_len = stream.size();
  auto codec = grk_compress_init(&streamParams, &params, image);
  uint64_t length = codec ? grk_compress(codec, nullptr) : 0;
  grk_object_unref(codec);
  grk_object_unref(&image->obj);
  return length;
}
} // namespace

int main()
{
  grk_initialize(nullptr, 0, nullptr);
  int failures = 0;
  const std::initializer_list<double> rateSets[] = {
      {8},
      {16, 8},
      {9, 8},
      {11, 8},
      {362, 256, 181, 128, 90, 64, 45, 32, 22, 16, 11, 8},
  };
  for(bool tiled : {false, true})
  {
    for(auto& rates : rateSets)
    {
      char label[128];
      int pos = std::snprintf(label, sizeof(label), "%s -r ", tiled ? "tiled" : "untiled");
      for(auto it = rates.begin(); it != rates.end(); ++it)
        pos += std::snprintf(label + pos, sizeof(label) - (size_t)pos, "%s%g",
                             it == rates.begin() ? "" : ",", *it);
      double lastRate = *(rates.end() - 1);
      uint64_t budget = (uint64_t)std::ceil((double)kRawBytes / lastRate);
      uint64_t length = compress(rates, tiled);
      std::printf("%s: %llu bytes against a %llu byte budget\n", label,
                  (unsigned long long)length, (unsigned long long)budget);
      if(length == 0)
      {
        std::fprintf(stderr, "FAIL: %s: compress failed\n", label);
        ++failures;
      }
      else if(length > budget)
      {
        std::fprintf(stderr, "FAIL: %s: %llu bytes is over budget\n", label,
                     (unsigned long long)length);
        ++failures;
      }
      else if((double)length < (double)budget * (1.0 - kMaxShortfall))
      {
        std::fprintf(stderr, "FAIL: %s: %llu bytes is %.1f%% under budget\n", label,
                     (unsigned long long)length,
                     100.0 * (1.0 - (double)length / (double)budget));
        ++failures;
      }
    }
  }
  grk_deinitialize();
  if(failures)
    return 1;
  std::printf("last layer reaches its rate OK\n");
  return 0;
}
