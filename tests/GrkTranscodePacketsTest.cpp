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
 *
 */

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "grok.h"
#include "NoiseSample.h"

namespace
{
const uint32_t IMAGE_SIZE = 256;
const uint32_t TILE_SIZE = 128;
const uint8_t NUM_RESOLUTIONS = 3;
const uint8_t NUM_LAYERS = 3;
const uint16_t NUM_COMPONENTS = 3;
const uint32_t SUBSAMPLED_COMPONENT = 1;

struct SourceCase
{
  const char* name;
  bool tileParts;
  bool plt;
  bool sop;
  bool eph;
  uint32_t originX;
  uint32_t originY;
};
const SourceCase SOURCE_CASES[] = {
    {"plain", false, false, false, false, 0, 0},
    {"tile_parts_plt", true, true, false, false, 0, 0},
    {"sop_eph", false, false, true, true, 0, 0},
    {"unaligned_origin", false, false, false, false, 127, 62},
};

struct TranscodeCase
{
  const char* name;
  bool tlm;
  bool plt;
  bool sop;
  bool eph;
  uint16_t maxLayers;
  uint8_t maxResolutions;
  GRK_PROG_ORDER progression;
};
const TranscodeCase TRANSCODE_CASES[] = {
    {"tlm", true, false, false, false, 0, 0, GRK_PROG_UNKNOWN},
    {"plt", false, true, false, false, 0, 0, GRK_PROG_UNKNOWN},
    {"sop", false, false, true, false, 0, 0, GRK_PROG_UNKNOWN},
    {"eph", false, false, false, true, 0, 0, GRK_PROG_UNKNOWN},
    {"sop_eph", false, false, true, true, 0, 0, GRK_PROG_UNKNOWN},
    {"lrcp", false, false, false, false, 0, 0, GRK_LRCP},
    {"rlcp", false, false, false, false, 0, 0, GRK_RLCP},
    {"rpcl", false, false, false, false, 0, 0, GRK_RPCL},
    {"pcrl", false, false, false, false, 0, 0, GRK_PCRL},
    {"cprl", false, false, false, false, 0, 0, GRK_CPRL},
    {"all_markers_rpcl", true, true, true, true, 0, 0, GRK_RPCL},
    {"one_layer", false, false, false, false, 1, 0, GRK_PROG_UNKNOWN},
    {"two_resolutions", false, false, false, false, 0, 2, GRK_PROG_UNKNOWN},
    {"one_layer_two_resolutions_cprl", false, false, true, true, 1, 2, GRK_CPRL},
};

int32_t sampleAt(uint16_t component, uint32_t x, uint32_t y)
{
  return noiseSample(x + component * IMAGE_SIZE, y);
}

bool compress(const std::string& path, const SourceCase& source)
{
  grk_image_comp componentParams[NUM_COMPONENTS] = {};
  for(uint16_t component = 0; component < NUM_COMPONENTS; ++component)
  {
    const uint32_t subsampling = component == SUBSAMPLED_COMPONENT ? 2 : 1;
    componentParams[component].dx = subsampling;
    componentParams[component].dy = subsampling;
    componentParams[component].w = IMAGE_SIZE / subsampling;
    componentParams[component].h = IMAGE_SIZE / subsampling;
    componentParams[component].prec = 8;
  }
  grk_image* image = grk_image_new(NUM_COMPONENTS, componentParams, GRK_CLRSPC_SRGB, true);
  if(!image)
    return false;
  image->x0 = source.originX;
  image->y0 = source.originY;
  image->x1 = source.originX + IMAGE_SIZE;
  image->y1 = source.originY + IMAGE_SIZE;
  for(uint16_t component = 0; component < NUM_COMPONENTS; ++component)
  {
    auto& comp = image->comps[component];
    auto data = static_cast<int32_t*>(comp.data);
    for(uint32_t y = 0; y < comp.h; ++y)
      for(uint32_t x = 0; x < comp.w; ++x)
        data[(size_t)y * comp.stride + x] = sampleAt(component, x, y);
  }

  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.numresolution = NUM_RESOLUTIONS;
  parameters.allocation_by_rate_distortion = true;
  parameters.numlayers = NUM_LAYERS;
  parameters.layer_rate[0] = 8;
  parameters.layer_rate[1] = 3;
  parameters.layer_rate[2] = 0;
  parameters.tile_size_on = true;
  parameters.tx0 = source.originX;
  parameters.ty0 = source.originY;
  parameters.t_width = TILE_SIZE;
  parameters.t_height = TILE_SIZE;
  parameters.csty |= 0x01;
  parameters.res_spec = 2;
  parameters.prcw_init[0] = 32;
  parameters.prch_init[0] = 32;
  parameters.prcw_init[1] = 32;
  parameters.prch_init[1] = 32;
  parameters.write_plt = source.plt;
  if(source.sop)
    parameters.csty |= 0x02;
  if(source.eph)
    parameters.csty |= 0x04;
  parameters.enable_tile_part_generation = source.tileParts;
  parameters.new_tile_part_progression_divider = source.tileParts ? 'R' : 0;

  grk_stream_params streamParams = {};
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());
  grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
  bool ok = codec && grk_compress(codec, nullptr) != 0;
  if(codec)
    grk_object_unref(codec);
  grk_object_unref(&image->obj);
  return ok;
}

bool transcode(const std::string& source, const std::string& destination,
               const TranscodeCase& transcodeCase)
{
  grk_stream_params sourceParams = {};
  snprintf(sourceParams.file, sizeof(sourceParams.file), "%s", source.c_str());
  grk_decompress_parameters decompressParams = {};
  grk_object* codec = grk_decompress_init(&sourceParams, &decompressParams);
  if(!codec)
    return false;
  grk_header_info header = {};
  bool ok = grk_decompress_read_header(codec, &header);
  if(ok)
  {
    grk_cparameters parameters = {};
    grk_compress_set_default_params(&parameters);
    parameters.cod_format = GRK_FMT_J2K;
    parameters.write_tlm = transcodeCase.tlm;
    parameters.write_plt = transcodeCase.plt;
    parameters.write_sop = transcodeCase.sop;
    parameters.write_eph = transcodeCase.eph;
    parameters.max_layers_transcode = transcodeCase.maxLayers;
    parameters.max_res_transcode = transcodeCase.maxResolutions;
    parameters.transcode_prog_order = transcodeCase.progression;
    grk_stream_params destinationParams = {};
    snprintf(destinationParams.file, sizeof(destinationParams.file), "%s", destination.c_str());
    grk_stream_params transcodeSource = sourceParams;
    ok = grk_transcode(&transcodeSource, &destinationParams, &parameters,
                       grk_decompress_get_image(codec)) != 0;
  }
  grk_object_unref(codec);
  return ok;
}

using Planes = std::vector<std::vector<int32_t>>;

Planes decode(const std::string& path, uint16_t layers, uint8_t reduce)
{
  grk_stream_params streamParams = {};
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());
  grk_decompress_parameters params = {};
  params.core.layers_to_decompress = layers;
  params.core.reduce = reduce;
  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
    return {};
  grk_header_info header = {};
  Planes planes;
  if(grk_decompress_read_header(codec, &header) && grk_decompress_update(&params, codec) &&
     grk_decompress(codec, nullptr))
  {
    grk_image* image = grk_decompress_get_image(codec);
    for(uint16_t component = 0; image && component < image->numcomps; ++component)
    {
      const auto& comp = image->comps[component];
      if(!comp.data)
        return {};
      std::vector<int32_t> plane;
      plane.reserve((size_t)comp.w * comp.h);
      for(uint32_t y = 0; y < comp.h; ++y)
        for(uint32_t x = 0; x < comp.w; ++x)
        {
          size_t index = (size_t)y * comp.stride + x;
          plane.push_back(comp.data_type == GRK_INT_16 ? static_cast<int16_t*>(comp.data)[index]
                                                       : static_cast<int32_t*>(comp.data)[index]);
        }
      planes.push_back(std::move(plane));
    }
  }
  grk_object_unref(codec);
  return planes;
}

bool losslessFullDecodeMatchesImage(const Planes& planes)
{
  if(planes.size() != NUM_COMPONENTS)
    return false;
  for(uint16_t component = 0; component < NUM_COMPONENTS; ++component)
  {
    const uint32_t size = component == SUBSAMPLED_COMPONENT ? IMAGE_SIZE / 2 : IMAGE_SIZE;
    if(planes[component].size() != (size_t)size * size)
      return false;
    for(uint32_t y = 0; y < size; ++y)
      for(uint32_t x = 0; x < size; ++x)
        if(planes[component][(size_t)y * size + x] != sampleAt(component, x, y))
          return false;
  }
  return true;
}

struct SourceGeometry
{
  uint8_t numResolutions;
  uint32_t tileWidth;
  uint32_t tileHeight;
  uint32_t numTiles;
};

SourceGeometry readGeometry(const std::string& path)
{
  grk_stream_params streamParams = {};
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());
  grk_decompress_parameters params = {};
  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
    return {};
  grk_header_info header = {};
  SourceGeometry geometry = {};
  if(grk_decompress_read_header(codec, &header))
    geometry = {header.numresolutions, header.t_width, header.t_height,
                (uint32_t)header.t_grid_width * header.t_grid_height};
  grk_object_unref(codec);
  return geometry;
}

bool checkTranscodes(const std::filesystem::path& directory, const std::string& name,
                     const std::string& sourcePath)
{
  const SourceGeometry geometry = readGeometry(sourcePath);
  bool ok = geometry.numResolutions > 0;
  for(const auto& transcodeCase : TRANSCODE_CASES)
  {
    const std::string output = (directory / (name + "_" + transcodeCase.name + ".j2k")).string();
    if(transcodeCase.maxResolutions >= geometry.numResolutions)
      continue;
    const uint8_t reduce =
        transcodeCase.maxResolutions ? geometry.numResolutions - transcodeCase.maxResolutions : 0;
    const uint32_t scaleMask = (1U << reduce) - 1U;
    const bool gridFitsScale =
        geometry.numTiles == 1 || ((geometry.tileWidth | geometry.tileHeight) & scaleMask) == 0;
    const bool transcoded = transcode(sourcePath, output, transcodeCase);
    if(transcoded != gridFitsScale)
    {
      fprintf(stderr, "%s %s: transcode %s\n", name.c_str(), transcodeCase.name,
              transcoded ? "accepted a tile size it must reject" : "failed");
      ok = false;
      continue;
    }
    if(!transcoded)
      continue;
    const Planes expected = decode(sourcePath, transcodeCase.maxLayers, reduce);
    const Planes actual = decode(output, 0, 0);
    if(expected.empty() || actual != expected)
    {
      fprintf(stderr, "%s %s: decoded samples differ\n", name.c_str(), transcodeCase.name);
      ok = false;
    }
  }
  return ok;
}

bool quantizationMatchesLevels(const std::string& path)
{
  std::ifstream file(path, std::ios::binary);
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
  const uint16_t MARKER_COD = 0xFF52;
  const uint16_t MARKER_QCD = 0xFF5C;
  const uint16_t MARKER_SOT = 0xFF90;
  uint32_t levels = 0;
  uint32_t quantizationLength = 0;
  uint32_t bytesPerBand = 0;
  for(size_t position = 2; position + 10 < bytes.size();)
  {
    const uint32_t marker = ((uint32_t)bytes[position] << 8) | bytes[position + 1];
    const uint32_t length = ((uint32_t)bytes[position + 2] << 8) | bytes[position + 3];
    if(marker == MARKER_SOT)
      break;
    if(marker == MARKER_COD)
      levels = bytes[position + 9];
    if(marker == MARKER_QCD)
    {
      quantizationLength = length;
      bytesPerBand = (bytes[position + 4] & 0x1F) == 0 ? 1 : 2;
    }
    position += 2U + length;
  }
  return quantizationLength == 3U + bytesPerBand * (3U * levels + 1U);
}

bool checkGeneratedSource(const std::filesystem::path& directory, const SourceCase& source)
{
  const std::string sourcePath = (directory / (std::string(source.name) + ".j2k")).string();
  if(!compress(sourcePath, source))
  {
    fprintf(stderr, "%s: compress failed\n", source.name);
    return false;
  }
  if(!losslessFullDecodeMatchesImage(decode(sourcePath, 0, 0)))
  {
    fprintf(stderr, "%s: source does not decode to the image\n", source.name);
    return false;
  }
  bool ok = checkTranscodes(directory, source.name, sourcePath);
  const std::string reduced =
      (directory / (std::string(source.name) + "_two_resolutions.j2k")).string();
  if(!quantizationMatchesLevels(reduced))
  {
    fprintf(stderr, "%s: QCD length does not match the kept resolutions\n", source.name);
    ok = false;
  }
  return ok;
}
} // namespace

int main(int argc, char** argv)
{
  const bool checkFiles = argc > 1;
  auto directory = std::filesystem::temp_directory_path() /
                   (checkFiles ? "grk_transcode_packets_files_test" : "grk_transcode_packets_test");
  std::filesystem::create_directories(directory);
  grk_initialize(nullptr, 0, nullptr);
  bool ok = true;
  if(!checkFiles)
    for(const auto& source : SOURCE_CASES)
      ok = checkGeneratedSource(directory, source) && ok;
  for(int i = 1; i < argc; ++i)
    ok = checkTranscodes(directory, std::filesystem::path(argv[i]).stem().string(), argv[i]) && ok;
  grk_deinitialize();
  std::filesystem::remove_all(directory);
  return ok ? 0 : 1;
}
