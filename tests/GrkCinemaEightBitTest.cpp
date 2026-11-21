// The cinema profiles require 12-bit X'Y'Z' samples. A source handed to the
// compressor with the XYZ transform on must come out as a cinema code stream
// whatever precision it started at: an 8-bit RGB frame is widened to 12 bits
// and a 16-bit one is reduced, and the code stream declares the cinema Rsiz.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include "grok.h"

namespace
{
int g_failures = 0;
constexpr uint32_t kWidth = 64;
constexpr uint32_t kHeight = 36;
constexpr uint32_t kNumComps = 3;
constexpr uint32_t kCinemaPrecision = 12;
constexpr int32_t kCinemaMax = (1 << kCinemaPrecision) - 1;
// a lossy encode of a flat frame at the DCI cap lands within a few codes
constexpr int32_t kTolerance = 16;

// DCI companding: peak white 48 cd/m2 in the 52.37 encoding range
constexpr double kDciCoefficient = 48.0 / 52.37;
constexpr double kDciGamma = 2.6;
// linear Rec.709 RGB to CIE XYZ, D65, first column only (pure red)
constexpr double kRedToX = 0.4124564;
constexpr double kRedToY = 0.2126729;
constexpr double kRedToZ = 0.0193339;

// what a full-scale red pixel encodes to under the DCI transform
int32_t expectedCode(double linearXyz)
{
  return (int32_t)std::lround(std::pow(linearXyz * kDciCoefficient, 1.0 / kDciGamma) * kCinemaMax);
}

grk_image* redFrame(uint8_t precision)
{
  auto components = std::make_unique<grk_image_comp[]>(kNumComps);
  for(uint32_t i = 0; i < kNumComps; ++i)
  {
    auto c = &components[i];
    c->w = kWidth;
    c->h = kHeight;
    c->dx = 1;
    c->dy = 1;
    c->prec = precision;
    c->sgnd = false;
  }
  auto image = grk_image_new(kNumComps, components.get(), GRK_CLRSPC_SRGB, true);
  if(!image)
    return nullptr;
  int32_t fullScale = (1 << precision) - 1;
  for(uint16_t compno = 0; compno < image->numcomps; ++compno)
  {
    auto comp = image->comps + compno;
    auto data = (int32_t*)comp->data;
    int32_t value = compno == 0 ? fullScale : 0;
    for(uint32_t j = 0; j < comp->h; ++j)
    {
      for(uint32_t i = 0; i < comp->w; ++i)
        data[i] = value;
      data += comp->stride;
    }
  }
  return image;
}

uint64_t compressCinema(grk_image* image, uint8_t* outBuf, size_t outBufLen)
{
  grk_cparameters params;
  grk_compress_set_default_params(&params);
  params.cod_format = GRK_FMT_J2K;
  params.rsiz = GRK_PROFILE_CINEMA_2K;
  params.framerate = 24;
  params.apply_xyz_transform = true;
  grk_stream_params stream = {};
  stream.buf = outBuf;
  stream.buf_len = outBufLen;

  uint64_t length = 0;
  auto codec = grk_compress_init(&stream, &params, image);
  if(codec)
    length = grk_compress(codec, nullptr);
  grk_object_unref(codec);
  return length;
}

void fail(const char* what, uint8_t sourcePrecision, int64_t got, int64_t expected)
{
  ++g_failures;
  std::fprintf(stderr, "FAIL %u-bit source: %s is %lld, expected %lld\n", sourcePrecision, what,
               (long long)got, (long long)expected);
}

void checkSource(uint8_t sourcePrecision, uint8_t* buf, size_t bufLen)
{
  auto image = redFrame(sourcePrecision);
  if(!image)
  {
    fail("frame allocation", sourcePrecision, 0, 1);
    return;
  }
  uint64_t length = compressCinema(image, buf, bufLen);
  grk_object_unref(&image->obj);
  if(length == 0)
  {
    fail("compressed length", sourcePrecision, 0, 1);
    return;
  }

  grk_decompress_parameters decompressParams = {};
  grk_stream_params stream = {};
  stream.buf = buf;
  stream.buf_len = length;
  auto codec = grk_decompress_init(&stream, &decompressParams);
  grk_header_info header = {};
  if(!codec || !grk_decompress_read_header(codec, &header))
  {
    fail("header read", sourcePrecision, 0, 1);
    grk_object_unref(codec);
    return;
  }
  if(header.rsiz != GRK_PROFILE_CINEMA_2K)
    fail("Rsiz", sourcePrecision, header.rsiz, GRK_PROFILE_CINEMA_2K);
  for(uint16_t compno = 0; compno < header.header_image.numcomps; ++compno)
  {
    uint8_t prec = header.header_image.comps[compno].prec;
    if(prec != kCinemaPrecision)
      fail("component precision", sourcePrecision, prec, kCinemaPrecision);
  }

  grk_image* decoded = nullptr;
  if(grk_decompress(codec, nullptr))
    decoded = grk_decompress_get_image(codec);
  if(!decoded)
  {
    fail("decode", sourcePrecision, 0, 1);
    grk_object_unref(codec);
    return;
  }
  const int32_t expected[kNumComps] = {expectedCode(kRedToX), expectedCode(kRedToY),
                                       expectedCode(kRedToZ)};
  const char* names[kNumComps] = {"X'", "Y'", "Z'"};
  for(uint16_t compno = 0; compno < kNumComps; ++compno)
  {
    auto comp = decoded->comps + compno;
    auto data = (int32_t*)comp->data;
    int32_t centre = data[(uint64_t)(comp->h / 2) * comp->stride + comp->w / 2];
    if(std::abs(centre - expected[compno]) > kTolerance)
      fail(names[compno], sourcePrecision, centre, expected[compno]);
  }
  grk_object_unref(codec);
}

} // namespace

int main()
{
  grk_initialize(nullptr, 0, nullptr);

  size_t bufLen = (size_t)kNumComps * 2 * kWidth * kHeight + 4096;
  auto buf = std::make_unique<uint8_t[]>(bufLen);
  for(uint8_t sourcePrecision : {(uint8_t)8, (uint8_t)12, (uint8_t)16})
    checkSource(sourcePrecision, buf.get(), bufLen);

  grk_deinitialize();

  if(g_failures == 0)
  {
    std::fprintf(stderr, "GrkCinemaEightBitTest: all tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "GrkCinemaEightBitTest: %d failure(s)\n", g_failures);
  return 1;
}
