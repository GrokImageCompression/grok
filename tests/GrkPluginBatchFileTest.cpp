#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#include "grok.h"

namespace
{
int g_failures = 0;
constexpr uint8_t kPrecision = 12;
constexpr uint32_t kWidth = 512;
constexpr uint32_t kHeight = 256;
struct BatchShape
{
  uint16_t numComps;
  uint32_t numFrames;
};
// 24 small frames fill every slot of an eight tile cluster, 20 leave four spare
constexpr BatchShape kBatchShapes[] = {{3, 24}, {3, 20}, {1, 24}, {1, 20}};
// with sixteen workers for twenty-odd frames, workers finish their shares at different times
constexpr uint32_t kNumWorkers = 16;
constexpr uint32_t kNumRuns = 3;
#ifdef _WIN32
constexpr const char* kDiscardOutput = " > NUL";
#else
constexpr const char* kDiscardOutput = " > /dev/null";
#endif

void fail(const std::string& what)
{
  ++g_failures;
  std::fprintf(stderr, "FAIL: %s\n", what.c_str());
}

bool writeFrame(uint16_t numComps, uint32_t frameIndex, const std::filesystem::path& path)
{
  auto components = std::make_unique<grk_image_comp[]>(numComps);
  for(uint32_t i = 0; i < numComps; ++i)
  {
    auto c = &components[i];
    c->w = kWidth;
    c->h = kHeight;
    c->dx = 1;
    c->dy = 1;
    c->prec = kPrecision;
    c->sgnd = false;
  }
  auto image = grk_image_new(numComps, components.get(),
                             numComps == 1 ? GRK_CLRSPC_GRAY : GRK_CLRSPC_SRGB, true);
  if(!image)
    return false;
  constexpr int32_t maxSample = (1 << kPrecision) - 1;
  for(uint16_t compno = 0; compno < image->numcomps; ++compno)
  {
    auto comp = image->comps + compno;
    auto data = (int32_t*)comp->data;
    for(uint32_t y = 0; y < comp->h; ++y, data += comp->stride)
      for(uint32_t x = 0; x < comp->w; ++x)
        data[x] = (int32_t)((x * 7 + y * 3 + compno * 501 + frameIndex * 37) & maxSample);
  }
  grk_cparameters params;
  grk_compress_set_default_params(&params);
  params.cod_format = GRK_FMT_J2K;
  params.numresolution = 5;
  params.cblockw_init = 32;
  params.cblockh_init = 32;
  params.numlayers = 1;
  params.mct = numComps == 3;
  std::vector<uint8_t> buffer((size_t)kWidth * kHeight * numComps * 4);
  grk_stream_params stream = {};
  stream.buf = buffer.data();
  stream.buf_len = buffer.size();
  auto codec = grk_compress_init(&stream, &params, image);
  uint64_t length = codec ? grk_compress(codec, nullptr) : 0;
  if(codec)
    grk_object_unref(codec);
  grk_object_unref(&image->obj);
  if(!length)
    return false;
  auto file = std::fopen(path.string().c_str(), "wb");
  if(!file)
    return false;
  bool written = std::fwrite(buffer.data(), 1, length, file) == length;
  return std::fclose(file) == 0 && written;
}

void checkRun(const std::string& label, const std::filesystem::path& outputDir, BatchShape shape)
{
  // 12 bit samples are written as two bytes each
  uintmax_t expectedSize = (uintmax_t)kWidth * kHeight * shape.numComps * 2;
  for(uint32_t frame = 0; frame < shape.numFrames; ++frame)
  {
    char name[32];
    std::snprintf(name, sizeof(name), "frame_%02u.j2k.raw", frame);
    auto path = outputDir / name;
    if(!std::filesystem::exists(path))
      fail(label + ": no " + name);
    else if(std::filesystem::file_size(path) != expectedSize)
      fail(label + ": " + name + " has " + std::to_string(std::filesystem::file_size(path)) +
           " bytes, expected " + std::to_string(expectedSize));
  }
  auto numOutputs = std::distance(std::filesystem::directory_iterator(outputDir),
                                  std::filesystem::directory_iterator());
  if(numOutputs != (std::ptrdiff_t)shape.numFrames)
    fail(label + ": " + std::to_string(numOutputs) + " outputs for " +
         std::to_string(shape.numFrames) + " frames");
}

bool checkBatch(const std::string& decompressPath, const std::filesystem::path& root,
                BatchShape shape)
{
  auto shapeName = std::to_string(shape.numFrames) + " frames of " +
                   std::to_string(shape.numComps) + " components";
  auto inputDir =
      root / ("in_" + std::to_string(shape.numComps) + "_" + std::to_string(shape.numFrames));
  auto outputDir = root / "out";
  std::filesystem::create_directories(inputDir);
  for(uint32_t frame = 0; frame < shape.numFrames; ++frame)
  {
    char name[32];
    std::snprintf(name, sizeof(name), "frame_%02u.j2k", frame);
    if(!writeFrame(shape.numComps, frame, inputDir / name))
    {
      std::fprintf(stderr, "could not write %s\n", name);
      return false;
    }
  }
  for(uint32_t run = 0; run < kNumRuns; ++run)
  {
    std::filesystem::remove_all(outputDir);
    std::filesystem::create_directories(outputDir);
    auto command = "\"" + decompressPath + "\" -y \"" + inputDir.string() + "\" -a \"" +
                   outputDir.string() + "\" -O raw -e 1 -H " + std::to_string(kNumWorkers) +
                   kDiscardOutput;
    auto label = shapeName + ", run " + std::to_string(run);
    int rc = std::system(command.c_str());
    if(rc != 0)
      fail(label + ": grk_decompress returned " + std::to_string(rc));
    checkRun(label, outputDir, shape);
  }
  return true;
}

} // namespace

int main(int argc, char** argv)
{
  if(argc != 2)
  {
    std::fprintf(stderr, "usage: %s <grk_decompress>\n", argv[0]);
    return 1;
  }
  auto root = std::filesystem::current_path() / "plugin_batch_file_test";
  std::filesystem::remove_all(root);
  grk_initialize(nullptr, 0, nullptr);
  for(auto shape : kBatchShapes)
  {
    if(!checkBatch(argv[1], root, shape))
      return 1;
  }
  grk_deinitialize();
  std::filesystem::remove_all(root);
  if(g_failures)
  {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
