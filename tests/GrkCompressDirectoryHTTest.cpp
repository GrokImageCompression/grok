#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
constexpr uint32_t kNumFrames = 6;
constexpr uint16_t kComponentCounts[] = {3, 1};
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

int32_t sampleAt(uint32_t x, uint32_t y, uint16_t compno, uint32_t frame)
{
  constexpr int32_t maxSample = (1 << kPrecision) - 1;
  return (int32_t)((x * 7 + y * 3 + compno * 501 + frame * 37 + (x * y) % 97) & maxSample);
}

std::string frameName(uint32_t frame, const char* extension)
{
  char name[32];
  std::snprintf(name, sizeof(name), "frame_%02u.%s", frame, extension);
  return name;
}

// grk_decompress turns these into the TIFF frames the directory encode reads
bool writeFrame(uint16_t numComps, uint32_t frame, const std::filesystem::path& path)
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
  for(uint16_t compno = 0; compno < image->numcomps; ++compno)
  {
    auto comp = image->comps + compno;
    auto data = (int32_t*)comp->data;
    for(uint32_t y = 0; y < comp->h; ++y, data += comp->stride)
      for(uint32_t x = 0; x < comp->w; ++x)
        data[x] = sampleAt(x, y, compno, frame);
  }
  grk_cparameters params;
  grk_compress_set_default_params(&params);
  params.cod_format = GRK_FMT_J2K;
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
  std::ofstream file(path, std::ios::binary);
  file.write((const char*)buffer.data(), (std::streamsize)length);
  return (bool)file;
}

std::vector<uint8_t> readFile(const std::filesystem::path& path)
{
  std::ifstream file(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(file),
                              std::istreambuf_iterator<char>());
}

// the stream is HTJ2K and decodes to the frame it was made from
void checkStream(const std::string& label, std::vector<uint8_t>& stream, uint16_t numComps,
                 uint32_t frame)
{
  grk_decompress_parameters params = {};
  grk_stream_params streamParams = {};
  streamParams.buf = stream.data();
  streamParams.buf_len = stream.size();
  auto codec = grk_decompress_init(&streamParams, &params);
  grk_header_info header = {};
  if(!codec || !grk_decompress_read_header(codec, &header) || !grk_decompress(codec, nullptr))
  {
    fail(label + ": decompress");
    grk_object_unref(codec);
    return;
  }
  if(!(header.cblk_sty & GRK_CBLKSTY_HT_ONLY) || !(header.rsiz & GRK_JPH_RSIZ_FLAG))
    fail(label + ": not an HTJ2K code stream");
  auto image = grk_decompress_get_image(codec);
  if(!image || image->numcomps != numComps)
  {
    fail(label + ": decoded component count");
    grk_object_unref(codec);
    return;
  }
  for(uint16_t compno = 0; compno < numComps; ++compno)
  {
    auto comp = image->comps + compno;
    bool isInt16 = comp->data_type == GRK_INT_16;
    if(comp->w != kWidth || comp->h != kHeight || (!isInt16 && comp->data_type != GRK_INT_32))
    {
      fail(label + ": decoded component shape");
      break;
    }
    bool equal = true;
    for(uint32_t y = 0; y < kHeight && equal; ++y)
    {
      for(uint32_t x = 0; x < kWidth && equal; ++x)
      {
        size_t index = (size_t)y * comp->stride + x;
        int32_t sample =
            isInt16 ? ((const int16_t*)comp->data)[index] : ((const int32_t*)comp->data)[index];
        equal = sample == sampleAt(x, y, compno, frame);
      }
    }
    if(!equal)
    {
      fail(label + ": decode is not the source");
      break;
    }
  }
  grk_object_unref(codec);
}

int run(const std::string& command)
{
  return std::system((command + kDiscardOutput).c_str());
}

// a directory encode to JPH writes HTJ2K .jph files, the CPU's bytes under the extra arguments
void checkDirectory(const std::string& compressPath, const std::string& decompressPath,
                    const std::string& extraArguments, const std::filesystem::path& root,
                    uint16_t numComps)
{
  auto shapeName = std::to_string(numComps) + " components";
  auto shapeRoot = root / std::to_string(numComps);
  auto j2kDir = shapeRoot / "j2k";
  auto tifDir = shapeRoot / "tif";
  auto referenceDir = shapeRoot / "reference";
  auto testDir = shapeRoot / "test";
  for(auto& dir : {j2kDir, tifDir, referenceDir, testDir})
    std::filesystem::create_directories(dir);
  for(uint32_t frame = 0; frame < kNumFrames; ++frame)
  {
    if(!writeFrame(numComps, frame, j2kDir / frameName(frame, "j2k")))
    {
      fail(shapeName + ": could not write frame " + std::to_string(frame));
      return;
    }
  }
  if(run("\"" + decompressPath + "\" -y \"" + j2kDir.string() + "\" -a \"" + tifDir.string() +
         "\" -O tif") != 0)
  {
    fail(shapeName + ": grk_decompress to TIFF");
    return;
  }
  auto compress = "\"" + compressPath + "\" -y \"" + tifDir.string() + "\" -O JPH -M 64 -a ";
  if(run(compress + "\"" + referenceDir.string() + "\" -G -2") != 0)
    fail(shapeName + ": directory encode on the CPU returned non zero");
  if(run(compress + "\"" + testDir.string() + "\" " + extraArguments) != 0)
    fail(shapeName + ": directory encode with \"" + extraArguments + "\" returned non zero");
  for(uint32_t frame = 0; frame < kNumFrames; ++frame)
  {
    // the frames keep their .j2k through grk_decompress, the output name drops only the .tif
    auto name = frameName(frame, "j2k.jph");
    auto label = shapeName + ", " + name;
    if(!std::filesystem::exists(referenceDir / name) || !std::filesystem::exists(testDir / name))
    {
      fail(label + ": missing");
      continue;
    }
    auto reference = readFile(referenceDir / name);
    auto test = readFile(testDir / name);
    if(test != reference)
      fail(label + ": differs with \"" + extraArguments + "\"");
    checkStream(label, reference, numComps, frame);
  }
}

} // namespace

int main(int argc, char** argv)
{
  if(argc < 3)
  {
    std::fprintf(stderr, "usage: %s <grk_compress> <grk_decompress> [compress arguments]\n",
                 argv[0]);
    return 1;
  }
  std::string extraArguments;
  for(int i = 3; i < argc; ++i)
    extraArguments += std::string(i > 3 ? " " : "") + argv[i];
  auto root = std::filesystem::current_path() / "compress_directory_ht_test";
  std::filesystem::remove_all(root);
  grk_initialize(nullptr, 0, nullptr);
  for(auto numComps : kComponentCounts)
    checkDirectory(argv[1], argv[2], extraArguments, root, numComps);
  grk_deinitialize();
  if(g_failures)
  {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  std::filesystem::remove_all(root);
  return 0;
}
