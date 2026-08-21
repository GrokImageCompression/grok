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

// a stream whose last tile part of every tile has TPsot == TNsot must be
// rejected. the parse thread notices that only after every tile has been
// scheduled, so it sets the failure flag while per-tile completions are still
// running, and a completion that overwrote the flag used to erase the failure.
// round-robin tile parts across many tiles is what makes the overlap wide.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "grok.h"

namespace
{
  const uint32_t IMAGE_WIDTH = 512;
  const uint32_t IMAGE_HEIGHT = 512;
  const uint32_t TILE_WIDTH = 32;
  const uint32_t TILE_HEIGHT = 32;
  const uint16_t NUM_COMPONENTS = 3;
  const uint8_t PRECISION = 8;
  const uint8_t NUM_RESOLUTIONS = 6;
  // 'R' in the LRCP progression: one tile part per resolution
  const uint8_t TILE_PART_DIVIDER = 'R';
  const uint8_t MIN_TILE_PARTS_PER_TILE = 3;
  // a small pool keeps the parse thread and the tile completions contending
  const uint32_t NUM_THREADS = 4;
  // the clobber hit about one decode in 20 of this geometry before the fix
  const uint32_t DECODES = 200;

  const uint16_t SOT = 0xff90;
  const uint16_t EOC = 0xffd9;
  const size_t SOT_SEGMENT_BYTES = 12;
  const size_t TPSOT_OFFSET = 10;
  const size_t TNSOT_OFFSET = 11;

  struct TilePart
  {
    size_t offset;
    size_t length;
    uint8_t tilePartIndex;
  };

  void silence(const char*, void*) {}

  uint16_t readBigEndian16(const uint8_t* p)
  {
    return (uint16_t)((p[0] << 8) | p[1]);
  }

  uint32_t readBigEndian32(const uint8_t* p)
  {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
  }

  int32_t expectedSample(uint32_t x, uint32_t y, uint16_t c)
  {
    return (int32_t)((x * 7 + y * 13 + c * 53 + ((x ^ y) & 31) * 3) & 0xFF);
  }

  grk_image* makeImage(void)
  {
    grk_image_comp params[NUM_COMPONENTS] = {};
    for(uint16_t c = 0; c < NUM_COMPONENTS; ++c)
    {
      params[c].dx = 1;
      params[c].dy = 1;
      params[c].w = IMAGE_WIDTH;
      params[c].h = IMAGE_HEIGHT;
      params[c].prec = PRECISION;
      params[c].sgnd = false;
    }
    grk_image* image = grk_image_new(NUM_COMPONENTS, params, GRK_CLRSPC_SRGB, true);
    if(!image)
      return nullptr;
    for(uint16_t c = 0; c < NUM_COMPONENTS; ++c)
    {
      auto* data = static_cast<int32_t*>(image->comps[c].data);
      if(!data)
      {
        grk_object_unref(&image->obj);
        return nullptr;
      }
      uint32_t stride = image->comps[c].stride;
      for(uint32_t y = 0; y < IMAGE_HEIGHT; ++y)
        for(uint32_t x = 0; x < IMAGE_WIDTH; ++x)
          data[(size_t)y * stride + x] = expectedSample(x, y, c);
    }
    return image;
  }

  bool compress(std::vector<uint8_t>& codestream)
  {
    grk_image* image = makeImage();
    if(!image)
    {
      fprintf(stderr, "could not build the source image\n");
      return false;
    }
    grk_cparameters parameters = {};
    grk_compress_set_default_params(&parameters);
    parameters.cod_format = GRK_FMT_J2K;
    parameters.irreversible = false;
    parameters.tile_size_on = true;
    parameters.t_width = TILE_WIDTH;
    parameters.t_height = TILE_HEIGHT;
    parameters.numlayers = 1;
    parameters.numresolution = NUM_RESOLUTIONS;
    parameters.enable_tile_part_generation = true;
    parameters.new_tile_part_progression_divider = TILE_PART_DIVIDER;

    codestream.assign((size_t)IMAGE_WIDTH * IMAGE_HEIGHT * NUM_COMPONENTS * 2 + (1u << 20), 0);
    grk_stream_params streamParams = {};
    streamParams.buf = codestream.data();
    streamParams.buf_len = codestream.size();

    grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
    uint64_t written = 0;
    if(!codec)
      fprintf(stderr, "grk_compress_init failed\n");
    else
    {
      written = grk_compress(codec, nullptr);
      if(!written)
        fprintf(stderr, "grk_compress failed\n");
      grk_object_unref(codec);
    }
    grk_object_unref(&image->obj);
    if(!written)
      return false;
    codestream.resize(written);
    return true;
  }

  // collects the tile parts of every tile, keyed by tile index
  bool collectTileParts(const std::vector<uint8_t>& codestream, size_t& mainHeaderBytes,
                        std::vector<std::vector<TilePart>>& perTile, uint8_t& tilePartsPerTile)
  {
    size_t pos = 0;
    while(pos + 1 < codestream.size() && readBigEndian16(&codestream[pos]) != SOT)
      ++pos;
    if(pos + SOT_SEGMENT_BYTES > codestream.size())
    {
      fprintf(stderr, "no SOT marker in the compressed codestream\n");
      return false;
    }
    mainHeaderBytes = pos;
    while(pos + 1 < codestream.size() && readBigEndian16(&codestream[pos]) == SOT)
    {
      if(pos + SOT_SEGMENT_BYTES > codestream.size())
      {
        fprintf(stderr, "truncated SOT segment at offset %zu\n", pos);
        return false;
      }
      uint16_t tileIndex = readBigEndian16(&codestream[pos + 4]);
      uint32_t segmentBytes = readBigEndian32(&codestream[pos + 6]);
      uint8_t tilePartIndex = codestream[pos + TPSOT_OFFSET];
      if(segmentBytes == 0 || pos + segmentBytes > codestream.size())
      {
        fprintf(stderr, "SOT at offset %zu has Psot %u, which does not fit the stream\n", pos,
                segmentBytes);
        return false;
      }
      if(tileIndex >= perTile.size())
        perTile.resize((size_t)tileIndex + 1);
      perTile[tileIndex].push_back({pos, segmentBytes, tilePartIndex});
      pos += segmentBytes;
    }
    if(pos + 1 >= codestream.size() || readBigEndian16(&codestream[pos]) != EOC)
    {
      fprintf(stderr, "expected EOC after the last tile part, offset %zu\n", pos);
      return false;
    }
    if(perTile.empty())
    {
      fprintf(stderr, "no tile parts found\n");
      return false;
    }
    tilePartsPerTile = (uint8_t)perTile[0].size();
    for(size_t t = 0; t < perTile.size(); ++t)
    {
      if(perTile[t].size() != tilePartsPerTile)
      {
        fprintf(stderr, "tile %zu has %zu tile parts, tile 0 has %u\n", t, perTile[t].size(),
                tilePartsPerTile);
        return false;
      }
    }
    if(tilePartsPerTile < MIN_TILE_PARTS_PER_TILE)
    {
      fprintf(stderr, "%u tile parts per tile is too few, need at least %u\n", tilePartsPerTile,
              MIN_TILE_PARTS_PER_TILE);
      return false;
    }
    return true;
  }

  // round-robins the tile parts across tiles and patches every TNsot down by one,
  // so the last tile part of each tile carries TPsot == TNsot
  bool makeIllegalStream(const std::vector<uint8_t>& codestream, std::vector<uint8_t>& patched,
                         size_t& numTiles, uint8_t& tilePartsPerTile)
  {
    size_t mainHeaderBytes = 0;
    std::vector<std::vector<TilePart>> perTile;
    if(!collectTileParts(codestream, mainHeaderBytes, perTile, tilePartsPerTile))
      return false;
    numTiles = perTile.size();

    patched.assign(codestream.begin(), codestream.begin() + (long)mainHeaderBytes);
    for(uint8_t k = 0; k < tilePartsPerTile; ++k)
    {
      for(const auto& tile : perTile)
      {
        const auto& part = tile[k];
        if(part.tilePartIndex != k)
        {
          fprintf(stderr, "tile part %u of a tile reports TPsot %u\n", k, part.tilePartIndex);
          return false;
        }
        size_t start = patched.size();
        patched.insert(patched.end(), codestream.begin() + (long)part.offset,
                       codestream.begin() + (long)(part.offset + part.length));
        patched[start + TNSOT_OFFSET] = (uint8_t)(tilePartsPerTile - 1);
      }
    }
    patched.push_back((uint8_t)(EOC >> 8));
    patched.push_back((uint8_t)(EOC & 0xff));
    return true;
  }

  // true when the decode reported success, which the illegal stream must never do
  bool decodeSucceeds(std::vector<uint8_t>& patched)
  {
    grk_decompress_parameters params = {};
    grk_stream_params streamParams = {};
    streamParams.buf = patched.data();
    streamParams.buf_len = patched.size();

    grk_object* codec = grk_decompress_init(&streamParams, &params);
    if(!codec)
    {
      fprintf(stderr, "grk_decompress_init failed\n");
      return false;
    }
    bool succeeded = false;
    grk_header_info headerInfo = {};
    if(!grk_decompress_read_header(codec, &headerInfo))
      fprintf(stderr, "grk_decompress_read_header failed\n");
    else
      succeeded = grk_decompress(codec, nullptr) != 0;
    grk_object_unref(codec);
    return succeeded;
  }
} // namespace

int main(void)
{
#if defined(_WIN32)
  _putenv_s("GRK_MERCURY", "");
#else
  unsetenv("GRK_MERCURY");
#endif

  grk_msg_handlers handlers = {};
  handlers.info_callback = silence;
  handlers.warn_callback = silence;
  handlers.error_callback = silence;
  grk_set_msg_handlers(handlers);

  grk_initialize(nullptr, NUM_THREADS, nullptr);

  std::vector<uint8_t> codestream;
  std::vector<uint8_t> patched;
  size_t numTiles = 0;
  uint8_t tilePartsPerTile = 0;
  int result = 0;
  if(!compress(codestream) || !makeIllegalStream(codestream, patched, numTiles, tilePartsPerTile))
    result = 1;

  uint32_t decodesThatSucceeded = 0;
  for(uint32_t i = 0; i < DECODES && result == 0; ++i)
  {
    if(decodeSucceeds(patched))
    {
      ++decodesThatSucceeded;
      fprintf(stderr, "decode %u of the illegal stream reported success\n", i);
    }
  }
  if(decodesThatSucceeded)
    result = 1;

  if(result == 0)
    printf("%u decodes of %zu tiles with %u tile parts each all rejected the stream\n", DECODES,
           numTiles, tilePartsPerTile);
  else if(decodesThatSucceeded)
    fprintf(stderr, "%u of %u decodes accepted a stream with TPsot == TNsot\n",
            decodesThatSucceeded, DECODES);

  grk_deinitialize();
  return result;
}
