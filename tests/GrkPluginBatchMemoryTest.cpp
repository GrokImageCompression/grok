// The plugin's batch pipeline fed from host memory: frames go in through
// grk_plugin_batch_memory_submit and code streams come back on the plugin's
// threads, no file and no shared memory anywhere.
//
// Needs the plugin library and a device: GRK_PLUGIN_PATH names the directory
// holding libgrokj2k_plugin.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
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
constexpr uint16_t kNumComps = 3;
constexpr uint8_t kPrecision = 12;
constexpr uint8_t kSourcePrecision16 = 16;
constexpr int32_t kMaxSample = (1 << kPrecision) - 1;
constexpr uint32_t kCodeBlock = 32;
constexpr uint8_t kResolutions = 6;
constexpr uint32_t kWidth = 1998;
constexpr uint32_t kHeight = 1080;
constexpr uint32_t kNumFrames = 48;
constexpr double kCinemaRatio = 10.0;
// a lossy 12 bit encode held against the transformed source: the device's 9/7
// pipeline lands within a handful of codes
constexpr int32_t kCinemaEncodeTolerance = 16;
// device and host colour transform held against each other through the same
// encoder: only the transform's own rounding separates them
constexpr int32_t kTransformTolerance = 2;
// the device inverse wavelet and the CPU's land within a code of each other
constexpr int32_t kDeviceDecodeTolerance = 2;

void fail(const char* what)
{
  ++g_failures;
  std::fprintf(stderr, "FAIL: %s\n", what);
}

// picture-like content, so a 10:1 encode has something to work with. Every
// component differs at every pixel and every frame differs from its neighbours,
// so a swapped plane, a bad stride or a misrouted frame cannot pass.
grk_image* patternFrame(uint32_t frameIndex, uint8_t precision)
{
  const int32_t maxSample = (1 << precision) - 1;
  // the same picture at every precision, so a 16 bit frame and its 12 bit twin
  // differ only by the width of the samples
  const int32_t frameStep = 37 * ((maxSample + 1) / (kMaxSample + 1));
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
  for(uint16_t compno = 0; compno < image->numcomps; ++compno)
  {
    auto comp = image->comps + compno;
    auto data = (int32_t*)comp->data;
    for(uint32_t y = 0; y < comp->h; ++y)
    {
      for(uint32_t x = 0; x < comp->w; ++x)
      {
        int32_t gradient = (int32_t)(((uint64_t)x * maxSample) / kWidth);
        int32_t band = ((y / 64) % 2) ? maxSample / 4 : 0;
        int32_t tint = compno * (maxSample / 8);
        int32_t value =
            (gradient + band + tint) / 2 + (int32_t)(y % 7) + (int32_t)(frameIndex * frameStep);
        data[x] = std::min(maxSample, value % (maxSample + 1));
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

// the samples of every component, row major without padding, empty on failure
std::vector<std::vector<int32_t>> decodeOnCpu(const std::vector<uint8_t>& stream)
{
  std::vector<std::vector<int32_t>> planes;
  grk_decompress_parameters params = {};
  grk_stream_params streamParams = {};
  streamParams.buf = const_cast<uint8_t*>(stream.data());
  streamParams.buf_len = stream.size();
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

bool planesEqual(const std::vector<std::vector<int32_t>>& left,
                 const std::vector<std::vector<int32_t>>& right, int32_t tolerance,
                 int32_t* maxDifference)
{
  *maxDifference = 0;
  if(left.empty() || left.size() != right.size())
    return false;
  for(size_t plane = 0; plane < left.size(); ++plane)
  {
    if(left[plane].size() != right[plane].size())
      return false;
    for(size_t i = 0; i < left[plane].size(); ++i)
    {
      int32_t d = std::abs(left[plane][i] - right[plane][i]);
      if(d > *maxDifference)
        *maxDifference = d;
    }
  }
  return *maxDifference <= tolerance;
}

double secondsSince(std::chrono::steady_clock::time_point start)
{
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// the code streams the batch handed back, keyed by the frame identity submitted
// with each frame. Callbacks run on the plugin's threads, so it takes a lock.
struct Collector
{
  std::mutex mutex;
  std::map<size_t, std::vector<uint8_t>> codestreams;
  int emptyStreams = 0;
  int duplicates = 0;
};

void collect(void* user, void* frame, const uint8_t* codestream, size_t length)
{
  auto collector = (Collector*)user;
  std::lock_guard<std::mutex> lock(collector->mutex);
  if(!length)
  {
    ++collector->emptyStreams;
    return;
  }
  auto frameId = (size_t)frame;
  if(collector->codestreams.count(frameId))
    ++collector->duplicates;
  collector->codestreams[frameId] = std::vector<uint8_t>(codestream, codestream + length);
}

// the depths one batch runs at: what the code stream carries, what the caller
// declares its frames to be, and what the submitted frames actually are
struct BatchShape
{
  uint8_t prec;
  uint8_t sourcePrec;
  uint8_t framePrec;
};

// runs kNumFrames pattern frames through one batch. Reports the seconds spent in
// submit and end, so synthesizing the frames does not count against the pipeline.
bool runBatch(grk_cparameters& params, BatchShape shape, Collector& collector, double* seconds,
              bool* xyzOnDevice)
{
  *xyzOnDevice = false;
  grk_plugin_batch_memory_info info = {};
  info.compress_parameters = &params;
  info.width = kWidth;
  info.height = kHeight;
  info.numcomps = kNumComps;
  info.prec = shape.prec;
  info.source_prec = shape.sourcePrec;
  info.callback = collect;
  info.user = &collector;
  info.xyz_on_device = xyzOnDevice;
  int32_t rc = grk_plugin_batch_memory_begin(info);
  if(rc != 0)
  {
    std::fprintf(stderr, "grk_plugin_batch_memory_begin returned %d\n", (int)rc);
    return false;
  }
  double submitSeconds = 0;
  for(uint32_t i = 0; i < kNumFrames; ++i)
  {
    auto frame = patternFrame(i, shape.framePrec);
    if(!frame)
    {
      fail("frame allocation");
      break;
    }
    // the frame identity starts at 1: it travels to the callback as a pointer
    // and a frame numbered 0 would be indistinguishable from no frame at all
    auto submitStart = std::chrono::steady_clock::now();
    bool submitted = grk_plugin_batch_memory_submit(frame, (void*)(size_t)(i + 1));
    submitSeconds += secondsSince(submitStart);
    grk_object_unref(&frame->obj);
    if(!submitted)
    {
      fail("grk_plugin_batch_memory_submit");
      break;
    }
  }
  auto endStart = std::chrono::steady_clock::now();
  bool ended = grk_plugin_batch_memory_end();
  *seconds = submitSeconds + secondsSince(endStart);
  if(!ended)
    fail("grk_plugin_batch_memory_end");
  return ended;
}

void checkCollector(const Collector& collector, const char* what)
{
  if(collector.emptyStreams)
  {
    std::printf("  %d frame(s) came back empty\n", collector.emptyStreams);
    fail(what);
  }
  if(collector.duplicates)
  {
    std::printf("  %d frame identity(s) arrived twice\n", collector.duplicates);
    fail(what);
  }
  if(collector.codestreams.size() != kNumFrames)
  {
    std::printf("  %u of %u frames arrived\n", (unsigned)collector.codestreams.size(), kNumFrames);
    fail(what);
  }
}

// lossless: every frame's code stream decodes to that frame's own samples
void checkLossless()
{
  std::printf("lossless 12 bit, mct, %ux%u, %u frames\n", kWidth, kHeight, kNumFrames);
  grk_cparameters params;
  baseParameters(params);
  params.irreversible = false;

  Collector collector;
  double seconds = 0;
  bool xyzOnDevice = false;
  if(!runBatch(params, {kPrecision, 0, kPrecision}, collector, &seconds, &xyzOnDevice))
  {
    fail("lossless batch");
    return;
  }
  std::printf("lossless batch: %.2f s, %.1f frames per second\n", seconds, kNumFrames / seconds);
  checkCollector(collector, "lossless batch delivery");

  for(const auto& entry : collector.codestreams)
  {
    auto frameIndex = (uint32_t)(entry.first - 1);
    auto source = patternFrame(frameIndex, kPrecision);
    if(!source)
    {
      fail("lossless source frame allocation");
      return;
    }
    int32_t difference = 0;
    auto decoded = decodeOnCpu(entry.second);
    if(!samplesEqual(source, decoded, 0, &difference))
    {
      std::printf("  frame %u: max difference %d\n", frameIndex, difference);
      fail("a lossless batch frame does not decode to its own source");
    }
    grk_object_unref(&source->obj);
  }
  std::printf("lossless batch: all %u frames decode to their own source exactly\n", kNumFrames);
}

// irreversible cinema 2K with the colour transform: every frame's decode stays
// within a few codes of the transformed source
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

void checkCinema()
{
  std::printf("cinema 2K irreversible, xyz transform, %ux%u, %u frames\n", kWidth, kHeight,
              kNumFrames);
  grk_cparameters params;
  cinemaParameters(params);

  Collector collector;
  double seconds = 0;
  bool xyzOnDevice = false;
  if(!runBatch(params, {kPrecision, 0, kPrecision}, collector, &seconds, &xyzOnDevice))
  {
    fail("cinema batch");
    return;
  }
  std::printf("cinema batch: %.2f s, %.1f frames per second, most of it the colour transform "
              "submit runs on the caller's frame\n",
              seconds, kNumFrames / seconds);
  if(xyzOnDevice)
    fail("a 12 bit source has no device colour transform");
  checkCollector(collector, "cinema batch delivery");

  for(const auto& entry : collector.codestreams)
  {
    auto frameIndex = (uint32_t)(entry.first - 1);
    auto expected = patternFrame(frameIndex, kPrecision);
    if(!expected || !grk::applyXYZTransform(expected, kPrecision))
    {
      fail("cinema source frame allocation");
      if(expected)
        grk_object_unref(&expected->obj);
      return;
    }
    int32_t difference = 0;
    auto decoded = decodeOnCpu(entry.second);
    if(!samplesEqual(expected, decoded, kCinemaEncodeTolerance, &difference))
    {
      std::printf("  frame %u: max difference %d codes\n", frameIndex, difference);
      fail("a cinema batch frame strays from the transformed source");
    }
    grk_object_unref(&expected->obj);
  }
  std::printf("cinema batch: all %u frames within %d codes of the transformed source\n", kNumFrames,
              kCinemaEncodeTolerance);
}

// a 16 bit RGB source encoded at 12 bits: the preprocess kernel runs the colour
// transform, so submit hands the frame over untouched. Held against the host
// transform, and against the host transform through the same encoder.
void checkCinemaSource16()
{
  std::printf("cinema 2K irreversible, xyz transform on device, 16 bit source, %ux%u, %u frames\n",
              kWidth, kHeight, kNumFrames);
  grk_cparameters params;
  cinemaParameters(params);

  Collector deviceTransform;
  double seconds = 0;
  bool xyzOnDevice = false;
  if(!runBatch(params, {kPrecision, kSourcePrecision16, kSourcePrecision16}, deviceTransform,
               &seconds, &xyzOnDevice))
  {
    fail("16 bit source cinema batch");
    return;
  }
  std::printf("16 bit source cinema batch: %.2f s, %.1f frames per second\n", seconds,
              kNumFrames / seconds);
  if(!xyzOnDevice)
    fail("a 16 bit RGB source encoded at 12 bits should transform on the device");
  checkCollector(deviceTransform, "16 bit source cinema batch delivery");

  // the same frames declared as a 12 bit source, which sends the transform back
  // to the host and leaves the encoder identical
  grk_cparameters hostParams;
  cinemaParameters(hostParams);
  Collector hostTransform;
  double hostSeconds = 0;
  bool hostXyzOnDevice = false;
  if(!runBatch(hostParams, {kPrecision, 0, kSourcePrecision16}, hostTransform, &hostSeconds,
               &hostXyzOnDevice))
  {
    fail("16 bit source cinema batch with the host transform");
    return;
  }
  std::printf("16 bit source, host transform: %.2f s, %.1f frames per second\n", hostSeconds,
              kNumFrames / hostSeconds);
  if(hostXyzOnDevice)
    fail("a batch declaring a 12 bit source should transform on the host");
  checkCollector(hostTransform, "16 bit source host transform batch delivery");

  int32_t worstTransformDifference = 0;
  for(const auto& entry : deviceTransform.codestreams)
  {
    auto frameIndex = (uint32_t)(entry.first - 1);
    auto expected = patternFrame(frameIndex, kSourcePrecision16);
    if(!expected || !grk::applyXYZTransform(expected, kPrecision))
    {
      fail("16 bit source frame allocation");
      if(expected)
        grk_object_unref(&expected->obj);
      return;
    }
    auto decoded = decodeOnCpu(entry.second);
    int32_t difference = 0;
    if(!samplesEqual(expected, decoded, kCinemaEncodeTolerance, &difference))
    {
      std::printf("  frame %u: max difference %d codes\n", frameIndex, difference);
      fail("a 16 bit source cinema frame strays from the transformed source");
    }
    grk_object_unref(&expected->obj);

    auto host = hostTransform.codestreams.find(entry.first);
    if(host == hostTransform.codestreams.end())
    {
      fail("a device transform frame has no host transform code stream");
      continue;
    }
    if(!planesEqual(decoded, decodeOnCpu(host->second), kTransformTolerance, &difference))
    {
      std::printf("  frame %u: device and host transform differ by %d codes\n", frameIndex,
                  difference);
      fail("the device colour transform strays from the host one");
    }
    if(difference > worstTransformDifference)
      worstTransformDifference = difference;
  }
  std::printf("16 bit source cinema batch: all %u frames within %d codes of the transformed "
              "source, and within %d of the host transform through the same encoder\n",
              kNumFrames, kCinemaEncodeTolerance, worstTransformDifference);
}

// ---------------------------------------------------------------------------
// planar YUV straight from a video decoder
// ---------------------------------------------------------------------------

constexpr uint32_t kYuvWidth = 512;
constexpr uint32_t kYuvHeight = 288;
constexpr uint32_t kYuvNumFrames = 48;
// The reference goes through the 16 bit RGB path and the planes through the YUV
// one, so the kernel's rgb16 has to match the scalar reference at every sample.
// The check asserts every frame's two code streams are identical byte for byte
// and that both decode to the same samples.
// the planes carry a row pitch wider than the picture, so a submit that ignored
// the stride would read the wrong samples
constexpr uint32_t kYuvLumaStridePadding = 13;
constexpr uint32_t kYuvChromaStridePadding = 7;

struct YuvSource
{
  const char* name;
  GRK_SOURCE_FORMAT format;
  GRK_YUV_MATRIX matrix;
  bool matrixIsExplicit;
  bool fullRange;
  uint8_t sourcePrec;
};

// the colour constants the plugin derives from the matrix and the range, so the
// reference and the kernel start from the same numbers
struct YuvColour
{
  float lumaOffset;
  float lumaScale;
  float chromaOffset;
  float chromaScale;
  float redFromCr;
  float greenFromCb;
  float greenFromCr;
  float blueFromCb;
};

YuvColour yuvColour(const YuvSource& source)
{
  YuvColour colour = {};
  if(source.fullRange)
  {
    auto sampleMaximum = (float)((1u << source.sourcePrec) - 1u);
    colour.lumaOffset = 0.0f;
    colour.lumaScale = 1.0f / sampleMaximum;
    colour.chromaOffset = (float)(1u << (source.sourcePrec - 1));
    colour.chromaScale = 1.0f / sampleMaximum;
  }
  else
  {
    auto scale = (float)(1u << (source.sourcePrec - 8));
    colour.lumaOffset = 16.0f * scale;
    colour.lumaScale = 1.0f / (219.0f * scale);
    colour.chromaOffset = 128.0f * scale;
    colour.chromaScale = 1.0f / (224.0f * scale);
  }
  switch(source.matrix)
  {
    case GRK_YUV_BT601:
      colour.redFromCr = 1.402f;
      colour.greenFromCb = -0.344136f;
      colour.greenFromCr = -0.714136f;
      colour.blueFromCb = 1.772f;
      break;
    case GRK_YUV_BT2020:
      colour.redFromCr = 1.4746f;
      colour.greenFromCb = -0.16455f;
      colour.greenFromCr = -0.57135f;
      colour.blueFromCb = 1.8814f;
      break;
    default:
      colour.redFromCr = 1.5748f;
      colour.greenFromCb = -0.1873242f;
      colour.greenFromCr = -0.4681243f;
      colour.blueFromCb = 1.8556f;
      break;
  }
  return colour;
}

// planar Y, Cb and Cr held as 16 bit samples whatever the source depth is, so
// the reference and the submitted planes read the same picture
struct YuvPlanes
{
  uint32_t chromaWidth;
  uint32_t chromaHeight;
  uint32_t lumaStride;
  uint32_t chromaStride;
  std::vector<uint16_t> luma;
  std::vector<uint16_t> cb;
  std::vector<uint16_t> cr;
};

uint32_t pseudoRandom(uint32_t seed)
{
  seed = seed * 1664525u + 1013904223u;
  seed ^= seed >> 15;
  return seed * 2246822519u;
}

YuvPlanes makeYuvPlanes(const YuvSource& source, uint32_t frameIndex)
{
  YuvPlanes planes;
  planes.chromaWidth = (kYuvWidth + 1) / 2;
  planes.chromaHeight =
      source.format == GRK_SOURCE_YUV420P ? (kYuvHeight + 1) / 2 : kYuvHeight;
  planes.lumaStride = kYuvWidth + kYuvLumaStridePadding;
  planes.chromaStride = planes.chromaWidth + kYuvChromaStridePadding;
  planes.luma.assign((size_t)planes.lumaStride * kYuvHeight, 0);
  planes.cb.assign((size_t)planes.chromaStride * planes.chromaHeight, 0);
  planes.cr.assign((size_t)planes.chromaStride * planes.chromaHeight, 0);

  auto scale = (int32_t)(1u << (source.sourcePrec - 8));
  int32_t lumaLow = 16 * scale;
  int32_t lumaHigh = 235 * scale;
  int32_t chromaLow = 16 * scale;
  int32_t chromaHigh = 240 * scale;
  for(uint32_t y = 0; y < kYuvHeight; ++y)
  {
    for(uint32_t x = 0; x < kYuvWidth; ++x)
    {
      int32_t gradient =
          lumaLow + (int32_t)((uint64_t)x * (lumaHigh - lumaLow) / (kYuvWidth - 1));
      int32_t band = ((y / 32) % 2) ? (lumaHigh - lumaLow) / 8 : 0;
      int32_t jitter = (int32_t)(pseudoRandom(x * 7919u + y * 104729u + frameIndex * 15485863u) %
                                 (uint32_t)(24 * scale));
      int32_t value = gradient + band + jitter + (int32_t)frameIndex * scale;
      planes.luma[(size_t)y * planes.lumaStride + x] =
          (uint16_t)std::min(lumaHigh, std::max(lumaLow, value));
    }
  }
  for(uint32_t y = 0; y < planes.chromaHeight; ++y)
  {
    for(uint32_t x = 0; x < planes.chromaWidth; ++x)
    {
      // Cb runs down the picture and Cr across it, so swapping the two planes
      // cannot pass
      int32_t blueGradient = chromaLow + (int32_t)((uint64_t)y * (chromaHigh - chromaLow) /
                                                   (planes.chromaHeight - 1));
      int32_t redGradient = chromaLow + (int32_t)((uint64_t)x * (chromaHigh - chromaLow) /
                                                  (planes.chromaWidth - 1));
      int32_t blueJitter =
          (int32_t)(pseudoRandom(x * 2654435761u + y * 40503u + frameIndex * 6700417u) %
                    (uint32_t)(16 * scale));
      int32_t redJitter =
          (int32_t)(pseudoRandom(x * 374761393u + y * 668265263u + frameIndex * 2971215073u) %
                    (uint32_t)(16 * scale));
      planes.cb[(size_t)y * planes.chromaStride + x] =
          (uint16_t)std::min(chromaHigh, std::max(chromaLow, blueGradient + blueJitter));
      planes.cr[(size_t)y * planes.chromaStride + x] =
          (uint16_t)std::min(chromaHigh, std::max(chromaLow, redGradient + redJitter));
    }
  }
  return planes;
}

// chroma sample i is co-sited with luma column 2i, so an even column takes i and
// an odd one the average of i and i+1, clamped at the right edge
float chromaColumnTap(const uint16_t* row, uint32_t column, uint32_t chromaWidth)
{
  uint32_t left = column >> 1;
  float sample = (float)row[left];
  if((column & 1) == 0)
    return sample;
  uint32_t right = (left + 1 < chromaWidth) ? left + 1 : chromaWidth - 1;
  return 0.5f * (sample + (float)row[right]);
}

void referenceChroma(const YuvPlanes& planes, bool chromaIsHalfHeight, uint32_t column,
                     uint32_t row, float* cb, float* cr)
{
  auto rowOf = [&planes](const std::vector<uint16_t>& plane, uint32_t index) {
    return plane.data() + (size_t)index * planes.chromaStride;
  };
  if(chromaIsHalfHeight)
  {
    uint32_t own = row >> 1;
    uint32_t leaning = (row & 1) ? own + 1 : (own ? own - 1 : 0);
    if(leaning >= planes.chromaHeight)
      leaning = planes.chromaHeight - 1;
    *cb = 0.75f * chromaColumnTap(rowOf(planes.cb, own), column, planes.chromaWidth) +
          0.25f * chromaColumnTap(rowOf(planes.cb, leaning), column, planes.chromaWidth);
    *cr = 0.75f * chromaColumnTap(rowOf(planes.cr, own), column, planes.chromaWidth) +
          0.25f * chromaColumnTap(rowOf(planes.cr, leaning), column, planes.chromaWidth);
  }
  else
  {
    *cb = chromaColumnTap(rowOf(planes.cb, row), column, planes.chromaWidth);
    *cr = chromaColumnTap(rowOf(planes.cr, row), column, planes.chromaWidth);
  }
}

uint32_t toRgb16(float value)
{
  if(value <= 0.0f)
    return 0u;
  if(value >= 1.0f)
    return 65535u;
  return (uint32_t)(value * 65535.0f + 0.5f);
}

// the same siting, filter and matrix the kernel runs, as a scalar 16 bit planar
// RGB image the existing RGB batch path can encode
grk_image* referenceRgbFrame(const YuvPlanes& planes, const YuvSource& source)
{
  auto colour = yuvColour(source);
  bool chromaIsHalfHeight = source.format == GRK_SOURCE_YUV420P;
  auto components = std::make_unique<grk_image_comp[]>(kNumComps);
  for(uint32_t i = 0; i < kNumComps; ++i)
  {
    auto c = &components[i];
    c->w = kYuvWidth;
    c->h = kYuvHeight;
    c->dx = 1;
    c->dy = 1;
    c->prec = kSourcePrecision16;
    c->sgnd = false;
  }
  auto image = grk_image_new(kNumComps, components.get(), GRK_CLRSPC_SRGB, true);
  if(!image)
    return nullptr;
  auto red = (int32_t*)image->comps[0].data;
  auto green = (int32_t*)image->comps[1].data;
  auto blue = (int32_t*)image->comps[2].data;
  for(uint32_t y = 0; y < kYuvHeight; ++y)
  {
    for(uint32_t x = 0; x < kYuvWidth; ++x)
    {
      float luma = (float)planes.luma[(size_t)y * planes.lumaStride + x] - colour.lumaOffset;
      luma *= colour.lumaScale;
      float cb = 0.0f;
      float cr = 0.0f;
      referenceChroma(planes, chromaIsHalfHeight, x, y, &cb, &cr);
      cb = (cb - colour.chromaOffset) * colour.chromaScale;
      cr = (cr - colour.chromaOffset) * colour.chromaScale;
      red[x] = (int32_t)toRgb16(luma + colour.redFromCr * cr);
      green[x] = (int32_t)toRgb16(luma + colour.greenFromCb * cb + colour.greenFromCr * cr);
      blue[x] = (int32_t)toRgb16(luma + colour.blueFromCb * cb);
    }
    red += image->comps[0].stride;
    green += image->comps[1].stride;
    blue += image->comps[2].stride;
  }
  return image;
}

// the planes as the caller holds them: one byte per sample at 8 bits and a
// little endian 16 bit container at 10
struct YuvFrameBuffers
{
  std::vector<uint8_t> luma;
  std::vector<uint8_t> cb;
  std::vector<uint8_t> cr;
};

void copyPlane(const std::vector<uint16_t>& source, std::vector<uint8_t>& target,
               size_t containerBytes)
{
  target.resize(source.size() * containerBytes);
  for(size_t i = 0; i < source.size(); ++i)
  {
    if(containerBytes == 1)
    {
      target[i] = (uint8_t)source[i];
    }
    else
    {
      target[2 * i] = (uint8_t)(source[i] & 0xFF);
      target[2 * i + 1] = (uint8_t)(source[i] >> 8);
    }
  }
}

YuvFrameBuffers toFrameBuffers(const YuvPlanes& planes, uint8_t sourcePrec)
{
  size_t containerBytes = sourcePrec > 8 ? 2 : 1;
  YuvFrameBuffers buffers;
  copyPlane(planes.luma, buffers.luma, containerBytes);
  copyPlane(planes.cb, buffers.cb, containerBytes);
  copyPlane(planes.cr, buffers.cr, containerBytes);
  return buffers;
}

grk_image* yuvFrameImage(const YuvPlanes& planes, const YuvSource& source,
                         YuvFrameBuffers& buffers)
{
  auto components = std::make_unique<grk_image_comp[]>(kNumComps);
  for(uint32_t i = 0; i < kNumComps; ++i)
  {
    auto c = &components[i];
    bool isChroma = i > 0;
    c->w = isChroma ? planes.chromaWidth : kYuvWidth;
    c->h = isChroma ? planes.chromaHeight : kYuvHeight;
    c->dx = isChroma ? 2 : 1;
    c->dy = (isChroma && source.format == GRK_SOURCE_YUV420P) ? 2 : 1;
    c->prec = source.sourcePrec;
    c->sgnd = false;
    c->data_type = source.sourcePrec > 8 ? GRK_INT_16 : GRK_INT_8;
  }
  auto image = grk_image_new(kNumComps, components.get(), GRK_CLRSPC_SYCC, false);
  if(!image)
    return nullptr;
  image->comps[0].data = buffers.luma.data();
  image->comps[0].stride = planes.lumaStride;
  image->comps[1].data = buffers.cb.data();
  image->comps[1].stride = planes.chromaStride;
  image->comps[2].data = buffers.cr.data();
  image->comps[2].stride = planes.chromaStride;
  return image;
}

void yuvCinemaParameters(grk_cparameters& params)
{
  grk_compress_set_default_params(&params);
  params.cod_format = GRK_FMT_J2K;
  params.numresolution = kResolutions;
  params.cblockw_init = kCodeBlock;
  params.cblockh_init = kCodeBlock;
  params.numlayers = 1;
  params.mct = 1;
  params.prog_order = GRK_CPRL;
  params.rsiz = GRK_PROFILE_CINEMA_2K;
  params.framerate = 24;
  params.irreversible = true;
  params.apply_xyz_transform = true;
  params.allocation_by_rate_distortion = true;
  params.layer_rate[0] = kCinemaRatio;
  params.write_tlm = true;
}

// the reference RGB frames through the existing 16 bit path
bool runReferenceBatch(const YuvSource& source, Collector& collector)
{
  grk_cparameters params;
  yuvCinemaParameters(params);
  bool xyzOnDevice = false;
  grk_plugin_batch_memory_info info = {};
  info.compress_parameters = &params;
  info.width = kYuvWidth;
  info.height = kYuvHeight;
  info.numcomps = kNumComps;
  info.prec = kPrecision;
  info.source_prec = kSourcePrecision16;
  info.callback = collect;
  info.user = &collector;
  info.xyz_on_device = &xyzOnDevice;
  int32_t rc = grk_plugin_batch_memory_begin(info);
  if(rc != 0)
  {
    std::fprintf(stderr, "reference batch begin returned %d\n", (int)rc);
    return false;
  }
  if(!xyzOnDevice)
    fail("the 16 bit reference batch should transform on the device");
  for(uint32_t i = 0; i < kYuvNumFrames; ++i)
  {
    auto planes = makeYuvPlanes(source, i);
    auto frame = referenceRgbFrame(planes, source);
    if(!frame)
    {
      fail("reference frame allocation");
      break;
    }
    bool submitted = grk_plugin_batch_memory_submit(frame, (void*)(size_t)(i + 1));
    grk_object_unref(&frame->obj);
    if(!submitted)
    {
      fail("reference frame submit");
      break;
    }
  }
  return grk_plugin_batch_memory_end();
}

// the same pictures as planes, converted on the device
bool runYuvBatch(const YuvSource& source, Collector& collector, double* seconds)
{
  grk_cparameters params;
  yuvCinemaParameters(params);
  grk_plugin_batch_memory_info info = {};
  info.compress_parameters = &params;
  info.width = kYuvWidth;
  info.height = kYuvHeight;
  info.numcomps = kNumComps;
  info.prec = kPrecision;
  info.source_prec = source.sourcePrec;
  info.callback = collect;
  info.user = &collector;
  info.source_format = source.format;
  if(source.matrixIsExplicit)
    info.yuv_matrix = source.matrix;
  info.yuv_full_range = source.fullRange;
  int32_t rc = grk_plugin_batch_memory_begin(info);
  if(rc != 0)
  {
    std::fprintf(stderr, "yuv batch begin returned %d\n", (int)rc);
    return false;
  }
  double submitSeconds = 0;
  for(uint32_t i = 0; i < kYuvNumFrames; ++i)
  {
    auto planes = makeYuvPlanes(source, i);
    auto buffers = toFrameBuffers(planes, source.sourcePrec);
    auto frame = yuvFrameImage(planes, source, buffers);
    if(!frame)
    {
      fail("yuv frame allocation");
      break;
    }
    auto submitStart = std::chrono::steady_clock::now();
    bool submitted = grk_plugin_batch_memory_submit(frame, (void*)(size_t)(i + 1));
    submitSeconds += secondsSince(submitStart);
    grk_object_unref(&frame->obj);
    if(!submitted)
    {
      fail("yuv frame submit");
      break;
    }
  }
  auto endStart = std::chrono::steady_clock::now();
  bool ended = grk_plugin_batch_memory_end();
  *seconds = submitSeconds + secondsSince(endStart);
  return ended;
}

// the frames the determinism check runs twice: the 8 bit 4:2:0 pictures turned
// into 16 bit RGB, which is what the reference batch encodes
const YuvSource kDeterminismSource = {
    "planar 8 bit 4:2:0, limited range, BT.709", GRK_SOURCE_YUV420P, GRK_YUV_BT709, true, false, 8};

// reports the frame identities whose two code streams differ, and how
size_t countIdenticalStreams(const Collector& first, const Collector& second, const char* what)
{
  size_t identical = 0;
  for(const auto& entry : first.codestreams)
  {
    auto found = second.codestreams.find(entry.first);
    if(found == second.codestreams.end())
    {
      fail("a frame arrived in only one of the two batches");
      continue;
    }
    if(entry.second == found->second)
    {
      ++identical;
      continue;
    }
    size_t offset = 0;
    size_t shortest = std::min(entry.second.size(), found->second.size());
    while(offset < shortest && entry.second[offset] == found->second[offset])
      ++offset;
    std::printf("  %s frame %u: %zu and %zu bytes, first difference at %zu\n", what,
                (unsigned)(entry.first - 1), entry.second.size(), found->second.size(), offset);
  }
  return identical;
}

// the same frames through the same batch shape twice in one process: an encode
// that is reproducible hands back the same bytes
void checkDeterminism()
{
  std::printf("determinism, same frames encoded twice\n");

  grk_cparameters losslessParams;
  baseParameters(losslessParams);
  losslessParams.irreversible = false;
  Collector losslessFirst;
  Collector losslessSecond;
  double seconds = 0;
  bool xyzOnDevice = false;
  if(!runBatch(losslessParams, {kPrecision, 0, kPrecision}, losslessFirst, &seconds,
               &xyzOnDevice) ||
     !runBatch(losslessParams, {kPrecision, 0, kPrecision}, losslessSecond, &seconds, &xyzOnDevice))
  {
    fail("determinism lossless batch");
    return;
  }
  checkCollector(losslessFirst, "determinism lossless first batch delivery");
  checkCollector(losslessSecond, "determinism lossless second batch delivery");
  auto losslessIdentical = countIdenticalStreams(losslessFirst, losslessSecond, "lossless");
  std::printf("  lossless: %zu of %u code streams identical\n", losslessIdentical, kNumFrames);
  if(losslessIdentical != kNumFrames)
    fail("a lossless batch encoded twice does not give the same code streams");

  Collector cinemaFirst;
  Collector cinemaSecond;
  if(!runReferenceBatch(kDeterminismSource, cinemaFirst) ||
     !runReferenceBatch(kDeterminismSource, cinemaSecond))
  {
    fail("determinism cinema batch");
    return;
  }
  checkCollector(cinemaFirst, "determinism cinema first batch delivery");
  checkCollector(cinemaSecond, "determinism cinema second batch delivery");
  auto cinemaIdentical = countIdenticalStreams(cinemaFirst, cinemaSecond, "cinema");
  std::printf("  rate controlled: %zu of %u code streams identical\n", cinemaIdentical,
              kYuvNumFrames);
  if(cinemaIdentical != kYuvNumFrames)
    fail("a rate controlled batch encoded twice does not give the same code streams");
}

// grk_decompress consults the plugin flag, so this one runs on the device
std::vector<std::vector<int32_t>> decodeOnDevice(const std::vector<uint8_t>& stream)
{
  grk_plugin_set_enabled(true);
  auto planes = decodeOnCpu(stream);
  grk_plugin_set_enabled(false);
  return planes;
}

// a batch replaces the image the plugin's decoder was built against, so a
// device decompress on either side of a batch has to keep working
void checkDecompressBetweenBatches()
{
  std::printf("device decompress on both sides of a batch\n");
  int32_t worst = 0;
  for(int round = 0; round < 2; ++round)
  {
    Collector collector;
    if(!runReferenceBatch(kDeterminismSource, collector))
    {
      fail("decompress between batches: batch");
      return;
    }
    checkCollector(collector, "decompress between batches delivery");
    if(collector.codestreams.empty())
      return;
    const auto& stream = collector.codestreams.begin()->second;
    auto onDevice = decodeOnDevice(stream);
    auto onHost = decodeOnCpu(stream);
    int32_t difference = 0;
    if(!planesEqual(onDevice, onHost, kDeviceDecodeTolerance, &difference))
    {
      std::printf("  round %d: device and host decode differ by %d codes\n", round, difference);
      fail("a device decompress around a batch strays from the host decode");
    }
    if(difference > worst)
      worst = difference;
  }
  std::printf("  two batches, a device decompress after each, max difference %d codes from the "
              "host decode\n",
              worst);
}

void checkYuvSource(const YuvSource& source)
{
  std::printf("%s, %ux%u, %u frames\n", source.name, kYuvWidth, kYuvHeight, kYuvNumFrames);
  Collector reference;
  if(!runReferenceBatch(source, reference))
  {
    fail("reference batch");
    return;
  }
  checkCollector(reference, "reference batch delivery");

  Collector devices;
  double seconds = 0;
  if(!runYuvBatch(source, devices, &seconds))
  {
    fail("yuv batch");
    return;
  }
  std::printf("  yuv batch: %.2f s, %.1f frames per second\n", seconds, kYuvNumFrames / seconds);
  checkCollector(devices, "yuv batch delivery");

  int32_t worst = 0;
  size_t identicalStreams = 0;
  for(const auto& entry : devices.codestreams)
  {
    auto frameIndex = (uint32_t)(entry.first - 1);
    auto found = reference.codestreams.find(entry.first);
    if(found == reference.codestreams.end())
    {
      fail("a yuv frame has no reference code stream");
      continue;
    }
    if(entry.second == found->second)
      ++identicalStreams;
    else
      std::printf("  frame %u: %zu bytes against the reference's %zu\n", frameIndex,
                  entry.second.size(), found->second.size());
    auto decodedYuv = decodeOnCpu(entry.second);
    auto decodedReference = decodeOnCpu(found->second);
    if(decodedYuv.empty() || decodedReference.empty())
    {
      fail("a yuv frame did not decode");
      continue;
    }
    int32_t difference = 0;
    if(!planesEqual(decodedYuv, decodedReference, 0, &difference))
    {
      std::printf("  frame %u: device and scalar reference differ by %d codes\n", frameIndex,
                  difference);
      fail("a yuv frame strays from the scalar reference");
    }
    if(difference > worst)
      worst = difference;
  }
  std::printf("  %zu of %zu code streams identical, max difference %d codes\n", identicalStreams,
              devices.codestreams.size(), worst);
  if(identicalStreams != devices.codestreams.size())
    fail("a yuv frame's code stream differs from the scalar reference's");
}

// ---------------------------------------------------------------------------
// interleaved 16 bit RGB
// ---------------------------------------------------------------------------

// the same picture as patternFrame, interleaved into one 16 bit buffer with a
// row pitch wider than the picture
struct Rgb48Frame
{
  std::vector<uint16_t> samples;
  uint32_t stride;
};

Rgb48Frame makeRgb48Frame(const grk_image* source)
{
  Rgb48Frame frame;
  frame.stride = source->comps[0].w * kNumComps + kYuvLumaStridePadding;
  frame.samples.assign((size_t)frame.stride * source->comps[0].h, 0);
  for(uint32_t y = 0; y < source->comps[0].h; ++y)
  {
    for(uint32_t x = 0; x < source->comps[0].w; ++x)
    {
      for(uint16_t compno = 0; compno < kNumComps; ++compno)
      {
        auto comp = source->comps + compno;
        auto value = ((const int32_t*)comp->data)[(size_t)y * comp->stride + x];
        frame.samples[(size_t)y * frame.stride + x * kNumComps + compno] = (uint16_t)value;
      }
    }
  }
  return frame;
}

grk_image* rgb48Image(const grk_image* source, Rgb48Frame& buffer)
{
  auto components = std::make_unique<grk_image_comp[]>(kNumComps);
  for(uint32_t i = 0; i < kNumComps; ++i)
  {
    auto c = &components[i];
    c->w = source->comps[0].w;
    c->h = source->comps[0].h;
    c->dx = 1;
    c->dy = 1;
    c->prec = source->comps[0].prec;
    c->sgnd = false;
    c->data_type = GRK_INT_16;
  }
  auto image = grk_image_new(kNumComps, components.get(), GRK_CLRSPC_SRGB, false);
  if(!image)
    return nullptr;
  image->comps[0].data = buffer.samples.data();
  image->comps[0].stride = buffer.stride;
  return image;
}

// the planar int32 frames and the same pictures interleaved must reach the
// device as the same samples, so their code streams have to match byte for byte
void checkRgb48(bool applyXyz)
{
  const uint32_t frameCount = 8;
  uint8_t framePrec = applyXyz ? kSourcePrecision16 : kPrecision;
  std::printf("interleaved 16 bit RGB, xyz %s, %ux%u, %u frames\n", applyXyz ? "on" : "off",
              kWidth, kHeight, frameCount);
  grk_cparameters params;
  cinemaParameters(params);
  params.apply_xyz_transform = applyXyz;
  if(!applyXyz)
    params.rsiz = GRK_PROFILE_NONE;

  Collector planar;
  Collector interleaved;
  for(int pass = 0; pass < 2; ++pass)
  {
    grk_cparameters passParams = params;
    grk_plugin_batch_memory_info info = {};
    info.compress_parameters = &passParams;
    info.width = kWidth;
    info.height = kHeight;
    info.numcomps = kNumComps;
    info.prec = kPrecision;
    info.source_prec = framePrec;
    info.callback = collect;
    info.user = pass ? &interleaved : &planar;
    info.source_format = pass ? GRK_SOURCE_RGB48LE : GRK_SOURCE_PLANAR_RGB;
    int32_t rc = grk_plugin_batch_memory_begin(info);
    if(rc != 0)
    {
      std::fprintf(stderr, "rgb48 batch begin returned %d\n", (int)rc);
      fail("rgb48 batch begin");
      return;
    }
    for(uint32_t i = 0; i < frameCount; ++i)
    {
      auto source = patternFrame(i, framePrec);
      if(!source)
      {
        fail("rgb48 source frame allocation");
        break;
      }
      bool submitted = false;
      if(pass)
      {
        auto buffer = makeRgb48Frame(source);
        auto frame = rgb48Image(source, buffer);
        if(frame)
        {
          submitted = grk_plugin_batch_memory_submit(frame, (void*)(size_t)(i + 1));
          grk_object_unref(&frame->obj);
        }
      }
      else
      {
        submitted = grk_plugin_batch_memory_submit(source, (void*)(size_t)(i + 1));
      }
      grk_object_unref(&source->obj);
      if(!submitted)
      {
        fail("rgb48 frame submit");
        break;
      }
    }
    if(!grk_plugin_batch_memory_end())
    {
      fail("rgb48 batch end");
      return;
    }
  }
  if(planar.codestreams.size() != frameCount || interleaved.codestreams.size() != frameCount)
  {
    fail("rgb48 batch delivery");
    return;
  }
  int32_t worst = 0;
  for(const auto& entry : interleaved.codestreams)
  {
    auto found = planar.codestreams.find(entry.first);
    if(found == planar.codestreams.end())
    {
      fail("an interleaved frame has no planar code stream");
      continue;
    }
    int32_t difference = 0;
    if(!planesEqual(decodeOnCpu(entry.second), decodeOnCpu(found->second), 0, &difference))
      fail("an interleaved frame decodes differently from its planar twin");
    if(difference > worst)
      worst = difference;
  }
  std::printf("  interleaved and planar decode to the same samples, max difference %d\n", worst);
}

// a caller finds out whether a frame shape is taken with a begin and an end and
// no frame in between
void checkYuvProbe()
{
  grk_cparameters params;
  yuvCinemaParameters(params);
  Collector collector;
  grk_plugin_batch_memory_info info = {};
  info.compress_parameters = &params;
  info.width = kYuvWidth;
  info.height = kYuvHeight;
  info.numcomps = kNumComps;
  info.prec = kPrecision;
  info.source_prec = 8;
  info.callback = collect;
  info.user = &collector;
  info.source_format = GRK_SOURCE_YUV420P;
  info.yuv_matrix = GRK_YUV_BT709;
  int32_t rc = grk_plugin_batch_memory_begin(info);
  std::printf("yuv probe: grk_plugin_batch_memory_begin returned %d\n", (int)rc);
  if(rc < 0)
  {
    fail("a yuv probe should be taken or declined, not fail");
    return;
  }
  if(rc == 0 && !grk_plugin_batch_memory_end())
    fail("a yuv batch that took no frames should still end");
  if(collector.codestreams.size())
    fail("a yuv probe delivered a code stream");
}

// a source depth the kernels are not instantiated for: the caller is told to
// compress on the CPU
void checkYuvUnsupportedDepth()
{
  grk_cparameters params;
  yuvCinemaParameters(params);
  Collector collector;
  grk_plugin_batch_memory_info info = {};
  info.compress_parameters = &params;
  info.width = kYuvWidth;
  info.height = kYuvHeight;
  info.numcomps = kNumComps;
  info.prec = kPrecision;
  info.source_prec = kSourcePrecision16;
  info.callback = collect;
  info.user = &collector;
  info.source_format = GRK_SOURCE_YUV422P;
  int32_t rc = grk_plugin_batch_memory_begin(info);
  std::printf("16 bit yuv source: grk_plugin_batch_memory_begin returned %d\n", (int)rc);
  if(rc != 1)
    fail("a 16 bit yuv source should return 1");
}

// parameters the plugin does not handle: the caller is told to compress on the CPU
void checkUnsupportedParameters()
{
  grk_cparameters params;
  baseParameters(params);
  params.cblk_sty = 1;

  Collector collector;
  grk_plugin_batch_memory_info info = {};
  info.compress_parameters = &params;
  info.width = kWidth;
  info.height = kHeight;
  info.numcomps = kNumComps;
  info.prec = kPrecision;
  info.callback = collect;
  info.user = &collector;
  int32_t rc = grk_plugin_batch_memory_begin(info);
  std::printf("code block style 1: grk_plugin_batch_memory_begin returned %d\n", (int)rc);
  if(rc != 1)
    fail("a code block style the plugin does not handle should return 1");
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
  // every check decodes on the CPU. The batch API does not consult this flag,
  // so it only steers grk_decompress.
  grk_plugin_set_enabled(false);
  checkLossless();
  checkCinema();
  checkCinemaSource16();
  checkYuvProbe();
  checkYuvUnsupportedDepth();
  checkDeterminism();
  checkDecompressBetweenBatches();
  static const YuvSource yuvSources[] = {
      {"planar 8 bit 4:2:0, limited range, BT.709", GRK_SOURCE_YUV420P, GRK_YUV_BT709, true, false,
       8},
      {"planar 10 bit 4:2:2, limited range, BT.709", GRK_SOURCE_YUV422P, GRK_YUV_BT709, true,
       false, 10},
      {"planar 8 bit 4:2:0, limited range, BT.601 left unset", GRK_SOURCE_YUV420P, GRK_YUV_BT601,
       false, false, 8}};
  for(const auto& source : yuvSources)
    checkYuvSource(source);
  checkRgb48(true);
  checkRgb48(false);
  checkUnsupportedParameters();
  grk_deinitialize();
  if(g_failures)
  {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("plugin in-memory batch OK\n");
  return 0;
}
