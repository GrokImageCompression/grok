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
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "grok.h"
#include "mercury_capi.h"

namespace
{
const uint32_t COMPONENTS = 3;
const uint8_t CODED_PRECISION = 12;
const uint8_t SOURCE_PRECISION = 16;
const uint32_t SIXTEEN_TO_TWELVE_SHIFT = 4;
const size_t OUTPUT_CAPACITY = 8u << 20;
const uint32_t RESOLUTIONS_2K = 6;
const uint32_t RESOLUTIONS_4K = 7;
const uint8_t GUARD_BITS_2K = 1;
const uint8_t GUARD_BITS_4K = 2;
const size_t VERSION_FIELD_WIDTH = 16;
const char* MERCURY_ENCODE_VARIABLE = "GRK_MERCURY_ENCODE";
const char* MERCURY_COMPRESSED_MESSAGE = "mercury compressed";
const char* CLASSIC_XYZ_MESSAGE = "Applied Rec.709 RGB";
const char* OUTPUT_FILE = "grk_mercury_dci_encode_test.j2k";
// grok's path runs on every core
const uint32_t SINGLE_THREAD = 1;

bool mercuryCompressed = false;
bool classicXyzRan = false;

struct Case
{
  const char* name;
  bool fourK;
  uint32_t width;
  uint32_t height;
  // noise amplitude in 16-bit units, 0 for a smooth frame that fits the budget whole
  uint32_t noise;
  // added to the threshold an unhinted encode finds, 0 for no hint
  int32_t hintOffset;
  double tolerance;
  // RGB depth grok converts to X'Y'Z', 0 for 16-bit X'Y'Z' cut to 12 bits
  uint8_t xyzPrecision;
  // requested compression ratio, 0 for none
  double layerRate;
  // false lets grok derive the size caps from the ratio
  bool cinemaCaps;
  // mercury writes no PLT
  bool writePlt;
};

void recordInfo(const char* message, void*)
{
  if(std::strstr(message, MERCURY_COMPRESSED_MESSAGE))
    mercuryCompressed = true;
  if(std::strstr(message, CLASSIC_XYZ_MESSAGE))
    classicXyzRan = true;
}

void printError(const char* message, void*)
{
  fprintf(stderr, "%s", message);
}

// unset tests the default
void setMercuryEncode(bool enabled)
{
#ifdef _WIN32
  _putenv_s(MERCURY_ENCODE_VARIABLE, enabled ? "" : "0");
#else
  if(enabled)
    unsetenv(MERCURY_ENCODE_VARIABLE);
  else
    setenv(MERCURY_ENCODE_VARIABLE, "0", 1);
#endif
}

uint64_t nextRandom(uint64_t& state)
{
  state = state * 6364136223846793005ULL + 1442695040888963407ULL;
  return state >> 33;
}

uint8_t sourcePrecision(const Case& test)
{
  return test.xyzPrecision ? test.xyzPrecision : SOURCE_PRECISION;
}

std::vector<std::vector<int32_t>> makeFrame(const Case& test)
{
  uint32_t shift = SOURCE_PRECISION - sourcePrecision(test);
  uint64_t state = test.width * 7919ULL + test.height;
  std::vector<std::vector<int32_t>> planes(COMPONENTS,
                                           std::vector<int32_t>((size_t)test.width * test.height));
  for(uint32_t c = 0; c < COMPONENTS; ++c)
  {
    for(uint32_t y = 0; y < test.height; ++y)
    {
      for(uint32_t x = 0; x < test.width; ++x)
      {
        int64_t value = 8000 + (int64_t)x * 40000 / test.width + (int64_t)y * 15000 / test.height +
                        (int64_t)c * 3000;
        // noisier toward the right so blocks spread over many slopes
        if(test.noise)
          value += (int64_t)(nextRandom(state) % (test.noise * (x + 1) / test.width + 1));
        planes[c][(size_t)y * test.width + x] = (int32_t)std::min<int64_t>(value, 65535) >> shift;
      }
    }
  }
  return planes;
}

std::string defaultComment()
{
  auto version = std::string(grk_version());
  version.resize(std::max(version.size(), VERSION_FIELD_WIDTH), ' ');
  return "Created by Grok version " + version;
}

std::vector<uint8_t> libraryEncode(const Case& test,
                                   const std::vector<std::vector<int32_t>>& planes,
                                   uint16_t slopeHint, bool mercury, bool toFile)
{
  grk_image_comp comps[COMPONENTS] = {};
  for(auto& comp : comps)
  {
    comp.w = test.width;
    comp.h = test.height;
    comp.dx = 1;
    comp.dy = 1;
    comp.prec = test.xyzPrecision ? test.xyzPrecision : CODED_PRECISION;
    comp.sgnd = false;
  }
  auto image = grk_image_new(COMPONENTS, comps, GRK_CLRSPC_SRGB, true);
  uint32_t shift = test.xyzPrecision ? 0 : SIXTEEN_TO_TWELVE_SHIFT;
  for(uint32_t c = 0; c < COMPONENTS; ++c)
  {
    auto data = (int32_t*)image->comps[c].data;
    for(uint32_t y = 0; y < test.height; ++y)
      for(uint32_t x = 0; x < test.width; ++x)
        data[(size_t)y * image->comps[c].stride + x] =
            planes[c][(size_t)y * test.width + x] >> shift;
  }
  grk_cparameters params;
  grk_compress_set_default_params(&params);
  params.cod_format = GRK_FMT_J2K;
  params.rsiz = test.fourK ? GRK_PROFILE_CINEMA_4K : GRK_PROFILE_CINEMA_2K;
  params.apply_xyz_transform = test.xyzPrecision != 0;
  if(test.layerRate > 0.0)
  {
    params.numlayers = 1;
    params.layer_rate[0] = test.layerRate;
    params.allocation_by_rate_distortion = true;
  }
  if(test.cinemaCaps)
  {
    params.framerate = 24;
    params.max_cs_size = GRK_CINEMA_24_CS;
    params.max_comp_size = GRK_CINEMA_24_COMP;
  }
  params.numgbits = test.fourK ? GUARD_BITS_4K : GUARD_BITS_2K;
  params.numresolution = test.fourK ? RESOLUTIONS_4K : RESOLUTIONS_2K;
  params.write_tlm = true;
  params.write_plt = test.writePlt;
  params.rate_control_slope_hint = slopeHint;
  params.rate_control_tolerance = test.tolerance;
  std::vector<uint8_t> out(OUTPUT_CAPACITY);
  grk_stream_params stream = {};
  if(toFile)
  {
    snprintf(stream.file, sizeof(stream.file), "%s", OUTPUT_FILE);
  }
  else
  {
    stream.buf = out.data();
    stream.buf_len = out.size();
  }
  setMercuryEncode(mercury);
  mercuryCompressed = false;
  classicXyzRan = false;
  auto codec = grk_compress_init(&stream, &params, image);
  uint64_t length = codec ? grk_compress(codec, nullptr) : 0;
  grk_object_unref(codec);
  grk_object_unref(&image->obj);
  if(toFile)
  {
    auto file = fopen(OUTPUT_FILE, "rb");
    length = file ? fread(out.data(), 1, out.size(), file) : 0;
    if(file)
      fclose(file);
    remove(OUTPUT_FILE);
  }
  out.resize(length);
  return out;
}

std::vector<uint8_t> mercuryEncode(const Case& test,
                                   const std::vector<std::vector<int32_t>>& planes,
                                   uint16_t slopeHint, uint16_t* threshold)
{
  const int32_t* pointers[COMPONENTS] = {planes[0].data(), planes[1].data(), planes[2].data()};
  auto comment = defaultComment();
  std::vector<uint8_t> out(OUTPUT_CAPACITY);
  uint64_t length = 0;
  int32_t rc = mercury_encode_dci(
      pointers, test.width, test.width, test.height, test.fourK, sourcePrecision(test),
      GRK_CINEMA_24_CS, 0.0, GRK_CINEMA_24_COMP, slopeHint, test.tolerance, test.xyzPrecision != 0,
      true, SINGLE_THREAD, (const uint8_t*)comment.data(), (uint32_t)comment.size(), out.data(),
      out.size(), &length, threshold);
  if(rc != 0)
    length = 0;
  out.resize(length);
  return out;
}

bool sameCodestream(const Case& test, const char* label, const std::vector<uint8_t>& classic,
                    const std::vector<uint8_t>& mercury)
{
  if(classic.empty() || mercury.empty())
  {
    fprintf(stderr, "%s: %s encode failed, classic %zu bytes, mercury %zu bytes\n", test.name,
            label, classic.size(), mercury.size());
    return false;
  }
  if(classic == mercury)
    return true;
  size_t at = 0;
  while(at < std::min(classic.size(), mercury.size()) && classic[at] == mercury[at])
    ++at;
  fprintf(stderr, "%s: %s codestreams differ at byte %zu, classic %zu bytes, mercury %zu bytes\n",
          test.name, label, at, classic.size(), mercury.size());
  return false;
}

bool runCase(const Case& test)
{
  auto planes = makeFrame(test);
  uint16_t slopeHint = 0;
  uint16_t threshold = 0;
  if(test.hintOffset)
  {
    mercuryEncode(test, planes, 0, &threshold);
    slopeHint = (uint16_t)(threshold + test.hintOffset);
  }
  auto classic = libraryEncode(test, planes, slopeHint, false, false);
  if(mercuryCompressed)
  {
    fprintf(stderr, "%s: mercury ran with %s=0\n", test.name, MERCURY_ENCODE_VARIABLE);
    return false;
  }
  for(bool toFile : {false, true})
  {
    auto throughGrok = libraryEncode(test, planes, slopeHint, true, toFile);
    if(mercuryCompressed == test.writePlt)
    {
      fprintf(stderr, "%s: grok %s the frame to mercury\n", test.name,
              test.writePlt ? "handed" : "did not hand");
      return false;
    }
    if(test.xyzPrecision && classicXyzRan != test.writePlt)
    {
      fprintf(stderr, "%s: classic's XYZ transform %s\n", test.name,
              test.writePlt ? "did not run" : "ran before mercury");
      return false;
    }
    if(!sameCodestream(test, toFile ? "grok file" : "grok", classic, throughGrok))
      return false;
  }
  // a requested ratio only reaches mercury through grok's parameter setup
  if(test.layerRate == 0.0 && !test.writePlt &&
     !sameCodestream(test, "direct", classic, mercuryEncode(test, planes, slopeHint, &threshold)))
    return false;
  printf("%s: %zu bytes identical\n", test.name, classic.size());
  return true;
}

} // namespace

int main()
{
  grk_initialize(nullptr, 0, nullptr);
  grk_msg_handlers handlers = {};
  handlers.info_callback = recordInfo;
  handlers.error_callback = printError;
  grk_set_msg_handlers(handlers);
  const Case cases[] = {
      {"2k noisy", false, 1024, 540, 20000, 0, 0.0, 0, 0.0, true},
      {"2k smooth", false, 1024, 540, 0, 0, 0.0, 0, 0.0, true},
      {"2k odd size", false, 999, 317, 30000, 0, 0.0, 0, 0.0, true},
      {"4k noisy", true, 1500, 800, 20000, 0, 0.0, 0, 0.0, true},
      {"4k odd size", true, 1283, 611, 30000, 0, 0.0, 0, 0.0, true},
      {"2k hint above", false, 1024, 540, 20000, 2100, 0.1, 0, 0.0, true},
      {"4k hint below", true, 1500, 800, 20000, -2100, 0.02, 0, 0.0, true},
      {"2k xyz", false, 999, 317, 30000, 0, 0.0, 16, 0.0, true},
      {"4k xyz", true, 1500, 800, 20000, 0, 0.0, 16, 0.0, true},
      {"2k xyz 12 bit", false, 999, 317, 30000, 0, 0.0, 12, 0.0, true},
      {"4k xyz 10 bit", true, 1283, 611, 30000, 0, 0.0, 10, 0.0, true},
      {"2k xyz 8 bit", false, 999, 317, 30000, 0, 0.0, 8, 0.0, true},
      {"2k ratio 10", false, 1024, 540, 20000, 0, 0.0, 0, 10.0, false},
      {"4k xyz ratio 10 under cap", true, 1500, 800, 20000, 0, 0.0, 16, 10.0, true},
      {"2k xyz 10 bit plt", false, 999, 317, 30000, 0, 0.0, 10, 0.0, true, true},
  };
  bool ok = true;
  for(const auto& test : cases)
    ok = runCase(test) && ok;
  grk_deinitialize();
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
