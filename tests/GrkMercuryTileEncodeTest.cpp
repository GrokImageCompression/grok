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

#include "grok.h"

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

uint64_t nextRandom(uint64_t& state)
{
  state = state * 6364136223846793005ULL + 1442695040888963407ULL;
  return state >> 33;
}

std::vector<uint8_t> encode(const Case& test, bool mercury)
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
        data[(size_t)y * image->comps[c].stride + x] = (int32_t)std::min(value, maxValue);
      }
    }
  }
  grk_cparameters params;
  grk_compress_set_default_params(&params);
  params.cod_format = GRK_FMT_J2K;
  params.irreversible = true;
  params.mct = test.mct ? 1 : 0;
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
    params.progression[0] = {};
    params.progression[0].res_e = 3;
    params.progression[0].comp_e = test.numcomps;
    params.progression[0].lay_e = test.layers;
    params.progression[0].specified_compression_poc_prog = test.order;
    params.progression[1] = {};
    params.progression[1].res_s = 3;
    params.progression[1].res_e = test.resolutions;
    params.progression[1].comp_e = test.numcomps;
    params.progression[1].lay_e = test.layers;
    params.progression[1].specified_compression_poc_prog = test.order;
    params.numpocs = 1;
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
  auto codec = grk_compress_init(&stream, &params, image);
  uint64_t length = codec ? grk_compress(codec, nullptr) : 0;
  grk_object_unref(codec);
  grk_object_unref(&image->obj);
  out.resize(length);
  return out;
}

bool runCase(const Case& test)
{
  auto classic = encode(test, false);
  if(mercuryCompressed)
  {
    fprintf(stderr, "%s: mercury ran with %s=0\n", test.name, MERCURY_ENCODE_VARIABLE);
    return false;
  }
  auto withMercury = encode(test, true);
  if(mercuryCompressed != test.expectMercury)
  {
    fprintf(stderr, "%s: grok %s the image to mercury\n", test.name,
            test.expectMercury ? "did not hand" : "handed");
    return false;
  }
  if(classic.empty() || withMercury.empty())
  {
    fprintf(stderr, "%s: encode failed, classic %zu bytes, mercury %zu bytes\n", test.name,
            classic.size(), withMercury.size());
    return false;
  }
  if(classic != withMercury)
  {
    size_t at = 0;
    while(at < std::min(classic.size(), withMercury.size()) && classic[at] == withMercury[at])
      ++at;
    fprintf(stderr, "%s: codestreams differ at byte %zu, classic %zu bytes, mercury %zu bytes\n",
            test.name, at, classic.size(), withMercury.size());
    return false;
  }
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
      {"tiled", 640, 480, 3, 8, true, 6, 64, GRK_LRCP, 0, false, false, 20.0, 1, false, 256, false},
  };
  bool ok = true;
  for(const auto& test : cases)
    ok = runCase(test) && ok;
  grk_deinitialize();
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
