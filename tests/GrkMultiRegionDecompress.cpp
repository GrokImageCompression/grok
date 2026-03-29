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

#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include "grk_apps_config.h"
#include "grok.h"
#include "spdlog/spdlog.h"
#include "GrkMultiRegionDecompress.h"

template<size_t N>
void safe_strcpy(char (&dest)[N], const char* src)
{
  size_t len = strnlen(src, N - 1);
  memcpy(dest, src, len);
  dest[len] = '\0';
}

namespace grk
{

struct CodecDeleter
{
  void operator()(grk_object* codec) const
  {
    if(codec)
      grk_object_unref(codec);
  }
};
using CodecPtr = std::unique_ptr<grk_object, CodecDeleter>;

struct Region
{
  uint32_t x0, y0, x1, y1;
  const char* label;
};

static bool decompressRegion(grk_object* codec, const Region& region, const grk_header_info& header)
{
  spdlog::info("Decompressing region '{}': ({},{}) - ({},{})", region.label, region.x0, region.y0,
               region.x1, region.y1);

  auto start = std::chrono::high_resolution_clock::now();

  grk_decompress_parameters params{};
  params.asynchronous = true;
  params.simulate_synchronous = true;
  params.core.tile_cache_strategy = GRK_TILE_CACHE_NONE;
  params.dw_x0 = region.x0;
  params.dw_y0 = region.y0;
  params.dw_x1 = region.x1;
  params.dw_y1 = region.y1;

  spdlog::info("  calling grk_decompress_update...");
  if(!grk_decompress_update(&params, codec))
  {
    spdlog::error("grk_decompress_update failed for region '{}'", region.label);
    return false;
  }

  spdlog::info("  calling grk_decompress...");
  if(!grk_decompress(codec, nullptr))
  {
    spdlog::error("grk_decompress failed for region '{}'", region.label);
    return false;
  }

  spdlog::info("  starting row-by-row wait...");

  // Wait row-by-row using tile height intervals
  uint32_t tileHeight = header.t_height;
  if(tileHeight == 0)
    tileHeight = region.y1 - region.y0;

  spdlog::info("  starting row-by-row wait (tileHeight={})...", tileHeight);
  for(uint32_t rowY = region.y0; rowY < region.y1; rowY += tileHeight)
  {
    grk_wait_swath swath{};
    swath.x0 = region.x0;
    swath.y0 = rowY;
    swath.x1 = region.x1;
    swath.y1 = std::min(rowY + tileHeight, region.y1);
    spdlog::info("  waiting for swath row y={}...", rowY);
    grk_decompress_wait(codec, &swath);
  }

  spdlog::info("  calling final grk_decompress_wait(nullptr)...");
  // Final wait with null swath to join async workers and clean up
  // decompress state, so the codec is ready for a new decompress cycle.
  grk_decompress_wait(codec, nullptr);
  spdlog::info("  final wait complete.");

  auto end = std::chrono::high_resolution_clock::now();
  double ms = std::chrono::duration<double, std::milli>(end - start).count();

  // Verify we can retrieve tile images for tiles in this region
  uint32_t tileWidth = header.t_width;
  uint32_t tx0 = region.x0 / tileWidth;
  uint32_t ty0 = region.y0 / tileHeight;
  uint32_t tx1 = (region.x1 + tileWidth - 1) / tileWidth;
  uint32_t ty1 = (region.y1 + tileHeight - 1) / tileHeight;
  uint32_t numTileCols = header.t_grid_width;
  uint32_t tilesChecked = 0;

  for(uint32_t ty = ty0; ty < ty1; ++ty)
  {
    for(uint32_t tx = tx0; tx < tx1; ++tx)
    {
      uint16_t tileIdx = static_cast<uint16_t>(ty * numTileCols + tx);
      grk_image* tileImg = grk_decompress_get_tile_image(codec, tileIdx, false);
      if(!tileImg)
      {
        // For single-tile images, data is in the composite image
        tileImg = grk_decompress_get_image(codec);
      }
      if(!tileImg)
      {
        spdlog::error("No tile image for tile {} in region '{}'", tileIdx, region.label);
        return false;
      }
      for(uint16_t c = 0; c < tileImg->numcomps; ++c)
      {
        if(!tileImg->comps[c].data)
        {
          spdlog::error("Tile {} component {} has null data in region '{}'", tileIdx, c,
                        region.label);
          return false;
        }
      }
      tilesChecked++;
    }
  }

  spdlog::info("Region '{}' OK: {} tiles verified in {:.1f} ms", region.label, tilesChecked, ms);
  return true;
}

int GrkMultiRegionDecompress::main(int argc, char** argv)
{
  grk_initialize(nullptr, 0, nullptr);

  if(argc != 2)
  {
    spdlog::error("Usage: {} <input_file>", argv[0]);
    return EXIT_FAILURE;
  }

  std::string inputFile(argv[1]);

  // Create codec and read header
  grk_decompress_parameters initParams{};
  grk_stream_params streamParams{};
  safe_strcpy(streamParams.file, inputFile.data());

  CodecPtr codec(grk_decompress_init(&streamParams, &initParams));
  if(!codec)
  {
    spdlog::error("Failed to initialize decompressor for {}", inputFile);
    return EXIT_FAILURE;
  }

  grk_header_info headerInfo{};
  if(!grk_decompress_read_header(codec.get(), &headerInfo))
  {
    spdlog::error("Failed to read header from {}", inputFile);
    return EXIT_FAILURE;
  }

  const grk_image* image = grk_decompress_get_image(codec.get());
  if(!image)
  {
    spdlog::error("Failed to get image from {}", inputFile);
    return EXIT_FAILURE;
  }

  uint32_t imgW = image->x1 - image->x0;
  uint32_t imgH = image->y1 - image->y0;
  spdlog::info("Image: {}x{}, tiles: {}x{} ({}x{} grid)", imgW, imgH, headerInfo.t_width,
               headerInfo.t_height, headerInfo.t_grid_width, headerInfo.t_grid_height);

  // Build regions: four quadrants plus a center crop
  uint32_t halfW = imgW / 2;
  uint32_t halfH = imgH / 2;
  uint32_t quarterW = imgW / 4;
  uint32_t quarterH = imgH / 4;

  std::vector<Region> regions;
  regions.push_back({image->x0, image->y0, image->x0 + halfW, image->y0 + halfH, "top-left"});
  regions.push_back({image->x0 + halfW, image->y0, image->x1, image->y0 + halfH, "top-right"});
  regions.push_back({image->x0, image->y0 + halfH, image->x0 + halfW, image->y1, "bottom-left"});
  regions.push_back({image->x0 + halfW, image->y0 + halfH, image->x1, image->y1, "bottom-right"});
  regions.push_back({image->x0 + quarterW, image->y0 + quarterH, image->x1 - quarterW,
                     image->y1 - quarterH, "center"});

  // Decompress each region using the SAME codec (no close/reopen)
  int failures = 0;
  for(const auto& region : regions)
  {
    if(!decompressRegion(codec.get(), region, headerInfo))
    {
      spdlog::error("FAILED: region '{}'", region.label);
      failures++;
    }
  }

  if(failures > 0)
  {
    spdlog::error("{} of {} regions failed", failures, regions.size());
    return EXIT_FAILURE;
  }

  spdlog::info("All {} regions decompressed successfully from the same codec", regions.size());
  return EXIT_SUCCESS;
}

} // namespace grk
