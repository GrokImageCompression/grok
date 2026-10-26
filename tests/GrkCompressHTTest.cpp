#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#include "grok.h"
#include "grok_codec.h"
#include "GrkRunQuiet.h"

namespace
{
int g_failures = 0;
constexpr uint8_t kPrecision = 12;
constexpr uint32_t kWidth = 512;
constexpr uint32_t kHeight = 256;
constexpr uint32_t kNumFrames = 8;
constexpr uint16_t kComponentCounts[] = {3, 1};

enum class Device
{
  cpu,
  plugin
};

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

grk_image* makeFrame(uint16_t numComps, uint32_t frame, uint8_t chromaSubsampling = 1)
{
  auto components = std::make_unique<grk_image_comp[]>(numComps);
  for(uint32_t i = 0; i < numComps; ++i)
  {
    auto c = &components[i];
    uint8_t subsampling = i == 0 ? 1 : chromaSubsampling;
    c->w = (kWidth + subsampling - 1) / subsampling;
    c->h = (kHeight + subsampling - 1) / subsampling;
    c->dx = subsampling;
    c->dy = subsampling;
    c->prec = kPrecision;
    c->sgnd = false;
  }
  auto image = grk_image_new(numComps, components.get(),
                             numComps == 1 ? GRK_CLRSPC_GRAY : GRK_CLRSPC_SRGB, true);
  if(!image)
    return nullptr;
  for(uint16_t compno = 0; compno < image->numcomps; ++compno)
  {
    auto comp = image->comps + compno;
    auto data = (int32_t*)comp->data;
    for(uint32_t y = 0; y < comp->h; ++y, data += comp->stride)
      for(uint32_t x = 0; x < comp->w; ++x)
        data[x] = sampleAt(x, y, compno, frame);
  }
  return image;
}

bool writeFrame(uint16_t numComps, uint32_t frame, const std::filesystem::path& path)
{
  auto image = makeFrame(numComps, frame);
  if(!image)
    return false;
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
                 uint32_t frame, Device device = Device::cpu)
{
  grk_decompress_parameters params = {};
  params.cpu_only = device == Device::cpu;
  auto before = grk_plugin_accelerated_frames();
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
  if(device == Device::plugin && grk_plugin_accelerated_frames() != before + 1)
    fail(label + ": the decode did not run on the plugin");
}

// lossless HTJ2K of a frame from memory
std::vector<uint8_t> compressFrame(uint16_t numComps, uint32_t frame, Device device,
                                   uint8_t chromaSubsampling = 1)
{
  auto image = makeFrame(numComps, frame, chromaSubsampling);
  if(!image)
    return {};
  grk_cparameters params;
  grk_compress_set_default_params(&params);
  params.cod_format = GRK_FMT_J2K;
  params.numlayers = 1;
  params.irreversible = false;
  params.cblk_sty = GRK_CBLKSTY_HT_ONLY;
  params.mct = numComps == 3 && chromaSubsampling == 1;
  params.cpu_only = device == Device::cpu;
  std::vector<uint8_t> buffer((size_t)kWidth * kHeight * numComps * 4);
  grk_stream_params stream = {};
  stream.buf = buffer.data();
  stream.buf_len = buffer.size();
  auto codec = grk_compress_init(&stream, &params, image);
  uint64_t length = codec ? grk_compress(codec, nullptr) : 0;
  if(codec)
    grk_object_unref(codec);
  grk_object_unref(&image->obj);
  buffer.resize(length);
  return buffer;
}

bool writeTiff(const std::string& decompressPath, uint16_t numComps, uint32_t frame,
               const std::filesystem::path& j2kDir, const std::filesystem::path& tifDir)
{
  auto label = std::to_string(numComps) + " components, frame " + std::to_string(frame);
  auto j2k = j2kDir / frameName(frame, "j2k");
  if(!writeFrame(numComps, frame, j2k))
  {
    fail(label + ": could not write the code stream");
    return false;
  }
  // a directory decode names its outputs differently with and without the plugin
  if(runQuiet("\"" + decompressPath + "\" -i \"" + j2k.string() + "\" -o \"" +
              (tifDir / frameName(frame, "tif")).string() + "\"") != 0)
  {
    fail(label + ": grk_decompress to TIFF");
    return false;
  }
  return true;
}

// grk_compress in this process, returns the frames the plugin coded
uint64_t compress(const std::string& label, const std::vector<std::string>& arguments)
{
  std::vector<const char*> argv = {"grk_compress"};
  for(const auto& argument : arguments)
    argv.push_back(argument.c_str());
  auto before = grk_plugin_accelerated_frames();
  if(grk_codec_compress((int)argv.size(), argv.data(), nullptr, nullptr) != EXIT_SUCCESS)
    fail(label + ": grk_compress returned non zero");
  return grk_plugin_accelerated_frames() - before;
}

// a plugin batch decode of HTJ2K files writes each output under its input file's full name
void checkBatchDecode(const std::string& decompressPath, const std::filesystem::path& shapeRoot,
                      const std::string& shapeName)
{
  auto tifDir = shapeRoot / "tif";
  for(std::string extension : {"jhc", "jph"})
  {
    auto label = shapeName + ", batch decode of " + extension;
    auto streamDir = shapeRoot / (extension + "_streams");
    auto decodeDir = shapeRoot / (extension + "_decoded");
    for(auto& dir : {streamDir, decodeDir})
      std::filesystem::create_directories(dir);
    compress(label + " streams", {"-y", tifDir.string(), "-O", extension, "-M", "64", "-a",
                                  streamDir.string(), "-G", "-2"});
    if(runQuiet("\"" + decompressPath + "\" -y \"" + streamDir.string() + "\" -a \"" +
                decodeDir.string() + "\" -O tif") != 0)
    {
      fail(label + ": grk_decompress returned non zero");
      continue;
    }
    for(uint32_t frame = 0; frame < kNumFrames; ++frame)
    {
      auto decoded = readFile(decodeDir / (frameName(frame, extension.c_str()) + ".tif"));
      if(decoded.empty() || decoded != readFile(tifDir / frameName(frame, "tif")))
        fail(label + ", " + frameName(frame, extension.c_str()) + ": differs from the source");
    }
  }
}

// directory and single file HTJ2K encodes write the CPU's bytes
void checkCompress(const std::string& decompressPath, Device device,
                   const std::filesystem::path& root, uint16_t numComps)
{
  bool usePlugin = device == Device::plugin;
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
    if(!writeTiff(decompressPath, numComps, frame, j2kDir, tifDir))
      return;
  }
  std::string deviceId = usePlugin ? "0" : "-2";
  auto directoryLabel = shapeName + ", directory";
  if(compress(directoryLabel + " on the CPU", {"-y", tifDir.string(), "-O", "JPH", "-M", "64", "-a",
                                               referenceDir.string(), "-G", "-2"}) != 0)
    fail(directoryLabel + ": the CPU encode ran on the plugin");
  auto accelerated = compress(directoryLabel, {"-y", tifDir.string(), "-O", "JPH", "-M", "64", "-a",
                                               testDir.string(), "-G", deviceId});
  if(accelerated != (usePlugin ? kNumFrames : 0))
    fail(directoryLabel + ": " + std::to_string(accelerated) + " frames on the plugin");
  if(usePlugin)
  {
    checkBatchDecode(decompressPath, shapeRoot, shapeName);
    auto repeatDir = shapeRoot / "repeat";
    std::filesystem::create_directories(repeatDir);
    accelerated = compress(directoryLabel + ", two repetitions",
                           {"-y", tifDir.string(), "-O", "JPH", "-M", "64", "-a",
                            repeatDir.string(), "-G", "0", "-e", "2"});
    if(accelerated != 2 * kNumFrames)
      fail(directoryLabel + ", two repetitions: " + std::to_string(accelerated) +
           " frames on the plugin");
    auto firstFrame = readFile(testDir / frameName(0, "jph"));
    checkStream(directoryLabel + ", plugin decode after the batch", firstFrame, numComps, 0,
                Device::plugin);
  }
  for(uint32_t frame = 0; frame < kNumFrames; ++frame)
  {
    auto name = frameName(frame, "jph");
    auto label = shapeName + ", " + name;
    if(!std::filesystem::exists(referenceDir / name) || !std::filesystem::exists(testDir / name))
    {
      fail(label + ": missing");
      continue;
    }
    auto reference = readFile(referenceDir / name);
    auto test = readFile(testDir / name);
    if(test != reference)
      fail(label + ": differs from the CPU");
    checkStream(label, reference, numComps, frame);
  }

  auto tif = (tifDir / frameName(0, "tif")).string();
  auto singleReference = shapeRoot / "single_reference.jhc";
  auto singleTest = shapeRoot / "single_test.jhc";
  auto singleLabel = shapeName + ", single file";
  if(compress(singleLabel + " on the CPU",
              {"-i", tif, "-M", "64", "-o", singleReference.string(), "-G", "-2"}) != 0)
    fail(singleLabel + ": the CPU encode ran on the plugin");
  accelerated =
      compress(singleLabel, {"-i", tif, "-M", "64", "-o", singleTest.string(), "-G", deviceId});
  if(accelerated != (usePlugin ? 1 : 0))
    fail(singleLabel + ": " + std::to_string(accelerated) + " frames on the plugin");
  auto singleBytes = readFile(singleTest);
  if(singleBytes.empty() || singleBytes != readFile(singleReference))
    fail(singleLabel + ": differs from the CPU");
  checkStream(singleLabel, singleBytes, numComps, 0);
}

// a batch whose image has one shape hands the files of the other shape to the CPU
void checkMixedDirectory(const std::filesystem::path& root)
{
  auto mixedDir = root / "mixed";
  auto referenceDir = root / "mixed_reference";
  auto testDir = root / "mixed_test";
  for(auto& dir : {mixedDir, referenceDir, testDir})
    std::filesystem::create_directories(dir);
  std::vector<std::string> names;
  for(auto numComps : kComponentCounts)
  {
    for(uint32_t frame = 0; frame < kNumFrames; ++frame)
    {
      auto name = std::to_string(numComps) + "_" + frameName(frame, "tif");
      std::filesystem::copy_file(root / std::to_string(numComps) / "tif" / frameName(frame, "tif"),
                                 mixedDir / name);
      names.push_back(name.substr(0, name.size() - 3) + "jph");
    }
  }
  compress("mixed directory on the CPU", {"-y", mixedDir.string(), "-O", "JPH", "-M", "64", "-a",
                                          referenceDir.string(), "-G", "-2"});
  compress("mixed directory",
           {"-y", mixedDir.string(), "-O", "JPH", "-M", "64", "-a", testDir.string(), "-G", "0"});
  for(const auto& name : names)
  {
    auto test = readFile(testDir / name);
    if(test.empty() || test != readFile(referenceDir / name))
      fail("mixed directory, " + name + ": differs from the CPU");
  }
}

// a memory encode after a file encode of another shape still writes the CPU's bytes
void checkMemoryEncodeAfterFileEncode(const std::filesystem::path& root)
{
  auto reference = compressFrame(3, 0, Device::cpu);
  auto before = grk_plugin_accelerated_frames();
  if(compressFrame(3, 0, Device::plugin) != reference)
    fail("first memory encode differs from the CPU");
  auto tif = (root / "1" / "tif" / frameName(0, "tif")).string();
  auto output = (root / "memory_single.jhc").string();
  compress("file encode between memory encodes", {"-i", tif, "-M", "64", "-o", output, "-G", "0"});
  if(compressFrame(3, 0, Device::plugin) != reference)
    fail("memory encode after a file encode differs from the CPU");
  auto accelerated = grk_plugin_accelerated_frames() - before;
  if(accelerated != 3)
    fail("memory, file and memory encodes: " + std::to_string(accelerated) +
         " frames on the plugin");
}

// a 4:2:0 memory encode on the plugin writes the CPU's bytes
void checkSubsampledMemoryEncode()
{
  auto reference = compressFrame(3, 0, Device::cpu, 2);
  if(reference.empty() || compressFrame(3, 0, Device::plugin, 2) != reference)
    fail("4:2:0 memory encode differs from the CPU");
}

// the precision and samples of every component after an 8 bit output precision is requested
std::vector<int32_t> decompressTo8Bit(std::vector<uint8_t>& stream, Device device)
{
  grk_decompress_parameters params = {};
  params.cpu_only = device == Device::cpu;
  grk_stream_params streamParams = {};
  streamParams.buf = stream.data();
  streamParams.buf_len = stream.size();
  auto codec = grk_decompress_init(&streamParams, &params);
  grk_precision precision = {8, GRK_PREC_MODE_SCALE};
  grk_header_info header = {};
  header.precision = &precision;
  header.num_precision = 1;
  std::vector<int32_t> result;
  if(!codec || !grk_decompress_read_header(codec, &header) || !grk_decompress(codec, nullptr))
  {
    grk_object_unref(codec);
    return result;
  }
  auto image = grk_decompress_get_image(codec);
  for(uint16_t compno = 0; image && compno < image->numcomps; ++compno)
  {
    auto comp = image->comps + compno;
    result.push_back(comp->prec);
    bool isInt16 = comp->data_type == GRK_INT_16;
    for(uint32_t y = 0; y < comp->h; ++y)
    {
      for(uint32_t x = 0; x < comp->w; ++x)
      {
        size_t index = (size_t)y * comp->stride + x;
        result.push_back(isInt16 ? ((const int16_t*)comp->data)[index]
                                 : ((const int32_t*)comp->data)[index]);
      }
    }
  }
  grk_object_unref(codec);
  return result;
}

// a plugin decode through the library applies the requested output precision
void checkPluginDecodePrecision()
{
  auto stream = compressFrame(3, 0, Device::cpu);
  auto reference = decompressTo8Bit(stream, Device::cpu);
  auto before = grk_plugin_accelerated_frames();
  auto decoded = decompressTo8Bit(stream, Device::plugin);
  if(grk_plugin_accelerated_frames() != before + 1)
    fail("8 bit output precision: the decode did not run on the plugin");
  if(reference.empty() || decoded != reference)
    fail("8 bit output precision: plugin decode differs from the CPU");
}

// a plugin decode after a plugin file encode decodes the code stream, not the encoder's image
void checkPluginDecodeAfterFileEncode(const std::filesystem::path& root)
{
  auto stream = compressFrame(3, 0, Device::cpu);
  checkStream("plugin decode before a file encode", stream, 3, 0, Device::plugin);
  auto tif = (root / "3" / "tif" / frameName(0, "tif")).string();
  auto output = (root / "decode_single.jhc").string();
  compress("file encode between plugin decodes",
           {"-i", tif, "-M", "64", "-n", "3", "-o", output, "-G", "0"});
  checkStream("plugin decode after a file encode", stream, 3, 0, Device::plugin);
}

// a lossy batch frame writes the bytes of its single file encode, whatever frame finished before it
void checkBatchRateTolerance(const std::filesystem::path& root)
{
  auto tifDir = root / "3" / "tif";
  auto batchDir = root / "rate_batch";
  auto singleDir = root / "rate_single";
  for(auto& dir : {batchDir, singleDir})
    std::filesystem::create_directories(dir);
  std::vector<std::string> rateArguments = {"-I", "-r", "30", "--rate-tolerance", "0.3", "-G", "0"};
  std::vector<std::string> batchArguments = {"-y", tifDir.string(),  "-O", "J2K",
                                             "-a", batchDir.string()};
  batchArguments.insert(batchArguments.end(), rateArguments.begin(), rateArguments.end());
  compress("lossy batch", batchArguments);
  for(uint32_t frame = 0; frame < kNumFrames; ++frame)
  {
    auto single = singleDir / frameName(frame, "j2k");
    std::vector<std::string> singleArguments = {"-i", (tifDir / frameName(frame, "tif")).string(),
                                                "-o", single.string()};
    singleArguments.insert(singleArguments.end(), rateArguments.begin(), rateArguments.end());
    compress("lossy single file", singleArguments);
    auto batch = readFile(batchDir / frameName(frame, "j2k"));
    if(batch.empty() || batch != readFile(single))
      fail("lossy batch, " + frameName(frame, "j2k") + ": differs from the single file encode");
  }
}

// grk_decompress -G -2 decodes on the CPU, as grk_compress -G -2 encodes on the CPU
void checkDecompressDevice(const std::string& decompressPath, const std::filesystem::path& root)
{
  auto streamDir = root / "3" / "jhc_streams";
  auto name = frameName(0, "jhc");
  for(std::string deviceId : {"0", "-2"})
  {
    auto decodeDir = root / ("device_" + deviceId);
    std::filesystem::create_directories(decodeDir);
    if(runQuiet("\"" + decompressPath + "\" -y \"" + streamDir.string() + "\" -a \"" +
                decodeDir.string() + "\" -O tif -G " + deviceId) != 0)
      fail("grk_decompress -G " + deviceId + " returned non zero");
    // the plugin names a batch output after the whole input file name
    bool onPlugin = std::filesystem::exists(decodeDir / (name + ".tif"));
    bool onCpu = std::filesystem::exists(decodeDir / frameName(0, "tif"));
    if(onPlugin != (deviceId != "-2") || onCpu != (deviceId == "-2"))
      fail("grk_decompress -G " + deviceId + " decoded on the wrong device");
  }
}

constexpr char kXmp[] = "<x:xmpmeta>plugin tiff metadata</x:xmpmeta>";
constexpr uint32_t kProfileBytes = 132;
// pels per metre
constexpr double kResolution = 3000;

void writeBigEndian32(uint8_t* destination, uint32_t value)
{
  for(int i = 0; i < 4; ++i)
    destination[i] = (uint8_t)(value >> (24 - 8 * i));
}

// an RGB display profile header with no tags, enough for lcms to open it
std::vector<uint8_t> makeProfile()
{
  std::vector<uint8_t> profile(kProfileBytes, 0);
  writeBigEndian32(profile.data(), kProfileBytes);
  writeBigEndian32(profile.data() + 8, 0x04300000);
  std::memcpy(profile.data() + 12, "mntrRGB XYZ ", 12);
  std::memcpy(profile.data() + 36, "acsp", 4);
  return profile;
}

bool writeJp2WithMetadata(const std::filesystem::path& path)
{
  auto image = makeFrame(3, 0);
  if(!image)
    return false;
  image->meta = grk_image_meta_new();
  auto profile = makeProfile();
  image->meta->color.icc_profile_buf = new uint8_t[kProfileBytes];
  std::memcpy(image->meta->color.icc_profile_buf, profile.data(), kProfileBytes);
  image->meta->color.icc_profile_len = kProfileBytes;
  image->meta->xmp_buf = new uint8_t[sizeof(kXmp)];
  std::memcpy(image->meta->xmp_buf, kXmp, sizeof(kXmp));
  image->meta->xmp_len = sizeof(kXmp);
  grk_cparameters params;
  grk_compress_set_default_params(&params);
  params.cod_format = GRK_FMT_JP2;
  params.numlayers = 1;
  params.mct = 1;
  params.write_capture_resolution = true;
  params.capture_resolution[0] = kResolution;
  params.capture_resolution[1] = kResolution;
  std::vector<uint8_t> buffer((size_t)kWidth * kHeight * 3 * 4);
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

void checkJp2Metadata(const std::string& label, const std::filesystem::path& path)
{
  grk_decompress_parameters params = {};
  grk_stream_params streamParams = {};
  auto file = path.string();
  std::snprintf(streamParams.file, sizeof(streamParams.file), "%s", file.c_str());
  auto codec = grk_decompress_init(&streamParams, &params);
  grk_header_info header = {};
  if(!codec || !grk_decompress_read_header(codec, &header))
  {
    fail(label + ": read header");
    grk_object_unref(codec);
    return;
  }
  auto image = grk_decompress_get_image(codec);
  auto meta = image ? image->meta : nullptr;
  auto profile = makeProfile();
  if(!meta || meta->color.icc_profile_len != kProfileBytes ||
     std::memcmp(meta->color.icc_profile_buf, profile.data(), kProfileBytes) != 0)
    fail(label + ": ICC profile missing");
  if(!meta || meta->xmp_len != sizeof(kXmp) || std::memcmp(meta->xmp_buf, kXmp, sizeof(kXmp)) != 0)
    fail(label + ": XMP missing");
  bool hasResolution = image && std::abs(image->capture_resolution[0] - kResolution) < 1 &&
                       std::abs(image->capture_resolution[1] - kResolution) < 1;
  if(!hasResolution)
    fail(label + ": capture resolution missing");
  grk_object_unref(codec);
}

// metadata survives a plugin decode to TIFF and a plugin encode of that TIFF
void checkTiffMetadata(const std::string& decompressPath, const std::filesystem::path& root)
{
  auto metadataRoot = root / "tiff_metadata";
  auto sourceDir = metadataRoot / "source";
  auto tifDir = metadataRoot / "tif";
  auto batchDecodeDir = metadataRoot / "batch_decode";
  auto batchDir = metadataRoot / "batch";
  for(auto& dir : {sourceDir, tifDir, batchDecodeDir, batchDir})
    std::filesystem::create_directories(dir);
  auto source = sourceDir / "source.jp2";
  auto tif = tifDir / frameName(0, "tif");
  if(!writeJp2WithMetadata(source) ||
     runQuiet("\"" + decompressPath + "\" -i \"" + source.string() + "\" -o \"" + tif.string() +
              "\" -G 0") != 0)
  {
    fail("TIFF with metadata: could not write the input");
    return;
  }
  if(runQuiet("\"" + decompressPath + "\" -y \"" + sourceDir.string() + "\" -a \"" +
              batchDecodeDir.string() + "\" -O tif -G 0") != 0)
    fail("TIFF metadata, batch decode: grk_decompress returned non zero");
  // the plugin names a batch output after the whole input file name
  auto batchDecoded = batchDecodeDir / "source.jp2.tif";
  if(!std::filesystem::exists(batchDecoded))
    fail("TIFF metadata, batch decode: not decoded on the plugin");
  auto batchDecodedJp2 = metadataRoot / "batch_decode.jp2";
  compress("TIFF metadata, batch decode",
           {"-i", batchDecoded.string(), "-o", batchDecodedJp2.string(), "-G", "-2"});
  checkJp2Metadata("TIFF metadata, batch decode", batchDecodedJp2);
  auto single = metadataRoot / "single.jp2";
  if(compress("TIFF metadata, single file",
              {"-i", tif.string(), "-o", single.string(), "-G", "0"}) != 1)
    fail("TIFF metadata, single file: not coded on the plugin");
  checkJp2Metadata("TIFF metadata, single file", single);
  if(compress("TIFF metadata, batch",
              {"-y", tifDir.string(), "-O", "jp2", "-a", batchDir.string(), "-G", "0"}) != 1)
    fail("TIFF metadata, batch: not coded on the plugin");
  checkJp2Metadata("TIFF metadata, batch", batchDir / frameName(0, "jp2"));
}

} // namespace

int main(int argc, char** argv)
{
  bool validMode =
      argc == 3 && (std::strcmp(argv[2], "cpu") == 0 || std::strcmp(argv[2], "plugin") == 0);
  if(!validMode)
  {
    std::fprintf(stderr, "usage: %s <grk_decompress> cpu|plugin\n", argv[0]);
    return 1;
  }
  auto device = std::strcmp(argv[2], "plugin") == 0 ? Device::plugin : Device::cpu;
  auto root = std::filesystem::current_path() / (std::string("compress_ht_test_") + argv[2]);
  std::filesystem::remove_all(root);
  grk_initialize(nullptr, 0, nullptr);
  for(auto numComps : kComponentCounts)
    checkCompress(argv[1], device, root, numComps);
  if(device == Device::plugin)
  {
    checkMixedDirectory(root);
    checkMemoryEncodeAfterFileEncode(root);
    checkSubsampledMemoryEncode();
    checkPluginDecodePrecision();
    checkPluginDecodeAfterFileEncode(root);
    checkBatchRateTolerance(root);
    checkDecompressDevice(argv[1], root);
    checkTiffMetadata(argv[1], root);
  }
  grk_deinitialize();
  if(g_failures)
  {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  std::filesystem::remove_all(root);
  return 0;
}
