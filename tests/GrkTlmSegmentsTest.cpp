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

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "grok.h"
#include "NoiseSample.h"

namespace
{
// 105 x 105 tiles is more than one TLM segment can list
const uint32_t TILE_SIZE = 8;
const uint32_t IMAGE_SIZE = 105 * TILE_SIZE;
const uint32_t NUM_TILES = 105 * 105;
const uint32_t TLM_ENTRY_BYTES = 6;
const size_t SOT_BYTES = 12;
const uint16_t MARKER_SOT = 0xFF90;
const uint16_t MARKER_TLM = 0xFF55;
const uint16_t MARKER_EOC = 0xFFD9;

uint16_t readU16(const std::vector<uint8_t>& bytes, size_t position)
{
  return (uint16_t)((bytes[position] << 8) | bytes[position + 1]);
}

uint32_t readU32(const std::vector<uint8_t>& bytes, size_t position)
{
  return ((uint32_t)readU16(bytes, position) << 16) | readU16(bytes, position + 2);
}

bool compress(const std::string& path, bool writeTLM)
{
  grk_image_comp compParams = {};
  compParams.dx = 1;
  compParams.dy = 1;
  compParams.w = IMAGE_SIZE;
  compParams.h = IMAGE_SIZE;
  compParams.prec = 8;
  grk_image* image = grk_image_new(1, &compParams, GRK_CLRSPC_GRAY, true);
  if(!image)
    return false;
  auto& comp = image->comps[0];
  auto data = static_cast<int32_t*>(comp.data);
  for(uint32_t y = 0; y < comp.h; ++y)
    for(uint32_t x = 0; x < comp.w; ++x)
      data[(size_t)y * comp.stride + x] = noiseSample(x, y);

  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_JP2;
  parameters.numlayers = 1;
  parameters.numresolution = 1;
  parameters.write_tlm = writeTLM;
  parameters.tile_size_on = true;
  parameters.t_width = TILE_SIZE;
  parameters.t_height = TILE_SIZE;

  grk_stream_params streamParams = {};
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());
  grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
  bool ok = codec && grk_compress(codec, nullptr) != 0;
  if(codec)
    grk_object_unref(codec);
  grk_object_unref(&image->obj);
  return ok;
}

bool transcodeWithTLM(const std::string& source, const std::string& destination)
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
    parameters.cod_format = GRK_FMT_JP2;
    parameters.write_tlm = true;
    grk_stream_params destinationParams = {};
    snprintf(destinationParams.file, sizeof(destinationParams.file), "%s", destination.c_str());
    grk_stream_params transcodeSource = {};
    snprintf(transcodeSource.file, sizeof(transcodeSource.file), "%s", source.c_str());
    ok = grk_transcode(&transcodeSource, &destinationParams, &parameters,
                       grk_decompress_get_image(codec)) != 0;
  }
  grk_object_unref(codec);
  return ok;
}

bool tlmSegmentsMatchTileParts(const std::string& label, const std::string& path)
{
  std::ifstream file(path, std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
  std::vector<std::pair<uint16_t, uint32_t>> entries;
  uint32_t numSegments = 0;
  const uint8_t socAndSiz[] = {0xFF, 0x4F, 0xFF, 0x51};
  auto soc = std::search(bytes.begin(), bytes.end(), std::begin(socAndSiz), std::end(socAndSiz));
  if(soc == bytes.end())
  {
    fprintf(stderr, "%s: no code stream found\n", label.c_str());
    return false;
  }
  size_t position = (size_t)(soc - bytes.begin()) + 2;
  while(position + 4 <= bytes.size() && readU16(bytes, position) != MARKER_SOT)
  {
    uint16_t length = readU16(bytes, position + 2);
    if(readU16(bytes, position) == MARKER_TLM)
    {
      if(bytes[position + 4] != numSegments)
      {
        fprintf(stderr, "%s: segment %u has Ztlm %u\n", label.c_str(), numSegments,
                bytes[position + 4]);
        return false;
      }
      numSegments++;
      for(size_t entry = position + 6; entry < position + 2 + length; entry += TLM_ENTRY_BYTES)
        entries.push_back({readU16(bytes, entry), readU32(bytes, entry + 2)});
    }
    position += 2u + length;
  }
  if(numSegments < 2 || entries.size() != NUM_TILES)
  {
    fprintf(stderr, "%s: %u segments list %zu tile parts\n", label.c_str(), numSegments,
            entries.size());
    return false;
  }
  for(const auto& [tileIndex, length] : entries)
  {
    if(position + SOT_BYTES > bytes.size())
    {
      fprintf(stderr, "%s: tile part lengths run past the end of the stream\n", label.c_str());
      return false;
    }
    uint16_t isot = readU16(bytes, position + 4);
    uint32_t psot = readU32(bytes, position + 6);
    if(readU16(bytes, position) != MARKER_SOT || isot != tileIndex || psot != length)
    {
      fprintf(stderr, "%s: TLM lists tile %u length %u, stream has tile %u length %u\n",
              label.c_str(), tileIndex, length, isot, psot);
      return false;
    }
    position += length;
  }
  if(position + 2 > bytes.size() || readU16(bytes, position) != MARKER_EOC)
  {
    fprintf(stderr, "%s: tile parts do not end at the EOC marker\n", label.c_str());
    return false;
  }
  return true;
}

bool decodeMatchesSource(const std::string& label, const std::string& path)
{
  grk_stream_params streamParams = {};
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());
  grk_decompress_parameters params = {};
  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
  {
    fprintf(stderr, "%s: grk_decompress_init failed\n", label.c_str());
    return false;
  }
  grk_header_info header = {};
  bool ok = grk_decompress_read_header(codec, &header) && grk_decompress_update(&params, codec) &&
            grk_decompress(codec, nullptr);
  grk_image* image = ok ? grk_decompress_get_image(codec) : nullptr;
  if(!image || !image->comps[0].data)
  {
    fprintf(stderr, "%s: decode failed\n", label.c_str());
    grk_object_unref(codec);
    return false;
  }
  const auto& comp = image->comps[0];
  for(uint32_t y = 0; ok && y < comp.h; ++y)
    for(uint32_t x = 0; ok && x < comp.w; ++x)
    {
      size_t index = (size_t)y * comp.stride + x;
      int32_t sample = comp.data_type == GRK_INT_16 ? static_cast<int16_t*>(comp.data)[index]
                                                    : static_cast<int32_t*>(comp.data)[index];
      ok = sample == noiseSample(x, y);
    }
  if(!ok)
    fprintf(stderr, "%s: decoded samples differ\n", label.c_str());
  grk_object_unref(codec);
  return ok;
}
} // namespace

int main(void)
{
  auto directory = std::filesystem::temp_directory_path() / "grk_tlm_segments_test";
  std::filesystem::create_directories(directory);
  std::string compressed = (directory / "compressed.jp2").string();
  std::string plain = (directory / "plain.jp2").string();
  std::string transcoded = (directory / "transcoded.jp2").string();

  grk_initialize(nullptr, 0, nullptr);
  const bool writeTLM = true;
  bool ok = compress(compressed, writeTLM) && compress(plain, !writeTLM) &&
            transcodeWithTLM(plain, transcoded) &&
            tlmSegmentsMatchTileParts("compressed", compressed) &&
            tlmSegmentsMatchTileParts("transcoded", transcoded) &&
            decodeMatchesSource("compressed", compressed) &&
            decodeMatchesSource("transcoded", transcoded);
  grk_deinitialize();
  std::filesystem::remove_all(directory);
  if(!ok)
    fprintf(stderr, "TLM segments test failed\n");
  return ok ? 0 : 1;
}
