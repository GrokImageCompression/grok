// A rate budget the whole code stream fits under must not cost a single code:
// a reversible encode with the PCRD allocator decodes to its source exactly.
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "grok.h"

namespace
{
constexpr uint32_t kWidth = 64;
constexpr uint32_t kHeight = 48;
constexpr int32_t kMaxSample = 4095;

int32_t sampleAt(int component, uint32_t x, uint32_t y)
{
  int32_t values[3] = {(int32_t)((x * 37 + y * 11) % (kMaxSample + 1)),
                       (int32_t)((x * 5 + y * 71) % (kMaxSample + 1)),
                       (int32_t)((x * 13 + y * 29 + 7) % (kMaxSample + 1))};
  return values[component];
}

// decodes the stream and counts the samples that differ from the pattern
size_t decodeAndCountDifferences(std::vector<uint8_t>& stream, uint64_t length)
{
  grk_decompress_parameters params = {};
  grk_stream_params streamParams = {};
  streamParams.buf = stream.data();
  streamParams.buf_len = length;
  auto codec = grk_decompress_init(&streamParams, &params);
  grk_header_info header = {};
  if(!codec || !grk_decompress_read_header(codec, &header) || !grk_decompress(codec, nullptr))
  {
    grk_object_unref(codec);
    return SIZE_MAX;
  }
  auto image = grk_decompress_get_image(codec);
  size_t differences = 0;
  for(uint16_t compno = 0; compno < image->numcomps; ++compno)
  {
    auto comp = image->comps + compno;
    for(uint32_t y = 0; y < comp->h; ++y)
    {
      for(uint32_t x = 0; x < comp->w; ++x)
      {
        size_t index = (size_t)y * comp->stride + x;
        int32_t got = comp->data_type == GRK_INT_16 ? ((int16_t*)comp->data)[index]
                                                    : ((int32_t*)comp->data)[index];
        if(got != sampleAt(compno, x, y))
          ++differences;
      }
    }
  }
  grk_object_unref(codec);
  return differences;
}

// returns the differing sample count, SIZE_MAX on a failed encode or decode
size_t roundTrip(GRK_RATE_CONTROL_ALGORITHM algorithm, double ratio, uint64_t* length)
{
  grk_image_comp components[3] = {};
  for(auto& c : components)
  {
    c.w = kWidth;
    c.h = kHeight;
    c.dx = 1;
    c.dy = 1;
    c.prec = 12;
    c.sgnd = false;
  }
  auto image = grk_image_new(3, components, GRK_CLRSPC_SRGB, true);
  for(int compno = 0; compno < 3; ++compno)
  {
    auto data = (int32_t*)image->comps[compno].data;
    for(uint32_t y = 0; y < kHeight; ++y)
    {
      for(uint32_t x = 0; x < kWidth; ++x)
        data[x] = sampleAt(compno, x, y);
      data += image->comps[compno].stride;
    }
  }
  grk_cparameters params;
  grk_compress_set_default_params(&params);
  params.cod_format = GRK_FMT_J2K;
  params.numresolution = 3;
  params.cblockw_init = 32;
  params.cblockh_init = 32;
  params.numlayers = 1;
  params.mct = 0;
  params.irreversible = false;
  params.allocation_by_rate_distortion = true;
  params.layer_rate[0] = ratio;
  params.rate_control_algorithm = algorithm;

  std::vector<uint8_t> stream((size_t)kWidth * kHeight * 3 * 4);
  grk_stream_params streamParams = {};
  streamParams.buf = stream.data();
  streamParams.buf_len = stream.size();
  auto codec = grk_compress_init(&streamParams, &params, image);
  *length = codec ? grk_compress(codec, nullptr) : 0;
  grk_object_unref(codec);
  grk_object_unref(&image->obj);
  if(!*length)
    return SIZE_MAX;
  return decodeAndCountDifferences(stream, *length);
}
} // namespace

int main()
{
  grk_initialize(nullptr, 0, nullptr);
  int failures = 0;
  const uint64_t rawBytes = (uint64_t)kWidth * kHeight * 3 * 12 / 8;
  const GRK_RATE_CONTROL_ALGORITHM algorithms[] = {GRK_RATE_CONTROL_BISECT,
                                                    GRK_RATE_CONTROL_PCRD_OPT};
  const char* names[] = {"bisect", "pcrd"};
  // the pattern compresses to under a tenth of its raw size, so all of these fit
  const double ratios[] = {1.0, 1.5, 3.0};
  for(int a = 0; a < 2; ++a)
  {
    for(double ratio : ratios)
    {
      uint64_t length = 0;
      size_t differences = roundTrip(algorithms[a], ratio, &length);
      std::printf("%s at %.1f:1: %llu bytes against a %llu byte budget, %zu samples differ\n",
                  names[a], ratio, (unsigned long long)length,
                  (unsigned long long)(rawBytes / ratio), differences);
      if(differences != 0)
      {
        std::fprintf(stderr, "FAIL: %s at %.1f:1 lost samples under a budget the whole stream fits\n",
                     names[a], ratio);
        ++failures;
      }
    }
  }
  grk_deinitialize();
  if(failures)
    return 1;
  std::printf("rate allocation under a non binding budget OK\n");
  return 0;
}
