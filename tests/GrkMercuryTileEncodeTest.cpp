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
#include <vector>

#include <lcms2.h>

#include "grok.h"
#include "GrkCompressInStrips.h"

namespace
{
const size_t OUTPUT_CAPACITY = 16u << 20;
const char* MERCURY_ENCODE_VARIABLE = "GRK_MERCURY_ENCODE";
const char* MERCURY_COMPRESSED_MESSAGE = "mercury compressed";

bool mercuryCompressed = false;

struct Case
{
  const char* name;
  uint32_t width;
  uint32_t height;
  uint16_t numcomps;
  uint8_t precision;
  bool mct;
  uint8_t resolutions;
  uint32_t blockSide;
  GRK_PROG_ORDER order;
  // 0 for no precincts
  uint32_t precinctSide;
  bool splitByComponent;
  bool poc;
  double ratio;
  // classic only, mercury turns these down
  uint16_t layers;
  bool sop;
  uint32_t tileSide;
  bool expectMercury;
  grk_data_type dataType = GRK_INT_32;
  bool icc = false;
  bool pocSplitDiffersByTile = false;
};

void recordInfo(const char* message, void*)
{
  if(std::strstr(message, MERCURY_COMPRESSED_MESSAGE))
    mercuryCompressed = true;
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

enum class Input
{
  WholeImage,
  Strips,
};

void attachIccProfile(grk_image* image)
{
  // lcms leaves samples unchanged going from an sRGB profile to sRGB
  cmsToneCurve* linear = cmsBuildGamma(nullptr, 1.0);
  cmsHPROFILE profile = nullptr;
  if(image->numcomps == 3)
  {
    cmsCIExyY whitePoint = {0.3127, 0.3290, 1.0};
    cmsCIExyYTRIPLE rec2020 = {{0.708, 0.292, 1.0}, {0.170, 0.797, 1.0}, {0.131, 0.046, 1.0}};
    cmsToneCurve* curves[3] = {linear, linear, linear};
    profile = cmsCreateRGBProfile(&whitePoint, &rec2020, curves);
  }
  else
  {
    profile = cmsCreateGrayProfile(cmsD50_xyY(), linear);
  }
  cmsUInt32Number length = 0;
  cmsSaveProfileToMem(profile, nullptr, &length);
  image->meta = grk_image_meta_new();
  image->meta->color.icc_profile_buf = new uint8_t[length];
  image->meta->color.icc_profile_len = length;
  cmsSaveProfileToMem(profile, image->meta->color.icc_profile_buf, &length);
  cmsCloseProfile(profile);
  cmsFreeToneCurve(linear);
}

uint64_t nextRandom(uint64_t& state)
{
  state = state * 6364136223846793005ULL + 1442695040888963407ULL;
  return state >> 33;
}

std::vector<uint8_t> encode(const Case& test, bool mercury, Input input)
{
  std::vector<grk_image_comp> comps(test.numcomps);
  for(auto& comp : comps)
  {
    comp.w = test.width;
    comp.h = test.height;
    comp.dx = 1;
    comp.dy = 1;
    comp.prec = test.precision;
    comp.sgnd = false;
    comp.data_type = test.dataType;
  }
  auto colorSpace = test.numcomps == 3 ? GRK_CLRSPC_SRGB : GRK_CLRSPC_GRAY;
  auto image = grk_image_new(test.numcomps, comps.data(), colorSpace, true);
  const int64_t maxValue = (1LL << test.precision) - 1;
  uint64_t state = test.width * 7919ULL + test.height;
  for(uint16_t c = 0; c < test.numcomps; ++c)
  {
    auto data = (int32_t*)image->comps[c].data;
    for(uint32_t y = 0; y < test.height; ++y)
    {
      for(uint32_t x = 0; x < test.width; ++x)
      {
        int64_t value =
            maxValue * x / (2 * test.width) + maxValue * y / (4 * test.height) + maxValue * c / 16;
        // noisier toward the right so blocks spread over many slopes
        value +=
            (int64_t)(nextRandom(state) % ((uint64_t)maxValue * (x + 1) / (4 * test.width) + 1));
        size_t index = (size_t)y * image->comps[c].stride + x;
        if(test.dataType == GRK_INT_16)
          ((int16_t*)image->comps[c].data)[index] = (int16_t)std::min(value, maxValue);
        else
          data[index] = (int32_t)std::min(value, maxValue);
      }
    }
  }
  grk_cparameters params;
  grk_compress_set_default_params(&params);
  params.cod_format = GRK_FMT_J2K;
  params.irreversible = true;
  params.mct = test.mct ? 1 : 0;
  if(test.icc)
  {
    attachIccProfile(image);
    params.apply_icc = true;
  }
  params.numresolution = test.resolutions;
  params.cblockw_init = test.blockSide;
  params.cblockh_init = test.blockSide;
  params.prog_order = test.order;
  params.numlayers = test.layers;
  for(uint16_t layer = 0; layer < test.layers; ++layer)
    params.layer_rate[layer] = test.ratio * (test.layers - layer);
  params.allocation_by_rate_distortion = true;
  params.write_tlm = true;
  if(test.precinctSide)
  {
    params.csty |= 0x01;
    params.res_spec = 1;
    params.prcw_init[0] = test.precinctSide;
    params.prch_init[0] = test.precinctSide;
  }
  if(test.sop)
    params.csty |= 0x02;
  if(test.splitByComponent)
  {
    params.enable_tile_part_generation = true;
    params.new_tile_part_progression_divider = 'C';
  }
  if(test.poc)
  {
    uint32_t numTiles = 1;
    if(test.tileSide)
      numTiles = ((test.width + test.tileSide - 1) / test.tileSide) *
                 ((test.height + test.tileSide - 1) / test.tileSide);
    // grok fails a tile with no progression of its own
    for(uint32_t tile = 0; tile < numTiles; ++tile)
    {
      uint8_t splitResolution = test.pocSplitDiffersByTile && tile > 0 ? 2 : 3;
      auto progression = params.progression + 2 * tile;
      progression[0] = {};
      progression[0].tileno = tile;
      progression[0].res_e = splitResolution;
      progression[0].comp_e = test.numcomps;
      progression[0].lay_e = test.layers;
      progression[0].specified_compression_poc_prog = test.order;
      progression[1] = {};
      progression[1].tileno = tile;
      progression[1].res_s = splitResolution;
      progression[1].res_e = test.resolutions;
      progression[1].comp_e = test.numcomps;
      progression[1].lay_e = test.layers;
      progression[1].specified_compression_poc_prog = test.order;
    }
    params.numpocs = 2 * numTiles - 1;
  }
  if(test.tileSide)
  {
    params.tile_size_on = true;
    params.t_width = test.tileSide;
    params.t_height = test.tileSide;
  }
  std::vector<uint8_t> out(OUTPUT_CAPACITY);
  grk_stream_params stream = {};
  stream.buf = out.data();
  stream.buf_len = out.size();
  setMercuryEncode(mercury);
  mercuryCompressed = false;
  uint64_t length = 0;
  if(input == Input::Strips)
  {
    length = compressInStrips(&stream, &params, image);
  }
  else
  {
    auto codec = grk_compress_init(&stream, &params, image);
    length = codec ? grk_compress(codec, nullptr) : 0;
    grk_object_unref(codec);
  }
  grk_object_unref(&image->obj);
  out.resize(length);
  return out;
}

bool sameCodestream(const Case& test, const char* label, const std::vector<uint8_t>& classic,
                    const std::vector<uint8_t>& other)
{
  if(classic.empty() || other.empty())
  {
    fprintf(stderr, "%s: encode failed, classic %zu bytes, %s %zu bytes\n", test.name,
            classic.size(), label, other.size());
    return false;
  }
  if(classic == other)
    return true;
  size_t at = 0;
  while(at < std::min(classic.size(), other.size()) && classic[at] == other[at])
    ++at;
  fprintf(stderr, "%s: codestreams differ at byte %zu, classic %zu bytes, %s %zu bytes\n",
          test.name, at, classic.size(), label, other.size());
  return false;
}

bool runCase(const Case& test)
{
  auto classic = encode(test, false, Input::WholeImage);
  if(mercuryCompressed)
  {
    fprintf(stderr, "%s: mercury ran with %s=0\n", test.name, MERCURY_ENCODE_VARIABLE);
    return false;
  }
  for(auto input : {Input::WholeImage, Input::Strips})
  {
    const char* label = input == Input::Strips ? "strips" : "mercury";
    auto other = encode(test, true, input);
    if(mercuryCompressed != test.expectMercury)
    {
      fprintf(stderr, "%s: grok %s the %s to mercury\n", test.name,
              test.expectMercury ? "did not hand" : "handed", label);
      return false;
    }
    if(!sameCodestream(test, label, classic, other))
      return false;
  }
  if(test.icc)
  {
    auto withoutIcc = test;
    withoutIcc.icc = false;
    if(encode(withoutIcc, false, Input::WholeImage) == classic)
    {
      fprintf(stderr, "%s: the ICC profile left the codestream unchanged\n", test.name);
      return false;
    }
  }
  printf("%s: %zu bytes identical\n", test.name, classic.size());
  return true;
}

bool stripMisuseFails()
{
  const uint32_t width = 64;
  const uint32_t height = 32;
  grk_image_comp comp = {};
  comp.w = width;
  comp.h = height;
  comp.dx = 1;
  comp.dy = 1;
  comp.prec = 8;
  comp.data_type = GRK_INT_16;
  auto image = grk_image_new(1, &comp, GRK_CLRSPC_GRAY, false);
  std::vector<int16_t> samples((size_t)width * (height + 1));
  const void* planes[] = {samples.data()};
  grk_cparameters params;
  grk_compress_set_default_params(&params);
  params.cod_format = GRK_FMT_J2K;
  params.irreversible = true;
  params.numlayers = 1;
  params.layer_rate[0] = 10.0;
  std::vector<uint8_t> out(OUTPUT_CAPACITY);
  grk_stream_params stream = {};
  stream.buf = out.data();
  stream.buf_len = out.size();
  bool ok = true;
  for(uint32_t rows : {height + 1, height - 1})
  {
    auto codec = grk_compress_init_strips(&stream, &params, image);
    grk_strip strip = {rows, planes, width};
    bool pushed = codec && grk_compress_push_strip(codec, &strip);
    uint64_t length = pushed ? grk_compress(codec, nullptr) : 0;
    grk_object_unref(codec);
    if(rows > height && pushed)
    {
      fprintf(stderr, "a strip of %u rows was taken for an image of %u rows\n", rows, height);
      ok = false;
    }
    if(rows < height && (!pushed || length))
    {
      fprintf(stderr, "compressing after %u of %u rows did not fail\n", rows, height);
      ok = false;
    }
  }
  grk_object_unref(&image->obj);
  if(ok)
    printf("strip misuse fails\n");
  return ok;
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
      {"rgb 8 bit defaults", 1001, 733, 3, 8, true, 6, 64, GRK_LRCP, 0, false, false, 24.0, 1,
       false, 0, true},
      {"rgb 12 bit rpcl precincts", 777, 515, 3, 12, true, 5, 32, GRK_RPCL, 128, false, false, 10.0,
       1, false, 0, true},
      {"rgb 16 bit pcrl", 640, 480, 3, 16, true, 6, 64, GRK_PCRL, 0, false, false, 20.0, 1, false,
       0, true},
      {"gray 12 bit rlcp", 999, 317, 1, 12, false, 4, 64, GRK_RLCP, 0, false, false, 8.0, 1, false,
       0, true},
      {"three planes without mct", 513, 389, 3, 8, false, 6, 32, GRK_LRCP, 0, false, false, 16.0, 1,
       false, 0, true},
      {"cprl split by component", 800, 600, 3, 10, true, 6, 32, GRK_CPRL, 64, true, false, 12.0, 1,
       false, 0, true},
      {"rpcl poc", 700, 500, 3, 8, true, 6, 64, GRK_RPCL, 0, false, true, 20.0, 1, false, 0, true},
      {"two levels", 301, 1037, 3, 8, true, 3, 64, GRK_LRCP, 0, false, false, 30.0, 1, false, 0,
       true},
      {"two layers", 640, 480, 3, 8, true, 6, 64, GRK_LRCP, 0, false, false, 20.0, 2, false, 0,
       false},
      {"sop", 640, 480, 3, 8, true, 6, 64, GRK_LRCP, 0, false, false, 20.0, 1, true, 0, false},
      {"tiled", 640, 480, 3, 8, true, 6, 64, GRK_LRCP, 0, false, false, 20.0, 1, false, 256, true},
      {"int16 tiled partial edges", 1001, 733, 3, 8, true, 6, 64, GRK_LRCP, 0, false, false, 24.0,
       1, false, 300, true, GRK_INT_16},
      {"tiled cprl split by component", 800, 600, 3, 10, true, 6, 32, GRK_CPRL, 64, true, false,
       12.0, 1, false, 300, true},
      {"tiled rpcl poc", 700, 500, 3, 8, true, 6, 64, GRK_RPCL, 0, false, true, 20.0, 1, false, 256,
       true},
      {"tiled poc differs by tile",
       700,
       500,
       3,
       8,
       true,
       6,
       64,
       GRK_RPCL,
       0,
       false,
       true,
       20.0,
       1,
       false,
       256,
       false,
       GRK_INT_32,
       false,
       true},
      {"small tiles", 200, 150, 1, 12, false, 4, 32, GRK_LRCP, 0, false, false, 16.0, 1, false, 40,
       true},
      {"int16 rgb 8 bit defaults", 1001, 733, 3, 8, true, 6, 64, GRK_LRCP, 0, false, false, 24.0, 1,
       false, 0, true, GRK_INT_16},
      {"int16 rgb 10 bit rpcl precincts", 777, 515, 3, 10, true, 5, 32, GRK_RPCL, 128, false, false,
       10.0, 1, false, 0, true, GRK_INT_16},
      {"int16 gray 8 bit rlcp", 999, 317, 1, 8, false, 4, 64, GRK_RLCP, 0, false, false, 8.0, 1,
       false, 0, true, GRK_INT_16},
      {"int16 three planes without mct", 513, 389, 3, 9, false, 6, 32, GRK_LRCP, 0, false, false,
       16.0, 1, false, 0, true, GRK_INT_16},
      {"int16 two columns", 2, 300, 3, 8, true, 6, 64, GRK_LRCP, 0, false, false, 4.0, 1, false, 0,
       true, GRK_INT_16},
      {"int16 two rows", 300, 2, 3, 8, true, 6, 64, GRK_LRCP, 0, false, false, 4.0, 1, false, 0,
       true, GRK_INT_16},
      {"int16 odd tiny", 5, 3, 3, 8, true, 6, 64, GRK_LRCP, 0, false, false, 2.0, 1, false, 0, true,
       GRK_INT_16},
      {"int16 12 bit", 640, 480, 3, 12, true, 6, 64, GRK_LRCP, 0, false, false, 20.0, 1, false, 0,
       false, GRK_INT_16},
      {"icc int16 rgb 8 bit", 777, 515, 3, 8, true, 6, 64, GRK_LRCP, 0, false, false, 20.0, 1,
       false, 0, true, GRK_INT_16, true},
      {"icc gray 16 bit", 515, 777, 1, 16, false, 6, 64, GRK_LRCP, 0, false, false, 20.0, 1, false,
       0, true, GRK_INT_32, true},
      {"icc rgb 8 bit tiled", 640, 480, 3, 8, true, 6, 64, GRK_LRCP, 0, false, false, 20.0, 1,
       false, 256, true, GRK_INT_32, true},
  };
  bool ok = true;
  for(const auto& test : cases)
    ok = runCase(test) && ok;
  ok = stripMisuseFails() && ok;
  grk_deinitialize();
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
