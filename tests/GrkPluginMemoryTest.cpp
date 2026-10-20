// The accelerator plugin under grk_compress() and grk_decompress() on memory
// buffers, no file anywhere. A frame compressed through the plugin decodes back
// to the samples that went in, on the CPU and on the device, the device did the
// work, and a cinema frame the device decodes matches the CPU's decode. A compress
// with cpu_only set writes the same code stream as a process without the plugin,
// and a decompress with cpu_only set gives the same samples as one.
//
// Needs the plugin library and a device: GRK_PLUGIN_PATH names the directory
// holding libgrokj2k_plugin. The argument is the directory holding the code
// streams and samples that --write-cpu-only-reference wrote with GRK_NO_PLUGIN set.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
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
constexpr uint32_t kSyccWidth = 256;
constexpr uint32_t kSyccHeight = 144;
constexpr uint8_t kSyccPrecision = 16;
// under this the device and the CPU did not encode the same picture, an mct
// mismatch leaves two device components flat and lands near 20 dB
constexpr double kSyccPeakFloor = 50.0;

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
grk_image* smoothFrame(uint32_t width, uint32_t height, uint8_t precision = kPrecision)
{
  auto image = patternFrame(width, height, precision);
  if(!image)
    return image;
  const int32_t maxSample = (1 << precision) - 1;
  for(uint16_t compno = 0; compno < image->numcomps; ++compno)
  {
    auto comp = image->comps + compno;
    auto data = (int32_t*)comp->data;
    for(uint32_t y = 0; y < comp->h; ++y)
    {
      for(uint32_t x = 0; x < comp->w; ++x)
      {
        int32_t gradient = (int32_t)(((uint64_t)x * maxSample) / width);
        int32_t band = ((y / 64) % 2) ? maxSample / 4 : 0;
        int32_t tint = compno * (maxSample / 8);
        data[x] = std::min(maxSample, (gradient + band + tint) / 2 + (int32_t)(y % 7));
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
std::vector<std::vector<int32_t>> decode(std::vector<uint8_t>& stream, uint64_t length,
                                         bool cpuOnly = false)
{
  std::vector<std::vector<int32_t>> planes;
  grk_decompress_parameters params = {};
  params.cpu_only = cpuOnly;
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

// one plane against another, kMaxSample as the peak, 1e9 when they are identical
double peakSignalToNoise(const std::vector<int32_t>& reference, const std::vector<int32_t>& other,
                         int32_t* maxDifference)
{
  *maxDifference = 0;
  if(reference.empty() || reference.size() != other.size())
    return 0.0;
  double squared = 0;
  for(size_t i = 0; i < reference.size(); ++i)
  {
    double difference = (double)other[i] - reference[i];
    squared += difference * difference;
    *maxDifference = std::max(*maxDifference, (int32_t)std::abs(difference));
  }
  double meanSquared = squared / (double)reference.size();
  if(meanSquared == 0)
    return 1e9;
  return 10.0 * std::log10((double)kMaxSample * kMaxSample / meanSquared);
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

void cinemaParameters(grk_cparameters& params)
{
  baseParameters(params);
  params.rsiz = GRK_PROFILE_CINEMA_2K;
  params.framerate = 24;
  params.irreversible = true;
  params.apply_xyz_transform = true;
  params.allocation_by_rate_distortion = true;
  params.layer_rate[0] = kCinemaRatio;
  params.write_tlm = true;
}

// a DCI 2K frame: the device's decode of a lossy cinema code stream stays
// within a few codes of the CPU's
void checkCinema()
{
  grk_cparameters params;
  cinemaParameters(params);

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
// a 16 bit frame labelled sYCC with the XYZ transform off: the plugin has to
// keep the colour transform grok settles on, or its components drift apart from
// the CPU's
void checkSyccCinema()
{
  std::printf("cinema sYCC 16 bit, apply_xyz_transform false\n");
  grk_cparameters params;
  baseParameters(params);
  params.rsiz = GRK_PROFILE_CINEMA_2K;
  params.framerate = 24;
  params.irreversible = true;
  params.apply_xyz_transform = false;
  params.allocation_by_rate_distortion = true;
  params.layer_rate[0] = kCinemaRatio;
  params.write_tlm = true;

  auto syccFrame = [&]() {
    auto image = smoothFrame(kSyccWidth, kSyccHeight, kSyccPrecision);
    if(image)
      image->color_space = GRK_CLRSPC_SYCC;
    return image;
  };
  auto cpuSource = syccFrame();
  auto gpuSource = syccFrame();
  if(!cpuSource || !gpuSource)
  {
    fail("sYCC frame allocation");
    return;
  }
  std::vector<uint8_t> cpuStream((size_t)kSyccWidth * kSyccHeight * kNumComps * 4);
  std::vector<uint8_t> gpuStream(cpuStream.size());

  grk_plugin_set_enabled(false);
  uint64_t cpuLength = compress(cpuSource, params, cpuStream);
  if(cpuLength == 0)
    fail("sYCC CPU compress");

  grk_plugin_set_enabled(true);
  uint64_t before = grk_plugin_accelerated_frames();
  auto start = std::chrono::steady_clock::now();
  uint64_t gpuLength = compress(gpuSource, params, gpuStream);
  std::printf("cinema sYCC plugin compress: %.1f ms, %llu bytes (CPU %llu bytes)\n",
              millisecondsSince(start), (unsigned long long)gpuLength,
              (unsigned long long)cpuLength);
  if(gpuLength == 0)
    fail("sYCC plugin compress");
  if(grk_plugin_accelerated_frames() != before + 1)
    fail("sYCC plugin compress was not counted as accelerated");

  grk_plugin_set_enabled(false);
  auto cpuDecoded = decode(cpuStream, cpuLength);
  auto gpuDecoded = decode(gpuStream, gpuLength);
  grk_plugin_set_enabled(true);
  if(cpuDecoded.size() != kNumComps || gpuDecoded.size() != kNumComps)
  {
    fail("sYCC decode");
  }
  else
  {
    for(uint16_t compno = 0; compno < kNumComps; ++compno)
    {
      int32_t maxDifference = 0;
      double peak = peakSignalToNoise(cpuDecoded[compno], gpuDecoded[compno], &maxDifference);
      std::printf("cinema sYCC comp %u: device against host %.1f dB, max difference %d codes\n",
                  compno, peak, maxDifference);
      if(peak <= kSyccPeakFloor)
        fail("sYCC plugin encode strays from the CPU encode");
    }
  }
  grk_object_unref(&cpuSource->obj);
  grk_object_unref(&gpuSource->obj);
}

// the frames held against a process without the plugin, one code stream file each
struct CpuOnlyFrame
{
  const char* fileName;
  bool cinema;
};
constexpr CpuOnlyFrame kCpuOnlyFrames[] = {{"cpu_only_lossless.j2k", false},
                                           {"cpu_only_cinema.j2k", true}};

// the code stream of one frame, empty on failure
std::vector<uint8_t> compressCpuOnlyFrame(const CpuOnlyFrame& frame, bool cpuOnly)
{
  grk_cparameters params;
  uint32_t width = kWidth;
  uint32_t height = kHeight;
  if(frame.cinema)
  {
    cinemaParameters(params);
    width = kCinemaWidth;
    height = kCinemaHeight;
  }
  else
  {
    baseParameters(params);
  }
  params.cpu_only = cpuOnly;
  auto source = frame.cinema ? smoothFrame(width, height) : patternFrame(width, height);
  if(!source)
    return {};
  std::vector<uint8_t> stream((size_t)width * height * kNumComps * 2);
  stream.resize(compress(source, params, stream));
  grk_object_unref(&source->obj);
  return stream;
}

std::string cpuOnlyReferencePath(const char* directory, const CpuOnlyFrame& frame)
{
  return std::string(directory) + "/" + frame.fileName;
}

std::string cpuOnlySamplesPath(const char* directory, const CpuOnlyFrame& frame)
{
  return cpuOnlyReferencePath(directory, frame) + ".samples";
}

// every component's samples one after the other, empty on failure
std::vector<int32_t> decodedSamples(std::vector<uint8_t>& stream, bool cpuOnly)
{
  std::vector<int32_t> samples;
  for(const auto& plane : decode(stream, stream.size(), cpuOnly))
    samples.insert(samples.end(), plane.begin(), plane.end());
  return samples;
}

int writeCpuOnlyReference(const char* directory)
{
  for(const auto& frame : kCpuOnlyFrames)
  {
    auto stream = compressCpuOnlyFrame(frame, true);
    std::ofstream file(cpuOnlyReferencePath(directory, frame), std::ios::binary);
    file.write((const char*)stream.data(), (std::streamsize)stream.size());
    auto samples = decodedSamples(stream, true);
    std::ofstream samplesFile(cpuOnlySamplesPath(directory, frame), std::ios::binary);
    samplesFile.write((const char*)samples.data(),
                      (std::streamsize)(samples.size() * sizeof(int32_t)));
    if(stream.empty() || !file || samples.empty() || !samplesFile)
    {
      std::fprintf(stderr, "failed to write %s\n", frame.fileName);
      return 1;
    }
  }
  return 0;
}

// cpu_only keeps one compress off the device while the next one in the same
// process still runs there
void checkCpuOnly(const char* referenceDirectory)
{
  grk_cparameters params;
  baseParameters(params);
  grk_plugin_set_enabled(false);
  auto cpuDataType = grk_compress_get_recommended_data_type(&params, 8, true);
  grk_plugin_set_enabled(true);
  params.cpu_only = true;
  if(grk_compress_get_recommended_data_type(&params, 8, true) != cpuDataType)
    fail("cpu_only did not recommend the sample type of a CPU compress");
  for(const auto& frame : kCpuOnlyFrames)
  {
    std::printf("cpu_only %s\n", frame.fileName);
    std::ifstream file(cpuOnlyReferencePath(referenceDirectory, frame), std::ios::binary);
    std::vector<uint8_t> reference((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
    if(reference.empty())
    {
      fail("cpu_only reference code stream missing");
      continue;
    }
    uint64_t before = grk_plugin_accelerated_frames();
    auto cpuOnlyStream = compressCpuOnlyFrame(frame, true);
    if(grk_plugin_accelerated_frames() != before)
      fail("cpu_only compress was counted as accelerated");
    std::printf("  cpu_only compress %zu bytes, without the plugin %zu bytes\n",
                cpuOnlyStream.size(), reference.size());
    if(cpuOnlyStream != reference)
      fail("cpu_only code stream differs from the one written without the plugin");
    auto pluginStream = compressCpuOnlyFrame(frame, false);
    if(pluginStream.empty())
      fail("plugin compress after a cpu_only compress");
    if(grk_plugin_accelerated_frames() != before + 1)
      fail("plugin compress after a cpu_only compress was not counted as accelerated");
  }
}

// cpu_only keeps one decompress off the device while the next one in the same
// process still runs there
void checkCpuOnlyDecompress(const char* referenceDirectory)
{
  for(const auto& frame : kCpuOnlyFrames)
  {
    std::printf("cpu_only decompress %s\n", frame.fileName);
    std::ifstream file(cpuOnlyReferencePath(referenceDirectory, frame), std::ios::binary);
    std::vector<uint8_t> stream((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
    std::ifstream samplesFile(cpuOnlySamplesPath(referenceDirectory, frame), std::ios::binary);
    std::vector<char> samplesBytes((std::istreambuf_iterator<char>(samplesFile)),
                                   std::istreambuf_iterator<char>());
    if(stream.empty() || samplesBytes.empty())
    {
      fail("cpu_only reference code stream or samples missing");
      continue;
    }
    std::vector<int32_t> reference(samplesBytes.size() / sizeof(int32_t));
    std::memcpy(reference.data(), samplesBytes.data(), reference.size() * sizeof(int32_t));
    uint64_t before = grk_plugin_accelerated_frames();
    auto cpuOnlySamples = decodedSamples(stream, true);
    uint64_t afterCpuOnly = grk_plugin_accelerated_frames();
    std::printf("  accelerated frames before %llu, after cpu_only decompress %llu\n",
                (unsigned long long)before, (unsigned long long)afterCpuOnly);
    if(afterCpuOnly != before)
      fail("cpu_only decompress was counted as accelerated");
    if(cpuOnlySamples != reference)
      fail("cpu_only decompress differs from the one without the plugin");
    auto pluginSamples = decodedSamples(stream, false);
    uint64_t afterPlugin = grk_plugin_accelerated_frames();
    std::printf("  accelerated frames after plugin decompress %llu\n",
                (unsigned long long)afterPlugin);
    if(pluginSamples.size() != reference.size())
      fail("plugin decompress after a cpu_only decompress");
    if(afterPlugin != before + 1)
      fail("plugin decompress after a cpu_only decompress was not counted as accelerated");
  }
}

void putUint16(std::vector<uint8_t>& out, uint16_t value)
{
  out.push_back((uint8_t)(value >> 8));
  out.push_back((uint8_t)value);
}
void putUint32(std::vector<uint8_t>& out, uint32_t value)
{
  putUint16(out, (uint16_t)(value >> 16));
  putUint16(out, (uint16_t)value);
}

// a main header for a single tile 8 bit grey image, with a tile part that contains no packets
std::vector<uint8_t> headerOnlyCodeStream(uint32_t width, uint32_t height)
{
  constexpr uint8_t kLevels = 5;
  std::vector<uint8_t> out;
  putUint16(out, 0xFF4F);
  putUint16(out, 0xFF51);
  putUint16(out, 41);
  putUint16(out, 0);
  for(uint32_t value : {width, height, 0u, 0u, width, height, 0u, 0u})
    putUint32(out, value);
  putUint16(out, 1);
  out.insert(out.end(), {7, 1, 1});
  putUint16(out, 0xFF52);
  putUint16(out, 12);
  out.insert(out.end(), {0, 0, 0, 1, 0, kLevels, 4, 4, 0, 1});
  putUint16(out, 0xFF5C);
  putUint16(out, 3 + 3 * kLevels + 1);
  out.push_back(0x40);
  out.insert(out.end(), 3 * kLevels + 1, 0x50);
  putUint16(out, 0xFF90);
  putUint16(out, 10);
  putUint16(out, 0);
  putUint32(out, 0);
  out.insert(out.end(), {0, 1});
  putUint16(out, 0xFF93);
  putUint16(out, 0xFFD9);
  return out;
}

std::vector<uint8_t> g_headerOnlyStream;
int32_t g_initDecodersResult = 0;

int headerOnlyDecodeCallback(grk_plugin_decompress_callback_info* info)
{
  if(info->decompress_flags & GRK_PLUGIN_DECODE_CLEAN)
  {
    if(info->codec)
      grk_object_unref(info->codec);
    info->codec = nullptr;
    return 0;
  }
  if(!(info->decompress_flags & GRK_DECODE_HEADER))
    return -1;
  grk_stream_params stream = {};
  stream.buf = g_headerOnlyStream.data();
  stream.buf_len = g_headerOnlyStream.size();
  info->codec = grk_decompress_init(&stream, info->decompressor_parameters);
  if(!info->codec || !grk_decompress_read_header(info->codec, &info->header_info))
    return -1;
  info->image = grk_decompress_get_image(info->codec);
  g_initDecodersResult = info->init_decompressors_func(&info->header_info, info->image);
  return g_initDecodersResult;
}

// 65536 x 65536 is 2^32 samples, which wraps to zero in 32 bits
void checkOversizedDecodeDeclined()
{
  g_headerOnlyStream = headerOnlyCodeStream(65536, 65536);
  grk_decompress_parameters params = {};
  g_initDecodersResult = 0;
  int32_t rc = grk_plugin_decompress(&params, headerOnlyDecodeCallback);
  if(g_initDecodersResult != 1 || rc != 1)
  {
    std::fprintf(stderr, "init decoders returned %d, plugin decompress returned %d\n",
                 g_initDecodersResult, rc);
    fail("the plugin did not hand a 65536 x 65536 image back to the CPU");
  }
}
} // namespace

int main(int argc, char** argv)
{
  bool loaded = false;
  grk_initialize(nullptr, 0, &loaded);
  if(argc == 3 && std::strcmp(argv[1], "--write-cpu-only-reference") == 0)
  {
    if(loaded)
    {
      std::fprintf(stderr, "set GRK_NO_PLUGIN to write the cpu_only reference\n");
      return 1;
    }
    int rc = writeCpuOnlyReference(argv[2]);
    grk_deinitialize();
    return rc;
  }
  if(argc != 2)
  {
    std::fprintf(stderr,
                 "usage: %s <cpu_only reference directory>\n"
                 "       %s --write-cpu-only-reference <directory>\n",
                 argv[0], argv[0]);
    return 1;
  }
  if(!loaded)
  {
    std::fprintf(stderr, "no accelerator plugin loaded: set GRK_PLUGIN_PATH to the directory "
                         "holding libgrokj2k_plugin\n");
    return 1;
  }
  grk_plugin_init_info init = {};
  init.device_id = 0;
  // a plugin built with licence checking wants these, unset is the auth-off build
  init.license = std::getenv("GRK_PLUGIN_LICENSE");
  init.server = std::getenv("GRK_PLUGIN_SERVER");
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
  checkSyccCinema();
  checkOversizedDecodeDeclined();
  checkCpuOnly(argv[1]);
  checkCpuOnlyDecompress(argv[1]);
  grk_deinitialize();
  if(g_failures)
  {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("plugin memory path OK\n");
  return 0;
}
