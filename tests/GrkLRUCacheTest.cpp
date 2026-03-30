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

// Create a synthetic test image and compress it to a J2K file with TLM markers
static bool createTestImage(const std::string& path, uint32_t width, uint32_t height,
                            uint32_t tileWidth, uint32_t tileHeight)
{
  grk_cparameters cparams{};
  grk_compress_set_default_params(&cparams);
  cparams.cod_format = GRK_FMT_J2K;
  cparams.t_width = tileWidth;
  cparams.t_height = tileHeight;
  cparams.write_tlm = true;
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

  if(failures > 0)
  {
    spdlog::error("{} test(s) FAILED", failures);
    return EXIT_FAILURE;
  }

  spdlog::info("All tests PASSED");
  return EXIT_SUCCESS;
}

} // namespace grk
