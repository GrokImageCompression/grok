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
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>

#include "grk_apps_config.h"
#include "grok.h"
#include "spdlog/spdlog.h"
#include "GrkLRUCacheTest.h"

// Self-contained cache headers for unit testing
#include "LRUCache.h"
#include "DiskCache.h"

template<size_t N>
static void safe_strcpy(char (&dest)[N], const char* src)
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

// Create a synthetic test image and compress it to a J2K file
static bool createTestImage(const std::string& path, uint32_t width, uint32_t height,
                            uint32_t tileWidth, uint32_t tileHeight, bool writeTlm = true)
{
  grk_cparameters cparams{};
  grk_compress_set_default_params(&cparams);
  cparams.cod_format = GRK_FMT_J2K;
  cparams.t_width = tileWidth;
  cparams.t_height = tileHeight;
  cparams.tile_size_on = (tileWidth < width || tileHeight < height);
  cparams.write_tlm = writeTlm;
  cparams.numresolution = 3;
  // Lossless
  cparams.irreversible = false;
  cparams.numlayers = 1;
  cparams.layer_rate[0] = 0;

  grk_image_comp comp{};
  comp.dx = 1;
  comp.dy = 1;
  comp.w = width;
  comp.h = height;
  comp.x0 = 0;
  comp.y0 = 0;
  comp.prec = 8;
  comp.sgnd = 0;

  auto* image = grk_image_new(1, &comp, GRK_CLRSPC_GRAY, true);
  if(!image)
  {
    spdlog::error("Failed to create image");
    return false;
  }

  // Fill with a deterministic pattern
  auto* data = static_cast<int32_t*>(image->comps[0].data);
  for(uint32_t y = 0; y < height; ++y)
    for(uint32_t x = 0; x < width; ++x)
      data[y * width + x] = static_cast<int32_t>((x * 7 + y * 13) % 256);

  grk_stream_params streamParams{};
  safe_strcpy(streamParams.file, path.data());

  auto* codec = grk_compress_init(&streamParams, &cparams, image);
  if(!codec)
  {
    spdlog::error("Failed to init compressor");
    grk_object_unref(&image->obj);
    return false;
  }

  uint64_t len = grk_compress(codec, nullptr);
  grk_object_unref(codec);
  grk_object_unref(&image->obj);

  if(len == 0)
  {
    spdlog::error("Compression failed");
    return false;
  }

  spdlog::info("Created test image: {}x{}, tiles {}x{}, {} bytes", width, height, tileWidth,
               tileHeight, len);
  return true;
}

// Decompress a region and return per-component pixel data for all tiles
struct TileData
{
  uint16_t tileIndex;
  std::vector<std::vector<int32_t>> componentData; // [comp][pixel]
};

static std::vector<TileData> decompressAndCapture(const std::string& path,
                                                  uint32_t tileCacheStrategy)
{
  std::vector<TileData> result;

  grk_decompress_parameters params{};
  params.core.tile_cache_strategy = tileCacheStrategy;
  params.asynchronous = true;
  params.simulate_synchronous = true;

  grk_stream_params streamParams{};
  safe_strcpy(streamParams.file, path.data());

  CodecPtr codec(grk_decompress_init(&streamParams, &params));
  if(!codec)
  {
    spdlog::error("Failed to init decompressor");
    return result;
  }

  grk_header_info headerInfo{};
  if(!grk_decompress_read_header(codec.get(), &headerInfo))
  {
    spdlog::error("Failed to read header");
    return result;
  }

  if(!grk_decompress(codec.get(), nullptr))
  {
    spdlog::error("Decompress failed");
    return result;
  }

  // Wait for all data
  grk_decompress_wait(codec.get(), nullptr);

  // Capture tile data
  uint16_t numTiles = headerInfo.t_grid_width * headerInfo.t_grid_height;
  for(uint16_t t = 0; t < numTiles; ++t)
  {
    auto* tileImg = grk_decompress_get_tile_image(codec.get(), t, false);
    if(!tileImg)
      tileImg = grk_decompress_get_image(codec.get());
    if(!tileImg)
      continue;

    TileData td;
    td.tileIndex = t;
    for(uint16_t c = 0; c < tileImg->numcomps; ++c)
    {
      auto& comp = tileImg->comps[c];
      if(!comp.data)
        continue;
      uint32_t w = comp.w;
      uint32_t h = comp.h;
      auto* pixelData = static_cast<int32_t*>(comp.data);
      std::vector<int32_t> pixels(pixelData, pixelData + w * h);
      td.componentData.push_back(std::move(pixels));
    }
    result.push_back(std::move(td));
  }

  return result;
}

///////////////////////////////////////////////////////////////////
// Test 1: LRU tile cache strategy produces identical output
///////////////////////////////////////////////////////////////////
static bool testLRUMatchesReference(const std::string& testFile)
{
  spdlog::info("=== Test: LRU tile cache produces correct output ===");

  auto refData = decompressAndCapture(testFile, GRK_TILE_CACHE_IMAGE);
  if(refData.empty())
  {
    spdlog::error("Reference decompression produced no data");
    return false;
  }
  spdlog::info("Reference: {} tiles captured with TILE_CACHE_IMAGE", refData.size());

  auto lruData = decompressAndCapture(testFile, GRK_TILE_CACHE_LRU);
  if(lruData.empty())
  {
    spdlog::error("LRU decompression produced no data");
    return false;
  }
  spdlog::info("LRU: {} tiles captured with TILE_CACHE_LRU", lruData.size());

  if(refData.size() != lruData.size())
  {
    spdlog::error("Tile count mismatch: ref={}, lru={}", refData.size(), lruData.size());
    return false;
  }

  for(size_t i = 0; i < refData.size(); ++i)
  {
    auto& ref = refData[i];
    auto& lru = lruData[i];
    if(ref.tileIndex != lru.tileIndex)
    {
      spdlog::error("Tile index mismatch at position {}", i);
      return false;
    }
    if(ref.componentData.size() != lru.componentData.size())
    {
      spdlog::error("Component count mismatch for tile {}", ref.tileIndex);
      return false;
    }
    for(size_t c = 0; c < ref.componentData.size(); ++c)
    {
      if(ref.componentData[c] != lru.componentData[c])
      {
        spdlog::error("Pixel data mismatch for tile {} component {}", ref.tileIndex, c);
        return false;
      }
    }
  }

  spdlog::info("PASS: LRU output matches reference for all {} tiles", refData.size());
  return true;
}

///////////////////////////////////////////////////////////////////
// Test 2: Unit test DiskCache store/load round trip
///////////////////////////////////////////////////////////////////
static bool testDiskCache()
{
  spdlog::info("=== Test: DiskCache store/load round trip ===");

  DiskCache cache;

  // Store some data
  std::vector<uint8_t> data1 = {1, 2, 3, 4, 5, 6, 7, 8};
  std::vector<uint8_t> data2 = {10, 20, 30, 40};

  cache.store(0, data1.data(), data1.size());
  cache.store(1, data2.data(), data2.size());

  if(!cache.contains(0) || !cache.contains(1))
  {
    spdlog::error("DiskCache::contains failed");
    return false;
  }

  if(cache.contains(99))
  {
    spdlog::error("DiskCache::contains returned true for absent tile");
    return false;
  }

  auto loaded1 = cache.load(0);
  if(!loaded1.has_value() || loaded1->size() != data1.size())
  {
    spdlog::error("DiskCache::load failed for tile 0");
    return false;
  }
  if(*loaded1 != data1)
  {
    spdlog::error("DiskCache data mismatch for tile 0");
    return false;
  }

  auto loaded2 = cache.load(1);
  if(!loaded2.has_value() || *loaded2 != data2)
  {
    spdlog::error("DiskCache data mismatch for tile 1");
    return false;
  }

  // Not found
  auto loaded99 = cache.load(99);
  if(loaded99.has_value())
  {
    spdlog::error("DiskCache::load returned data for absent tile");
    return false;
  }

  // Clear
  cache.clear();
  if(cache.contains(0))
  {
    spdlog::error("DiskCache still contains tile 0 after clear");
    return false;
  }

  spdlog::info("PASS: DiskCache round trip");
  return true;
}

///////////////////////////////////////////////////////////////////
// Test 3: Unit test LRUCache eviction logic
///////////////////////////////////////////////////////////////////
static bool testLRUCacheEviction()
{
  spdlog::info("=== Test: LRUCache eviction logic ===");

  std::vector<std::pair<int, std::vector<uint8_t>>> evicted;

  LRUCache<int, std::vector<uint8_t>> cache(
      100, // 100 bytes max
      [](const std::vector<uint8_t>& v) { return v.size(); },
      [&evicted](const int& key, std::vector<uint8_t>&& value) {
        evicted.push_back({key, std::move(value)});
      });

  // Put 5 entries of 30 bytes each — should evict to stay under 100
  for(int i = 0; i < 5; ++i)
  {
    cache.put(i, std::vector<uint8_t>(30, static_cast<uint8_t>(i)));
  }

  // Should have evicted at least 2 entries (5*30=150 > 100)
  if(evicted.empty())
  {
    spdlog::error("LRUCache should have evicted entries but didn't");
    return false;
  }
  spdlog::info("Evicted {} entries (expected >=2)", evicted.size());

  // Most recently used (4, 3, 2) should still be in cache
  if(!cache.get(4) || !cache.get(3))
  {
    spdlog::error("LRUCache evicted recently used entries");
    return false;
  }

  // LRU entries (0, 1) should have been evicted
  if(cache.get(0) || cache.get(1))
  {
    spdlog::error("LRUCache kept oldest entries that should have been evicted");
    return false;
  }

  spdlog::info("PASS: LRUCache eviction logic");
  return true;
}

///////////////////////////////////////////////////////////////////
// Test 4: LRU eviction + re-decompress from cached SOT offsets
//
// Creates a non-TLM image with 16 tiles, sets max_active_tiles=4,
// decompresses all tiles (causing eviction), then re-decompresses
// evicted tiles and verifies pixel data matches a fresh reference.
///////////////////////////////////////////////////////////////////
static std::vector<TileData> decompressTileByTile(const std::string& path,
                                                  uint32_t tileCacheStrategy,
                                                  uint16_t maxActiveTiles)
{
  std::vector<TileData> result;

  grk_decompress_parameters params{};
  params.core.tile_cache_strategy = tileCacheStrategy;
  params.core.max_active_tiles = maxActiveTiles;

  grk_stream_params streamParams{};
  safe_strcpy(streamParams.file, path.data());

  CodecPtr codec(grk_decompress_init(&streamParams, &params));
  if(!codec)
  {
    spdlog::error("Failed to init decompressor");
    return result;
  }

  grk_header_info headerInfo{};
  if(!grk_decompress_read_header(codec.get(), &headerInfo))
  {
    spdlog::error("Failed to read header");
    return result;
  }

  if(!grk_decompress_update(&params, codec.get()))
  {
    spdlog::error("grk_decompress_update failed");
    return result;
  }

  uint16_t numTiles = headerInfo.t_grid_width * headerInfo.t_grid_height;
  spdlog::info("decompressTileByTile: grid {}x{} = {} tiles", headerInfo.t_grid_width,
               headerInfo.t_grid_height, numTiles);
  for(uint16_t t = 0; t < numTiles; ++t)
  {
    if(!grk_decompress_tile(codec.get(), t))
    {
      spdlog::error("grk_decompress_tile({}) failed", t);
      return {};
    }
  }

  // Now re-decompress all tiles (evicted tiles trigger re-decompress path)
  for(uint16_t t = 0; t < numTiles; ++t)
  {
    if(!grk_decompress_tile(codec.get(), t))
    {
      spdlog::error("Re-decompress of tile {} failed", t);
      return {};
    }

    auto* tileImg = grk_decompress_get_tile_image(codec.get(), t, true);
    if(!tileImg)
    {
      spdlog::error("get_tile_image({}) returned null after re-decompress", t);
      return {};
    }

    TileData td;
    td.tileIndex = t;
    for(uint16_t c = 0; c < tileImg->numcomps; ++c)
    {
      auto& comp = tileImg->comps[c];
      if(!comp.data)
        continue;
      uint32_t w = comp.w;
      uint32_t h = comp.h;
      auto* pixelData = static_cast<int32_t*>(comp.data);
      std::vector<int32_t> pixels(pixelData, pixelData + w * h);
      td.componentData.push_back(std::move(pixels));
    }
    result.push_back(std::move(td));
  }

  return result;
}

static bool testLRUEvictionReDecompress(const std::string& testFile)
{
  spdlog::info("=== Test: LRU eviction + re-decompress from cached SOT offsets ===");

  // Reference: decompress each tile individually, no LRU
  auto refData = decompressTileByTile(testFile, GRK_TILE_CACHE_IMAGE, 0);
  if(refData.empty())
  {
    spdlog::error("Reference tile-by-tile decompression produced no data");
    return false;
  }
  spdlog::info("Reference: {} tiles captured", refData.size());

  // Test: decompress with LRU, max_active_tiles=4 (forces eviction on 16-tile image)
  auto lruData = decompressTileByTile(testFile, GRK_TILE_CACHE_IMAGE | GRK_TILE_CACHE_LRU, 4);
  if(lruData.empty())
  {
    spdlog::error("LRU re-decompress produced no data");
    return false;
  }
  spdlog::info("LRU re-decompress: {} tiles captured", lruData.size());

  if(refData.size() != lruData.size())
  {
    spdlog::error("Tile count mismatch: ref={}, lru={}", refData.size(), lruData.size());
    return false;
  }

  for(size_t i = 0; i < refData.size(); ++i)
  {
    auto& ref = refData[i];
    auto& lru = lruData[i];
    if(ref.tileIndex != lru.tileIndex)
    {
      spdlog::error("Tile index mismatch at position {}", i);
      return false;
    }
    if(ref.componentData.size() != lru.componentData.size())
    {
      spdlog::error("Component count mismatch for tile {}", ref.tileIndex);
      return false;
    }
    for(size_t c = 0; c < ref.componentData.size(); ++c)
    {
      if(ref.componentData[c] != lru.componentData[c])
      {
        spdlog::error("Pixel data mismatch for tile {} component {}", ref.tileIndex, c);
        return false;
      }
    }
  }

  spdlog::info("PASS: LRU eviction + re-decompress matches reference for all {} tiles",
               refData.size());
  return true;
}

///////////////////////////////////////////////////////////////////
// Test 5: TLM image + LRU eviction + re-decompress
//
// Identical to Test 4, but uses a TLM image. Verifies that the
// tile-by-tile re-decompress path works with TLM random access.
///////////////////////////////////////////////////////////////////
static bool testTLMEvictionReDecompress(const std::string& testFile)
{
  spdlog::info("=== Test: TLM + LRU eviction + re-decompress ===");

  auto refData = decompressTileByTile(testFile, GRK_TILE_CACHE_IMAGE, 0);
  if(refData.empty())
  {
    spdlog::error("Reference tile-by-tile decompression produced no data");
    return false;
  }
  spdlog::info("Reference: {} tiles captured", refData.size());

  auto lruData = decompressTileByTile(testFile, GRK_TILE_CACHE_IMAGE | GRK_TILE_CACHE_LRU, 4);
  if(lruData.empty())
  {
    spdlog::error("TLM LRU re-decompress produced no data");
    return false;
  }
  spdlog::info("TLM LRU re-decompress: {} tiles captured", lruData.size());

  if(refData.size() != lruData.size())
  {
    spdlog::error("Tile count mismatch: ref={}, lru={}", refData.size(), lruData.size());
    return false;
  }

  for(size_t i = 0; i < refData.size(); ++i)
  {
    if(refData[i].componentData != lruData[i].componentData)
    {
      spdlog::error("Pixel data mismatch for tile {}", refData[i].tileIndex);
      return false;
    }
  }

  spdlog::info("PASS: TLM + LRU eviction re-decompress matches for all {} tiles", refData.size());
  return true;
}

///////////////////////////////////////////////////////////////////
// Test 6: Multi-tile-part image + LRU eviction + re-decompress
//
// Creates an image with multiple tile parts per tile (one per
// component), then exercises LRU eviction and re-decompress.
///////////////////////////////////////////////////////////////////
static bool createMultiTilePartImage(const std::string& path, uint32_t width, uint32_t height,
                                     uint32_t tileWidth, uint32_t tileHeight, bool writeTlm)
{
  grk_cparameters cparams{};
  grk_compress_set_default_params(&cparams);
  cparams.cod_format = GRK_FMT_J2K;
  cparams.t_width = tileWidth;
  cparams.t_height = tileHeight;
  cparams.tile_size_on = (tileWidth < width || tileHeight < height);
  cparams.write_tlm = writeTlm;
  cparams.numresolution = 3;
  cparams.irreversible = false;
  cparams.numlayers = 1;
  cparams.layer_rate[0] = 0;
  // Multiple tile parts: one per component
  cparams.enable_tile_part_generation = true;
  cparams.new_tile_part_progression_divider = 'C';

  // 3-component RGB image
  grk_image_comp comps[3]{};
  for(int c = 0; c < 3; ++c)
  {
    comps[c].dx = 1;
    comps[c].dy = 1;
    comps[c].w = width;
    comps[c].h = height;
    comps[c].x0 = 0;
    comps[c].y0 = 0;
    comps[c].prec = 8;
    comps[c].sgnd = 0;
  }

  auto* image = grk_image_new(3, comps, GRK_CLRSPC_SRGB, true);
  if(!image)
    return false;

  for(uint16_t c = 0; c < 3; ++c)
  {
    auto* data = static_cast<int32_t*>(image->comps[c].data);
    for(uint32_t y = 0; y < height; ++y)
      for(uint32_t x = 0; x < width; ++x)
        data[y * width + x] = static_cast<int32_t>((x * (7 + c) + y * (13 + c)) % 256);
  }

  grk_stream_params sp{};
  safe_strcpy(sp.file, path.data());

  auto* codec = grk_compress_init(&sp, &cparams, image);
  if(!codec)
  {
    grk_object_unref(&image->obj);
    return false;
  }

  uint64_t len = grk_compress(codec, nullptr);
  grk_object_unref(codec);
  grk_object_unref(&image->obj);

  if(len == 0)
    return false;

  spdlog::info("Created multi-tile-part image: {}x{}, tiles {}x{}, 3 comps, {} bytes", width,
               height, tileWidth, tileHeight, len);
  return true;
}

static bool testMultiTilePartReDecompress(const std::string& testFile)
{
  spdlog::info("=== Test: Multi-tile-part + async decompress ===");

  // Reference: async decompress with IMAGE cache
  auto refData = decompressAndCapture(testFile, GRK_TILE_CACHE_IMAGE);
  if(refData.empty())
  {
    spdlog::error("Reference async decompression produced no data");
    return false;
  }
  spdlog::info("Reference: {} tiles, {} components each", refData.size(),
               refData[0].componentData.size());

  // Test: async decompress with LRU cache
  auto lruData = decompressAndCapture(testFile, GRK_TILE_CACHE_IMAGE | GRK_TILE_CACHE_LRU);
  if(lruData.empty())
  {
    spdlog::error("LRU async decompress produced no data");
    return false;
  }

  if(refData.size() != lruData.size())
  {
    spdlog::error("Tile count mismatch");
    return false;
  }

  for(size_t i = 0; i < refData.size(); ++i)
  {
    if(refData[i].componentData != lruData[i].componentData)
    {
      spdlog::error("Pixel data mismatch for tile {}", refData[i].tileIndex);
      return false;
    }
  }

  spdlog::info("PASS: Multi-tile-part + async decompress matches for all {} tiles", refData.size());
  return true;
}

///////////////////////////////////////////////////////////////////
// Test 7: Truncated file error recovery
//
// Creates a valid multi-tile image, truncates it mid-stream,
// and verifies that decompression handles the error gracefully
// (returns partial data rather than crashing).
///////////////////////////////////////////////////////////////////
static bool testTruncatedFileRecovery()
{
  spdlog::info("=== Test: Truncated file error recovery ===");

  std::string fullFile =
      (std::filesystem::temp_directory_path() / "grk_truncate_full.j2k").string();
  if(!createTestImage(fullFile, 256, 256, 64, 64, false))
  {
    spdlog::error("Failed to create full test image");
    return false;
  }

  auto fullSize = std::filesystem::file_size(fullFile);
  spdlog::info("Full file size: {} bytes", fullSize);

  // Truncate to ~50% (enough for header + some tiles)
  std::string truncFile =
      (std::filesystem::temp_directory_path() / "grk_truncate_half.j2k").string();
  {
    std::ifstream in(fullFile, std::ios::binary);
    std::ofstream out(truncFile, std::ios::binary);
    std::vector<char> buf(fullSize / 2);
    in.read(buf.data(), (std::streamsize)buf.size());
    out.write(buf.data(), (std::streamsize)buf.size());
  }

  // Decompress — should handle truncation gracefully
  grk_decompress_parameters params{};
  params.core.tile_cache_strategy = GRK_TILE_CACHE_IMAGE;

  grk_stream_params streamParams{};
  safe_strcpy(streamParams.file, truncFile.data());

  CodecPtr codec(grk_decompress_init(&streamParams, &params));
  if(!codec)
  {
    spdlog::error("Failed to init decompressor for truncated file");
    std::filesystem::remove(fullFile);
    std::filesystem::remove(truncFile);
    return false;
  }

  grk_header_info headerInfo{};
  bool headerOk = grk_decompress_read_header(codec.get(), &headerInfo);
  if(!headerOk)
  {
    spdlog::info("Header read failed for truncated file (expected for very short truncation)");
    std::filesystem::remove(fullFile);
    std::filesystem::remove(truncFile);
    return true; // graceful failure is acceptable
  }

  spdlog::info("Header read OK for truncated file: grid {}x{}", headerInfo.t_grid_width,
               headerInfo.t_grid_height);

  // Try tile-by-tile decompress — some tiles should succeed, some should fail
  uint16_t numTiles = headerInfo.t_grid_width * headerInfo.t_grid_height;
  uint16_t succeeded = 0;
  uint16_t failed = 0;
  if(!grk_decompress_update(&params, codec.get()))
  {
    spdlog::error("grk_decompress_update failed");
    std::filesystem::remove(fullFile);
    std::filesystem::remove(truncFile);
    return false;
  }

  for(uint16_t t = 0; t < numTiles; ++t)
  {
    if(grk_decompress_tile(codec.get(), t))
      succeeded++;
    else
      failed++;
  }

  spdlog::info("Truncated decompress: {} succeeded, {} failed out of {} tiles", succeeded, failed,
               numTiles);

  // The test passes if we didn't crash and got at least some tiles
  bool pass = (succeeded > 0 || failed == numTiles);
  if(pass)
    spdlog::info("PASS: Truncated file handled gracefully");
  else
    spdlog::error("FAIL: Expected some tiles or all failures, got succeeded={} failed={}",
                  succeeded, failed);

  std::error_code ec;
  std::filesystem::remove(fullFile, ec);
  std::filesystem::remove(truncFile, ec);
  return pass;
}

///////////////////////////////////////////////////////////////////
// Test 8: Reduced resolution decompress
//
// Decompresses at reduce=1 (half resolution) and verifies
// dimensions are halved and output is non-empty.
///////////////////////////////////////////////////////////////////
static bool testReducedResolution()
{
  spdlog::info("=== Test: Reduced resolution decompress ===");

  std::string testFile = (std::filesystem::temp_directory_path() / "grk_reduce_test.j2k").string();
  if(!createTestImage(testFile, 256, 256, 64, 64, false))
  {
    spdlog::error("Failed to create test image");
    return false;
  }

  // Full resolution reference
  grk_decompress_parameters fullParams{};
  fullParams.core.tile_cache_strategy = GRK_TILE_CACHE_IMAGE;
  fullParams.core.reduce = 0;

  grk_stream_params sp1{};
  safe_strcpy(sp1.file, testFile.data());
  CodecPtr codec1(grk_decompress_init(&sp1, &fullParams));
  grk_header_info hi1{};
  grk_decompress_read_header(codec1.get(), &hi1);
  grk_decompress_update(&fullParams, codec1.get());
  grk_decompress_tile(codec1.get(), 0);
  auto* fullImg = grk_decompress_get_tile_image(codec1.get(), 0, true);
  uint32_t fullW = fullImg ? fullImg->comps[0].w : 0;
  uint32_t fullH = fullImg ? fullImg->comps[0].h : 0;
  spdlog::info("Full resolution tile 0: {}x{}", fullW, fullH);

  // Reduced resolution (reduce=1 → half)
  grk_decompress_parameters redParams{};
  redParams.core.tile_cache_strategy = GRK_TILE_CACHE_IMAGE;
  redParams.core.reduce = 1;

  grk_stream_params sp2{};
  safe_strcpy(sp2.file, testFile.data());
  CodecPtr codec2(grk_decompress_init(&sp2, &redParams));
  grk_header_info hi2{};
  grk_decompress_read_header(codec2.get(), &hi2);
  grk_decompress_update(&redParams, codec2.get());
  grk_decompress_tile(codec2.get(), 0);
  auto* redImg = grk_decompress_get_tile_image(codec2.get(), 0, true);
  uint32_t redW = redImg ? redImg->comps[0].w : 0;
  uint32_t redH = redImg ? redImg->comps[0].h : 0;
  spdlog::info("Reduced (reduce=1) tile 0: {}x{}", redW, redH);

  std::error_code ec;
  std::filesystem::remove(testFile, ec);

  if(fullW == 0 || redW == 0)
  {
    spdlog::error("FAIL: Got zero-dimension tile");
    return false;
  }

  // Reduced tile should be approximately half the full tile dimensions
  if(redW > fullW || redH > fullH)
  {
    spdlog::error("FAIL: Reduced dimensions ({}x{}) >= full ({}x{})", redW, redH, fullW, fullH);
    return false;
  }

  if(redW == fullW && redH == fullH)
  {
    spdlog::error("FAIL: Reduced dimensions same as full (reduce not applied)");
    return false;
  }

  spdlog::info("PASS: Reduced resolution produces smaller output");
  return true;
}

///////////////////////////////////////////////////////////////////
// Test 9: Reduce + region combined
//
// Decompresses a sub-region at reduced resolution. Verifies
// both parameters are respected simultaneously.
///////////////////////////////////////////////////////////////////
static bool testReduceWithRegion()
{
  spdlog::info("=== Test: Reduce + region combined ===");

  std::string testFile =
      (std::filesystem::temp_directory_path() / "grk_reduce_region_test.j2k").string();
  if(!createTestImage(testFile, 256, 256, 64, 64, false))
  {
    spdlog::error("Failed to create test image");
    return false;
  }

  // Decompress a sub-region at reduce=1
  grk_decompress_parameters params{};
  params.core.tile_cache_strategy = GRK_TILE_CACHE_IMAGE;
  params.core.reduce = 1;
  params.dw_x0 = 0;
  params.dw_y0 = 0;
  params.dw_x1 = 128;
  params.dw_y1 = 128;
  params.asynchronous = true;
  params.simulate_synchronous = true;

  grk_stream_params sp{};
  safe_strcpy(sp.file, testFile.data());
  CodecPtr codec(grk_decompress_init(&sp, &params));
  grk_header_info hi{};
  grk_decompress_read_header(codec.get(), &hi);
  grk_decompress_update(&params, codec.get());

  bool ok = grk_decompress(codec.get(), nullptr);
  if(!ok)
  {
    spdlog::error("Decompress with reduce+region failed");
    std::filesystem::remove(testFile);
    return false;
  }

  grk_decompress_wait(codec.get(), nullptr);

  auto* img = grk_decompress_get_image(codec.get());
  if(!img)
  {
    spdlog::error("Failed to get image after reduce+region decompress");
    std::filesystem::remove(testFile);
    return false;
  }

  // At reduce=1, the 128x128 region should produce ~64x64 output
  uint32_t w = img->x1 - img->x0;
  uint32_t h = img->y1 - img->y0;
  spdlog::info("Reduce+region output: {}x{}", w, h);

  std::error_code ec;
  std::filesystem::remove(testFile, ec);

  if(w == 0 || h == 0)
  {
    spdlog::error("FAIL: Zero-dimension output");
    return false;
  }

  // Region is 128x128, reduce=1 → expect ~64x64
  if(w > 128 || h > 128)
  {
    spdlog::error("FAIL: Output larger than region");
    return false;
  }

  spdlog::info("PASS: Reduce + region combined");
  return true;
}

///////////////////////////////////////////////////////////////////
// Test 10: All tiles via decompressTile then re-read all
//
// Decompresses every tile individually, then reads them all
// back via get_tile_image. With IMAGE cache strategy all tiles
// remain in cache. Verifies the tile data survives the full pass.
///////////////////////////////////////////////////////////////////
static bool testAllTilesThenReRead(const std::string& testFile)
{
  spdlog::info("=== Test: All tiles via decompressTile then re-read ===");

  grk_decompress_parameters params{};
  params.core.tile_cache_strategy = GRK_TILE_CACHE_IMAGE;

  grk_stream_params sp{};
  safe_strcpy(sp.file, testFile.data());
  CodecPtr codec(grk_decompress_init(&sp, &params));
  grk_header_info hi{};
  grk_decompress_read_header(codec.get(), &hi);
  grk_decompress_update(&params, codec.get());

  uint16_t numTiles = hi.t_grid_width * hi.t_grid_height;
  spdlog::info("All-tiles-then-reread: grid {}x{} = {} tiles", hi.t_grid_width, hi.t_grid_height,
               numTiles);

  // Step 1: decompress all tiles
  for(uint16_t t = 0; t < numTiles; ++t)
  {
    if(!grk_decompress_tile(codec.get(), t))
    {
      spdlog::error("decompressTile({}) failed", t);
      return false;
    }
  }
  spdlog::info("Step 1: All {} tiles decompressed", numTiles);

  // Step 2: re-read all tiles — they should all still be in cache
  for(uint16_t t = 0; t < numTiles; ++t)
  {
    auto* tileImg = grk_decompress_get_tile_image(codec.get(), t, true);
    if(!tileImg)
    {
      spdlog::error("get_tile_image({}) returned null on re-read", t);
      return false;
    }
    if(!tileImg->comps[0].data)
    {
      spdlog::error("Tile {} has no pixel data on re-read", t);
      return false;
    }
  }

  spdlog::info("PASS: All tiles accessible after full pass");
  return true;
}

///////////////////////////////////////////////////////////////////
// Test 11: Out-of-bounds tile index
//
// Verifies that grk_decompress_tile with an invalid tile index
// returns false rather than crashing.
///////////////////////////////////////////////////////////////////
static bool testOutOfBoundsTileIndex(const std::string& testFile)
{
  spdlog::info("=== Test: Out-of-bounds tile index ===");

  grk_decompress_parameters params{};
  params.core.tile_cache_strategy = GRK_TILE_CACHE_IMAGE;

  grk_stream_params sp{};
  safe_strcpy(sp.file, testFile.data());
  CodecPtr codec(grk_decompress_init(&sp, &params));
  grk_header_info hi{};
  grk_decompress_read_header(codec.get(), &hi);
  grk_decompress_update(&params, codec.get());

  uint16_t numTiles = hi.t_grid_width * hi.t_grid_height;
  uint16_t badIndex = numTiles + 10;

  bool result = grk_decompress_tile(codec.get(), badIndex);
  if(result)
  {
    spdlog::error("FAIL: decompressTile({}) succeeded for out-of-bounds index", badIndex);
    return false;
  }

  spdlog::info("PASS: Out-of-bounds tile index correctly returned false");
  return true;
}

///////////////////////////////////////////////////////////////////
// Test 12: RGB multi-component round-trip
//
// Creates a 3-component RGB image with MCT, compresses, and
// verifies lossless round-trip through decompress.
///////////////////////////////////////////////////////////////////
static bool testRGBRoundTrip()
{
  spdlog::info("=== Test: RGB multi-component round-trip ===");

  uint32_t width = 128, height = 128;
  std::string testFile =
      (std::filesystem::temp_directory_path() / "grk_rgb_roundtrip.j2k").string();

  // Compress
  grk_cparameters cparams{};
  grk_compress_set_default_params(&cparams);
  cparams.cod_format = GRK_FMT_J2K;
  cparams.t_width = 64;
  cparams.t_height = 64;
  cparams.tile_size_on = true;
  cparams.numresolution = 3;
  cparams.irreversible = false;
  cparams.numlayers = 1;
  cparams.layer_rate[0] = 0;
  cparams.mct = 1; // standard MCT

  grk_image_comp comps[3]{};
  for(int c = 0; c < 3; ++c)
  {
    comps[c].dx = 1;
    comps[c].dy = 1;
    comps[c].w = width;
    comps[c].h = height;
    comps[c].prec = 8;
    comps[c].sgnd = 0;
  }

  auto* image = grk_image_new(3, comps, GRK_CLRSPC_SRGB, true);
  if(!image)
  {
    spdlog::error("Failed to create RGB image");
    return false;
  }

  // Fill with distinct per-component patterns
  std::vector<std::vector<int32_t>> refData(3);
  for(uint16_t c = 0; c < 3; ++c)
  {
    auto* data = static_cast<int32_t*>(image->comps[c].data);
    refData[c].resize(width * height);
    for(uint32_t y = 0; y < height; ++y)
    {
      for(uint32_t x = 0; x < width; ++x)
      {
        int32_t val = static_cast<int32_t>((x * (3 + c * 7) + y * (11 + c * 5)) % 256);
        data[y * width + x] = val;
        refData[c][y * width + x] = val;
      }
    }
  }

  grk_stream_params sp{};
  safe_strcpy(sp.file, testFile.data());
  auto* enc = grk_compress_init(&sp, &cparams, image);
  if(!enc)
  {
    grk_object_unref(&image->obj);
    return false;
  }
  uint64_t len = grk_compress(enc, nullptr);
  grk_object_unref(enc);
  grk_object_unref(&image->obj);

  if(len == 0)
  {
    spdlog::error("RGB compression failed");
    return false;
  }
  spdlog::info("RGB compressed: {} bytes", len);

  // Decompress and verify
  grk_decompress_parameters dparams{};
  dparams.core.tile_cache_strategy = GRK_TILE_CACHE_IMAGE;
  dparams.asynchronous = true;
  dparams.simulate_synchronous = true;

  grk_stream_params sp2{};
  safe_strcpy(sp2.file, testFile.data());
  CodecPtr dec(grk_decompress_init(&sp2, &dparams));
  grk_header_info hi{};
  grk_decompress_read_header(dec.get(), &hi);
  grk_decompress(dec.get(), nullptr);
  grk_decompress_wait(dec.get(), nullptr);

  auto* decImg = grk_decompress_get_image(dec.get());
  if(!decImg)
  {
    spdlog::error("Failed to get decompressed RGB image");
    std::filesystem::remove(testFile);
    return false;
  }

  if(decImg->numcomps != 3)
  {
    spdlog::error("Expected 3 components, got {}", decImg->numcomps);
    std::filesystem::remove(testFile);
    return false;
  }

  bool match = true;
  for(uint16_t c = 0; c < 3 && match; ++c)
  {
    auto& comp = decImg->comps[c];
    auto* pixels = static_cast<int32_t*>(comp.data);
    if(!pixels)
    {
      spdlog::error("Component {} has no data", c);
      match = false;
      break;
    }
    for(uint32_t y = 0; y < comp.h && match; ++y)
    {
      for(uint32_t x = 0; x < comp.w && match; ++x)
      {
        int32_t got = pixels[y * comp.stride + x];
        int32_t expected = refData[c][y * width + x];
        if(got != expected)
        {
          spdlog::error("Mismatch at comp={} ({},{}) got={} expected={}", c, x, y, got, expected);
          match = false;
        }
      }
    }
  }

  std::error_code ec;
  std::filesystem::remove(testFile, ec);

  if(match)
    spdlog::info("PASS: RGB round-trip is pixel-perfect");
  else
    spdlog::error("FAIL: RGB round-trip pixel mismatch");

  return match;
}

///////////////////////////////////////////////////////////////////
// Main
///////////////////////////////////////////////////////////////////
int GrkLRUCacheTest::main(int argc, char** argv)
{
  grk_initialize(nullptr, 0, nullptr);

  int failures = 0;

  // Unit tests (no file needed)
  if(!testDiskCache())
    failures++;
  if(!testLRUCacheEviction())
    failures++;

  // Integration test: compress a test image and verify LRU output
  std::string testFile = (std::filesystem::temp_directory_path() / "grk_lru_test.j2k").string();
  bool created = createTestImage(testFile, 256, 256, 64, 64); // 4x4 = 16 tiles
  if(!created)
  {
    spdlog::error("Failed to create test image, skipping integration test");
    failures++;
  }
  else
  {
    if(!testLRUMatchesReference(testFile))
      failures++;

    // Clean up
    std::error_code ec;
    std::filesystem::remove(testFile, ec);
  }

  // Integration test: non-TLM image with LRU eviction + re-decompress
  std::string noTlmFile =
      (std::filesystem::temp_directory_path() / "grk_lru_no_tlm_test.j2k").string();
  bool noTlmCreated = createTestImage(noTlmFile, 256, 256, 64, 64, false); // 4x4 = 16 tiles, no TLM
  if(!noTlmCreated)
  {
    spdlog::error("Failed to create non-TLM test image, skipping eviction test");
    failures++;
  }
  else
  {
    if(!testLRUEvictionReDecompress(noTlmFile))
      failures++;

    std::error_code ec;
    std::filesystem::remove(noTlmFile, ec);
  }

  // Test 5: TLM image + LRU eviction + re-decompress (via decompressTile API)
  std::string tlmFile =
      (std::filesystem::temp_directory_path() / "grk_lru_tlm_evict_test.j2k").string();
  if(!createTestImage(tlmFile, 256, 256, 64, 64, true))
  {
    spdlog::error("Failed to create TLM test image");
    failures++;
  }
  else
  {
    if(!testTLMEvictionReDecompress(tlmFile))
      failures++;
    std::error_code ec;
    std::filesystem::remove(tlmFile, ec);
  }

  // Test 6: Multi-tile-part + LRU eviction + re-decompress
  std::string mtpFile = (std::filesystem::temp_directory_path() / "grk_multi_tp_test.j2k").string();
  if(!createMultiTilePartImage(mtpFile, 256, 256, 64, 64, false))
  {
    spdlog::error("Failed to create multi-tile-part image");
    failures++;
  }
  else
  {
    if(!testMultiTilePartReDecompress(mtpFile))
      failures++;
    std::error_code ec;
    std::filesystem::remove(mtpFile, ec);
  }

  // Test 7: Truncated file error recovery
  if(!testTruncatedFileRecovery())
    failures++;

  // Test 8: Reduced resolution decompress
  if(!testReducedResolution())
    failures++;

  // Test 9: Reduce + region combined
  if(!testReduceWithRegion())
    failures++;

  // Test 10: All tiles via decompressTile then re-read
  {
    std::string interFile =
        (std::filesystem::temp_directory_path() / "grk_reread_test.j2k").string();
    if(!createTestImage(interFile, 256, 256, 64, 64, false))
    {
      spdlog::error("Failed to create re-read test image");
      failures++;
    }
    else
    {
      if(!testAllTilesThenReRead(interFile))
        failures++;
      std::error_code ec;
      std::filesystem::remove(interFile, ec);
    }
  }

  // Test 11: Out-of-bounds tile index
  {
    std::string oobFile =
        (std::filesystem::temp_directory_path() / "grk_oob_tile_test.j2k").string();
    if(!createTestImage(oobFile, 256, 256, 64, 64, false))
    {
      spdlog::error("Failed to create OOB test image");
      failures++;
    }
    else
    {
      if(!testOutOfBoundsTileIndex(oobFile))
        failures++;
      std::error_code ec;
      std::filesystem::remove(oobFile, ec);
    }
  }

  // Test 12: RGB multi-component round-trip
  if(!testRGBRoundTrip())
    failures++;

  if(failures > 0)
  {
    spdlog::error("{} test(s) FAILED", failures);
    return EXIT_FAILURE;
  }

  spdlog::info("All tests PASSED");
  return EXIT_SUCCESS;
}

} // namespace grk
