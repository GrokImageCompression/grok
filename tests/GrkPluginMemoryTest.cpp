// The accelerator plugin under grk_compress() and grk_decompress() on memory
// buffers, no file anywhere. A frame compressed through the plugin decodes back
// to the samples that went in, on the CPU and on the device, the device did the
// work, and a cinema frame the device decodes matches the CPU's decode.
//
// Needs the plugin library and a device: GRK_PLUGIN_PATH names the directory
// holding libgrokj2k_plugin.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <algorithm>
#include <vector>
#include "grok.h"

namespace grk
{
// exported by libgrokj2k, the same transform grk_compress runs for apply_xyz_transform
GRK_API bool applyXYZTransform(grk_image* image, uint8_t targetPrec);
} // namespace grk

namespace
{
int g_failures = 0;
constexpr uint32_t kNumComps = 3;
constexpr uint8_t kPrecision = 12;
constexpr int32_t kMaxSample = (1 << kPrecision) - 1;
constexpr uint32_t kCodeBlock = 32;
constexpr uint8_t kResolutions = 6;
constexpr uint32_t kWidth = 512;
constexpr uint32_t kHeight = 288;
constexpr uint32_t kCinemaWidth = 1998;
constexpr uint32_t kCinemaHeight = 1080;
constexpr double kCinemaRatio = 10.0;
// the device and the CPU run the irreversible wavelet in different arithmetic
constexpr int32_t kLossyTolerance = 8;
// a lossy 12 bit encode held against the transformed source: the device's 9/7
// pipeline lands within 10 codes, the CPU encoder within 5
constexpr int32_t kCinemaEncodeTolerance = 16;

void fail(const char* what)
{
  ++g_failures;
  std::fprintf(stderr, "FAIL: %s\n", what);
}

// every component differs at every pixel, so a swapped plane or a row read at
// the wrong stride cannot pass
grk_image* patternFrame(uint32_t width, uint32_t height, uint8_t precision = kPrecision)
{
  const int32_t maxSample = (1 << precision) - 1;
  auto components = std::make_unique<grk_image_comp[]>(kNumComps);
  for(uint32_t i = 0; i < kNumComps; ++i)
  {
    auto c = &components[i];
    c->w = width;
    c->h = height;
    c->dx = 1;
    c->dy = 1;
    c->prec = precision;
    c->sgnd = false;
  }
  auto image = grk_image_new(kNumComps, components.get(), GRK_CLRSPC_SRGB, true);
  if(!image)
    return nullptr;
  for(uint16_t compno = 0; compno < image->numcomps; ++compno)
  {
    auto comp = image->comps + compno;
    auto data = (int32_t*)comp->data;
    for(uint32_t y = 0; y < comp->h; ++y)
    {
      for(uint32_t x = 0; x < comp->w; ++x)
      {
        int32_t v = compno == 0   ? (int32_t)(x * 37 + y * 11)
                    : compno == 1 ? (int32_t)(x * 5 + y * 71)
                                  : (int32_t)(x * 13 + y * 29 + 7);
        data[x] = v % (maxSample + 1);
      }
      data += comp->stride;
    }
  }
  return image;
}

// picture-like content: gradients with a few edges, what a real frame costs
grk_image* smoothFrame(uint32_t width, uint32_t height)
{
  auto image = patternFrame(width, height);
  if(!image)
    return image;
  for(uint16_t compno = 0; compno < image->numcomps; ++compno)
  {
    auto comp = image->comps + compno;
    auto data = (int32_t*)comp->data;
    for(uint32_t y = 0; y < comp->h; ++y)
    {
      for(uint32_t x = 0; x < comp->w; ++x)
      {
        int32_t gradient = (int32_t)(((uint64_t)x * kMaxSample) / width);
        int32_t band = ((y / 64) % 2) ? kMaxSample / 4 : 0;
        int32_t tint = compno * (kMaxSample / 8);
        data[x] = std::min(kMaxSample, (gradient + band + tint) / 2 + (int32_t)(y % 7));
      }
      data += comp->stride;
    }
  }
  return image;
}

void baseParameters(grk_cparameters& params)
{
  grk_compress_set_default_params(&params);
  params.cod_format = GRK_FMT_J2K;
  params.numresolution = kResolutions;
  params.cblockw_init = kCodeBlock;
  params.cblockh_init = kCodeBlock;
  params.numlayers = 1;
  params.mct = 1;
  params.prog_order = GRK_CPRL;
}

uint64_t compress(grk_image* image, const grk_cparameters& parameters, std::vector<uint8_t>& out)
{
  grk_cparameters params = parameters;
  grk_stream_params stream = {};
  stream.buf = out.data();
  stream.buf_len = out.size();
  uint64_t length = 0;
  auto codec = grk_compress_init(&stream, &params, image);
  if(codec)
    length = grk_compress(codec, nullptr);
  grk_object_unref(codec);
  return length;
}

// the samples of every component, row major without padding, empty on failure
std::vector<std::vector<int32_t>> decode(std::vector<uint8_t>& stream, uint64_t length)
{
  std::vector<std::vector<int32_t>> planes;
  grk_decompress_parameters params = {};
  grk_stream_params streamParams = {};
  streamParams.buf = stream.data();
  streamParams.buf_len = length;
  auto codec = grk_decompress_init(&streamParams, &params);
  grk_header_info header = {};
  if(!codec || !grk_decompress_read_header(codec, &header) || !grk_decompress(codec, nullptr))
  {
    grk_object_unref(codec);
    return planes;
  }
  auto image = grk_decompress_get_image(codec);
  if(!image)
  {
    grk_object_unref(codec);
    return planes;
  }
  for(uint16_t compno = 0; compno < image->numcomps; ++compno)
  {
    auto comp = image->comps + compno;
    std::vector<int32_t> plane;
    plane.reserve((size_t)comp->w * comp->h);
    for(uint32_t y = 0; y < comp->h; ++y)
    {
      for(uint32_t x = 0; x < comp->w; ++x)
      {
        size_t index = (size_t)y * comp->stride + x;
        switch(comp->data_type)
        {
          case GRK_INT_16:
            plane.push_back(((int16_t*)comp->data)[index]);
            break;
          case GRK_INT_8:
            plane.push_back(((int8_t*)comp->data)[index]);
            break;
          default:
            plane.push_back(((int32_t*)comp->data)[index]);
            break;
        }
      }
    }
    planes.push_back(std::move(plane));
  }
  grk_object_unref(codec);
  return planes;
}

bool samplesEqual(const grk_image* source, const std::vector<std::vector<int32_t>>& planes,
                  int32_t tolerance, int32_t* maxDifference)
{
  *maxDifference = 0;
  if(planes.size() != source->numcomps)
    return false;
  for(uint16_t compno = 0; compno < source->numcomps; ++compno)
  {
    auto comp = source->comps + compno;
    if(planes[compno].size() != (size_t)comp->w * comp->h)
      return false;
    auto data = (const int32_t*)comp->data;
    for(uint32_t y = 0; y < comp->h; ++y)
      for(uint32_t x = 0; x < comp->w; ++x)
      {
        int32_t d =
            std::abs(data[(size_t)y * comp->stride + x] - planes[compno][(size_t)y * comp->w + x]);
        if(d > *maxDifference)
          *maxDifference = d;
      }
  }
  return *maxDifference <= tolerance;
}

double millisecondsSince(std::chrono::steady_clock::time_point start)
{
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
      .count();
}

// lossless: the plugin's code stream decodes to the source exactly, on either side
void checkLossless(uint8_t mct, uint8_t precision)
{
  std::printf("lossless mct=%u precision=%u\n", mct, precision);
  grk_cparameters params;
  baseParameters(params);
  params.irreversible = false;
  params.mct = mct;
  // the reversible transform runs on the caller's image in place
  auto source = patternFrame(kWidth, kHeight, precision);
  auto cpuInput = patternFrame(kWidth, kHeight, precision);
  auto gpuInput = patternFrame(kWidth, kHeight, precision);
  if(!source || !cpuInput || !gpuInput)
  {
    fail("lossless frame allocation");
    return;
  }
  std::vector<uint8_t> cpuStream((size_t)kWidth * kHeight * kNumComps * 2);
  std::vector<uint8_t> gpuStream(cpuStream.size());

  grk_plugin_set_enabled(false);
  uint64_t cpuLength = compress(cpuInput, params, cpuStream);
  if(cpuLength == 0)
    fail("lossless CPU compress");

  grk_plugin_set_enabled(true);
  {
    int32_t firstDifference = 0;
    auto decodedFirst = decode(cpuStream, cpuLength);
    if(!samplesEqual(source, decodedFirst, 0, &firstDifference))
    {
      std::printf(
          "  CPU compress then plugin decode (before any plugin encode): max difference %d\n",
          firstDifference);
      fail("CPU compress then plugin decode is not the source, before any plugin encode");
    }
  }
  uint64_t before = grk_plugin_accelerated_frames();
  auto start = std::chrono::steady_clock::now();
  uint64_t gpuLength = compress(gpuInput, params, gpuStream);
  std::printf("lossless %ux%u plugin compress: %.1f ms, %llu bytes (CPU %llu bytes)\n", kWidth,
              kHeight, millisecondsSince(start), (unsigned long long)gpuLength,
              (unsigned long long)cpuLength);
  if(gpuLength == 0)
    fail("lossless plugin compress");
  if(grk_plugin_accelerated_frames() != before + 1)
    fail("plugin compress was not counted as accelerated");
  int32_t difference = 0;

  start = std::chrono::steady_clock::now();
  auto gpuDecoded = decode(gpuStream, gpuLength);
  std::printf("lossless plugin decode: %.1f ms\n", millisecondsSince(start));
  if(grk_plugin_accelerated_frames() != before + 2)
    fail("plugin decompress was not counted as accelerated");
  if(!samplesEqual(source, gpuDecoded, 0, &difference))
  {
    std::printf("  plugin compress then plugin decode: max difference %d\n", difference);
    fail("plugin compress then plugin decode is not the source");
  }

  grk_plugin_set_enabled(false);
  auto cpuDecoded = decode(gpuStream, gpuLength);
  if(!samplesEqual(source, cpuDecoded, 0, &difference))
  {
    std::printf("  plugin compress then CPU decode: max difference %d\n", difference);
    fail("plugin compress then CPU decode is not the source");
  }
  auto cpuStreamDecodedOnGpu = [&]() {
    grk_plugin_set_enabled(true);
    return decode(cpuStream, cpuLength);
  }();
  if(!samplesEqual(source, cpuStreamDecodedOnGpu, 0, &difference))
  {
    std::printf("  CPU compress then plugin decode: max difference %d\n", difference);
    fail("CPU compress then plugin decode is not the source");
  }
  grk_plugin_set_enabled(true);
  grk_object_unref(&source->obj);
  grk_object_unref(&cpuInput->obj);
  grk_object_unref(&gpuInput->obj);
}

// a DCI 2K frame: the device's decode of a lossy cinema code stream stays
// within a few codes of the CPU's
void checkCinema()
{
  grk_cparameters params;
  baseParameters(params);
  params.rsiz = GRK_PROFILE_CINEMA_2K;
  params.framerate = 24;
  params.irreversible = true;
  params.apply_xyz_transform = true;
  params.allocation_by_rate_distortion = true;
  params.layer_rate[0] = kCinemaRatio;
  params.write_tlm = true;

  std::vector<uint8_t> stream((size_t)kCinemaWidth * kCinemaHeight * kNumComps * 2);
  // the transform runs on the image in place, so each compress gets its own
  auto gpuSource = smoothFrame(kCinemaWidth, kCinemaHeight);
  auto cpuSource = smoothFrame(kCinemaWidth, kCinemaHeight);
  // the picture both encoders are handed after the colour transform
  auto expected = smoothFrame(kCinemaWidth, kCinemaHeight);
  if(!gpuSource || !cpuSource || !expected || !grk::applyXYZTransform(expected, kPrecision))
  {
    fail("cinema frame allocation");
    return;
  }
  grk_plugin_set_enabled(true);
  uint64_t before = grk_plugin_accelerated_frames();
  auto start = std::chrono::steady_clock::now();
  uint64_t length = compress(gpuSource, params, stream);
  std::printf("cinema 2K plugin compress: %.1f ms, %llu bytes\n", millisecondsSince(start),
              (unsigned long long)length);
  // the first frame pays for the device buffers and kernels, the rest is the rate
  for(int repeat = 0; repeat < 4; ++repeat)
  {
    auto again = smoothFrame(kCinemaWidth, kCinemaHeight);
    std::vector<uint8_t> againStream(stream.size());
    start = std::chrono::steady_clock::now();
    uint64_t againLength = compress(again, params, againStream);
    double compressMs = millisecondsSince(start);
    start = std::chrono::steady_clock::now();
    auto againDecoded = decode(againStream, againLength);
    std::printf("cinema 2K plugin frame %d: compress %.1f ms, decode %.1f ms\n", repeat + 2,
                compressMs, millisecondsSince(start));
    grk_object_unref(&again->obj);
  }
  before = grk_plugin_accelerated_frames() - 1;
  if(length == 0)
    fail("cinema plugin compress");
  if(grk_plugin_accelerated_frames() != before + 1)
    fail("cinema plugin compress was not counted as accelerated");

  start = std::chrono::steady_clock::now();
  auto gpuDecoded = decode(stream, length);
  std::printf("cinema 2K plugin decode: %.1f ms\n", millisecondsSince(start));
  grk_plugin_set_enabled(false);
  start = std::chrono::steady_clock::now();
  auto cpuDecoded = decode(stream, length);
  std::printf("cinema 2K CPU decode: %.1f ms\n", millisecondsSince(start));
  grk_plugin_set_enabled(true);
  if(gpuDecoded.size() != kNumComps || cpuDecoded.size() != kNumComps)
  {
    fail("cinema decode");
  }
  else
  {
    int32_t maxDifference = 0;
    for(uint16_t compno = 0; compno < kNumComps; ++compno)
    {
      if(gpuDecoded[compno].size() != cpuDecoded[compno].size())
      {
        fail("cinema plugin and CPU decodes differ in size");
        break;
      }
      for(size_t i = 0; i < gpuDecoded[compno].size(); ++i)
        maxDifference =
            std::max(maxDifference, std::abs(gpuDecoded[compno][i] - cpuDecoded[compno][i]));
    }
    std::printf("cinema 2K plugin decode vs CPU decode: max difference %d codes\n", maxDifference);
    if(maxDifference > kLossyTolerance)
      fail("cinema plugin decode strays from the CPU decode");
  }
  std::vector<uint8_t> cpuStream(stream.size());
  grk_plugin_set_enabled(false);
  start = std::chrono::steady_clock::now();
  uint64_t cpuLength = compress(cpuSource, params, cpuStream);
  std::printf("cinema 2K CPU compress: %.1f ms, %llu bytes\n", millisecondsSince(start),
              (unsigned long long)cpuLength);
  if(cpuLength == 0)
    fail("cinema CPU compress");
  // both decoded on the CPU and held against the transformed source
  auto cpuStreamDecoded = decode(cpuStream, cpuLength);
  auto gpuStreamDecoded = decode(stream, length);
  grk_plugin_set_enabled(true);
  int32_t pluginError = 0;
  int32_t cpuError = 0;
  bool pluginDecoded =
      samplesEqual(expected, gpuStreamDecoded, kCinemaEncodeTolerance, &pluginError);
  samplesEqual(expected, cpuStreamDecoded, kCinemaEncodeTolerance, &cpuError);
  std::printf("cinema 2K against the transformed source, CPU decoded: plugin encode max error %d "
              "codes, CPU encode max error %d codes\n",
              pluginError, cpuError);
  if(gpuStreamDecoded.size() != kNumComps)
    fail("cinema CPU decode of the plugin stream");
  else if(!pluginDecoded)
    fail("the plugin compressed a different picture than it was handed");
  grk_object_unref(&gpuSource->obj);
  grk_object_unref(&cpuSource->obj);
  grk_object_unref(&expected->obj);
}
} // namespace

int main()
{
  bool loaded = false;
  grk_initialize(nullptr, 0, &loaded);
  if(!loaded)
  {
    std::fprintf(stderr, "no accelerator plugin loaded: set GRK_PLUGIN_PATH to the directory "
                         "holding libgrokj2k_plugin\n");
    return 1;
  }
  grk_plugin_init_info init = {};
  init.device_id = 0;
  if(!grk_plugin_init(init))
  {
    std::fprintf(stderr, "the plugin refused device 0\n");
    return 1;
  }
  checkLossless(1, 12);
  checkLossless(0, 12);
  checkLossless(1, 8);
  checkLossless(0, 8);
  checkCinema();
  grk_deinitialize();
  if(g_failures)
  {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("plugin memory path OK\n");
  return 0;
}
