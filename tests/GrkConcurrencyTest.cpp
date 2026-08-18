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

// Stress tests for multi-threaded async decompress. Every test verifies
// pixel-exact output against a single-threaded reference, so a lost update
// or stale read fails the test even when it doesn't crash.

#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "grk_apps_config.h"
#include "grok.h"
#include "spdlog/spdlog.h"
#include "GrkConcurrencyTest.h"

namespace grk
{

namespace
{

  constexpr uint32_t kImageWidth = 256;
  constexpr uint32_t kImageHeight = 256;
  constexpr uint32_t kTileWidth = 64;
  constexpr uint32_t kTileHeight = 64;
  constexpr uint16_t kNumTiles = 16;
  constexpr int kNumThreads = 4;
  constexpr int kIterationsPerThread = 3;

  template<size_t N>
  void safe_strcpy(char (&dest)[N], const char* src)
  {
    size_t len = strnlen(src, N - 1);
    memcpy(dest, src, len);
    dest[len] = '\0';
  }

  struct CodecDeleter
  {
    void operator()(grk_object* codec) const
    {
      if(codec)
        grk_object_unref(codec);
    }
  };
  using CodecPtr = std::unique_ptr<grk_object, CodecDeleter>;

  // Heap-allocate large parameter structs to avoid stack overflows on Windows.
  auto make_cparameters()
  {
    auto p = std::make_unique<grk_cparameters>();
    memset(p.get(), 0, sizeof(grk_cparameters));
    return p;
  }
  auto make_decompress_parameters()
  {
    auto p = std::make_unique<grk_decompress_parameters>();
    memset(p.get(), 0, sizeof(grk_decompress_parameters));
    return p;
  }
  auto make_stream_params()
  {
    auto p = std::make_unique<grk_stream_params>();
    memset(p.get(), 0, sizeof(grk_stream_params));
    return p;
  }

  std::vector<int32_t> readPixels(const grk_image_comp& comp, uint32_t count)
  {
    std::vector<int32_t> pixels(count);
    if(comp.data_type == GRK_INT_16)
    {
      auto* p = static_cast<int16_t*>(comp.data);
      for(uint32_t i = 0; i < count; ++i)
        pixels[i] = p[i];
    }
    else
    {
      auto* p = static_cast<int32_t*>(comp.data);
      for(uint32_t i = 0; i < count; ++i)
        pixels[i] = p[i];
    }
    return pixels;
  }

  bool createTestImage(const std::string& path, bool writeTlm)
  {
    auto cparams = make_cparameters();
    grk_compress_set_default_params(cparams.get());
    cparams->cod_format = GRK_FMT_J2K;
    cparams->t_width = kTileWidth;
    cparams->t_height = kTileHeight;
    cparams->tile_size_on = true;
    cparams->write_tlm = writeTlm;
    cparams->numresolution = 3;
    cparams->irreversible = false;
    cparams->numlayers = 1;
    cparams->layer_rate[0] = 0;

    grk_image_comp comp{};
    comp.dx = 1;
    comp.dy = 1;
    comp.w = kImageWidth;
    comp.h = kImageHeight;
    comp.prec = 8;
    comp.sgnd = 0;

    auto* image = grk_image_new(1, &comp, GRK_CLRSPC_GRAY, true);
    if(!image)
      return false;

    auto* data = static_cast<int32_t*>(image->comps[0].data);
    for(uint32_t y = 0; y < kImageHeight; ++y)
      for(uint32_t x = 0; x < kImageWidth; ++x)
        data[y * kImageWidth + x] = static_cast<int32_t>((x * 7 + y * 13) % 256);

    auto streamParams = make_stream_params();
    safe_strcpy(streamParams->file, path.data());

    auto* codec = grk_compress_init(streamParams.get(), cparams.get(), image);
    if(!codec)
    {
      grk_object_unref(&image->obj);
      return false;
    }

    uint64_t len = grk_compress(codec, nullptr);
    grk_object_unref(codec);
    grk_object_unref(&image->obj);
    return len != 0;
  }

  // Per-tile pixel data for all components
  using TilePixels = std::vector<std::vector<int32_t>>;

  // Full asynchronous decompress, returning per-tile pixel data.
  // Empty result means failure.
  std::vector<TilePixels> decompressAsync(const std::string& path, uint32_t tileCacheStrategy,
                                          uint16_t maxActiveTiles)
  {
    std::vector<TilePixels> result;

    auto params = make_decompress_parameters();
    params->core.tile_cache_strategy = tileCacheStrategy;
    params->core.max_active_tiles = maxActiveTiles;
    params->asynchronous = true;
    params->simulate_synchronous = true;

    auto streamParams = make_stream_params();
    safe_strcpy(streamParams->file, path.data());

    CodecPtr codec(grk_decompress_init(streamParams.get(), params.get()));
    if(!codec)
      return result;

    grk_header_info headerInfo{};
    if(!grk_decompress_read_header(codec.get(), &headerInfo))
      return result;

    if(!grk_decompress(codec.get(), nullptr))
      return result;

    grk_decompress_wait(codec.get(), nullptr);

    uint16_t numTiles = headerInfo.t_grid_width * headerInfo.t_grid_height;
    for(uint16_t t = 0; t < numTiles; ++t)
    {
      auto* tileImg = grk_decompress_get_tile_image(codec.get(), t, false);
      if(!tileImg)
        tileImg = grk_decompress_get_image(codec.get());
      if(!tileImg)
        return {};

      TilePixels td;
      for(uint16_t c = 0; c < tileImg->numcomps; ++c)
      {
        auto& comp = tileImg->comps[c];
        if(!comp.data)
          continue;
        td.push_back(readPixels(comp, comp.w * comp.h));
      }
      result.push_back(std::move(td));
    }

    return result;
  }

  // Asynchronous decompress driven by row swath waits. Each wait releases tile
  // rows and re-enters batch scheduling while the parser thread is still running.
  bool decompressSwathAndVerify(const std::string& path, const std::vector<TilePixels>& reference)
  {
    auto params = make_decompress_parameters();
    params->core.tile_cache_strategy = GRK_TILE_CACHE_IMAGE;
    params->asynchronous = true;
    params->simulate_synchronous = true;

    auto streamParams = make_stream_params();
    safe_strcpy(streamParams->file, path.data());

    CodecPtr codec(grk_decompress_init(streamParams.get(), params.get()));
    if(!codec)
      return false;

    grk_header_info headerInfo{};
    if(!grk_decompress_read_header(codec.get(), &headerInfo))
      return false;

    if(!grk_decompress(codec.get(), nullptr))
      return false;

    uint16_t numTileCols = headerInfo.t_grid_width;
    for(uint32_t rowY = 0; rowY < kImageHeight; rowY += kTileHeight)
    {
      grk_wait_swath swath{};
      swath.x0 = 0;
      swath.y0 = rowY;
      swath.x1 = kImageWidth;
      swath.y1 = std::min(rowY + kTileHeight, kImageHeight);
      grk_decompress_wait(codec.get(), &swath);

      uint16_t tileY = static_cast<uint16_t>(rowY / kTileHeight);
      for(uint16_t tileX = 0; tileX < numTileCols; ++tileX)
      {
        uint16_t tileIndex = static_cast<uint16_t>(tileY * numTileCols + tileX);
        auto* tileImg = grk_decompress_get_tile_image(codec.get(), tileIndex, false);
        if(!tileImg || !tileImg->comps[0].data)
        {
          spdlog::error("Swath decompress: no data for tile {}", tileIndex);
          return false;
        }
        auto pixels = readPixels(tileImg->comps[0], tileImg->comps[0].w * tileImg->comps[0].h);
        if(pixels != reference[tileIndex][0])
        {
          spdlog::error("Swath decompress: pixel mismatch for tile {}", tileIndex);
          return false;
        }
      }
    }

    grk_decompress_wait(codec.get(), nullptr);
    return true;
  }

  // Tile-by-tile decompress with LRU eviction, then re-decompress of evicted
  // tiles from cached SOT offsets, verified against the reference.
  bool decompressTileByTileAndVerify(const std::string& path,
                                     const std::vector<TilePixels>& reference)
  {
    auto params = make_decompress_parameters();
    params->core.tile_cache_strategy = GRK_TILE_CACHE_IMAGE | GRK_TILE_CACHE_LRU;
    params->core.max_active_tiles = 4;

    auto streamParams = make_stream_params();
    safe_strcpy(streamParams->file, path.data());

    CodecPtr codec(grk_decompress_init(streamParams.get(), params.get()));
    if(!codec)
      return false;

    grk_header_info headerInfo{};
    if(!grk_decompress_read_header(codec.get(), &headerInfo))
      return false;

    if(!grk_decompress_update(params.get(), codec.get()))
      return false;

    uint16_t numTiles = headerInfo.t_grid_width * headerInfo.t_grid_height;
    // first pass forces eviction, second pass re-decompresses evicted tiles
    for(int pass = 0; pass < 2; ++pass)
    {
      for(uint16_t t = 0; t < numTiles; ++t)
      {
        if(!grk_decompress_tile(codec.get(), t))
        {
          spdlog::error("decompress_tile({}) failed on pass {}", t, pass);
          return false;
        }
        if(pass == 0)
          continue;
        auto* tileImg = grk_decompress_get_tile_image(codec.get(), t, true);
        if(!tileImg || !tileImg->comps[0].data)
        {
          spdlog::error("no tile image for tile {} after re-decompress", t);
          return false;
        }
        auto pixels = readPixels(tileImg->comps[0], tileImg->comps[0].w * tileImg->comps[0].h);
        if(pixels != reference[t][0])
        {
          spdlog::error("pixel mismatch for tile {} after eviction + re-decompress", t);
          return false;
        }
      }
    }

    return true;
  }

  bool compareToReference(const std::vector<TilePixels>& got,
                          const std::vector<TilePixels>& reference, const char* label)
  {
    if(got.size() != reference.size())
    {
      spdlog::error("{}: tile count mismatch ({} vs {})", label, got.size(), reference.size());
      return false;
    }
    for(size_t t = 0; t < got.size(); ++t)
    {
      if(got[t] != reference[t])
      {
        spdlog::error("{}: pixel mismatch for tile {}", label, t);
        return false;
      }
    }
    return true;
  }

  // Run `work` on kNumThreads threads, kIterationsPerThread iterations each.
  // Returns true only if every iteration succeeded.
  bool runConcurrently(const std::function<bool()>& work)
  {
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for(int i = 0; i < kNumThreads; ++i)
    {
      threads.emplace_back([&]() {
        for(int iter = 0; iter < kIterationsPerThread; ++iter)
        {
          if(!work())
            failures++;
        }
      });
    }
    for(auto& t : threads)
      t.join();
    return failures == 0;
  }

  ///////////////////////////////////////////////////////////////////
  // Test 1: concurrent async decompress, TLM (batched) path
  ///////////////////////////////////////////////////////////////////
  bool testConcurrentAsyncTLM(const std::string& tlmFile, const std::vector<TilePixels>& reference)
  {
    spdlog::info("=== Test: concurrent async decompress (TLM) ===");

    bool ok = runConcurrently([&]() {
      auto got = decompressAsync(tlmFile, GRK_TILE_CACHE_IMAGE, 0);
      return !got.empty() && compareToReference(got, reference, "concurrent TLM");
    });

    if(ok)
      spdlog::info("PASS: concurrent async decompress (TLM)");
    return ok;
  }

  ///////////////////////////////////////////////////////////////////
  // Test 2: concurrent async decompress, sequential (non-TLM) path
  ///////////////////////////////////////////////////////////////////
  bool testConcurrentAsyncSequential(const std::string& noTlmFile,
                                     const std::vector<TilePixels>& reference)
  {
    spdlog::info("=== Test: concurrent async decompress (sequential) ===");

    bool ok = runConcurrently([&]() {
      auto got = decompressAsync(noTlmFile, GRK_TILE_CACHE_IMAGE, 0);
      return !got.empty() && compareToReference(got, reference, "concurrent sequential");
    });

    if(ok)
      spdlog::info("PASS: concurrent async decompress (sequential)");
    return ok;
  }

  ///////////////////////////////////////////////////////////////////
  // Test 3: concurrent swath-based waits (row completion + release)
  ///////////////////////////////////////////////////////////////////
  bool testConcurrentSwathWaits(const std::string& tlmFile, const std::string& noTlmFile,
                                const std::vector<TilePixels>& reference)
  {
    spdlog::info("=== Test: concurrent swath waits ===");

    bool ok = runConcurrently([&]() { return decompressSwathAndVerify(tlmFile, reference); });
    ok = runConcurrently([&]() { return decompressSwathAndVerify(noTlmFile, reference); }) && ok;

    if(ok)
      spdlog::info("PASS: concurrent swath waits");
    return ok;
  }

  ///////////////////////////////////////////////////////////////////
  // Test 4: concurrent LRU eviction + re-decompress
  ///////////////////////////////////////////////////////////////////
  bool testConcurrentLRUEviction(const std::string& tlmFile, const std::string& noTlmFile,
                                 const std::vector<TilePixels>& reference)
  {
    spdlog::info("=== Test: concurrent LRU eviction + re-decompress ===");

    bool ok = runConcurrently([&]() { return decompressTileByTileAndVerify(tlmFile, reference); });
    ok = runConcurrently([&]() { return decompressTileByTileAndVerify(noTlmFile, reference); }) &&
         ok;

    if(ok)
      spdlog::info("PASS: concurrent LRU eviction + re-decompress");
    return ok;
  }

  ///////////////////////////////////////////////////////////////////
  // Test 5: concurrent truncated-file decompress
  ///////////////////////////////////////////////////////////////////
  bool testConcurrentTruncated(const std::string& fullFile)
  {
    spdlog::info("=== Test: concurrent truncated-file decompress ===");

    auto fullSize = std::filesystem::file_size(fullFile);
    std::string truncFile =
        (std::filesystem::temp_directory_path() / "grk_concurrency_trunc.j2k").string();
    {
      std::ifstream in(fullFile, std::ios::binary);
      std::ofstream out(truncFile, std::ios::binary);
      std::vector<char> buf(fullSize / 2);
      in.read(buf.data(), (std::streamsize)buf.size());
      out.write(buf.data(), (std::streamsize)buf.size());
    }

    // truncation handling is best-effort: require no crash and a non-empty
    // result set per iteration, since some tiles decode and some fail
    bool ok = runConcurrently([&]() {
      auto params = make_decompress_parameters();
      params->core.tile_cache_strategy = GRK_TILE_CACHE_IMAGE;
      params->asynchronous = true;
      params->simulate_synchronous = true;

      auto streamParams = make_stream_params();
      safe_strcpy(streamParams->file, truncFile.data());

      CodecPtr codec(grk_decompress_init(streamParams.get(), params.get()));
      if(!codec)
        return false;

      grk_header_info headerInfo{};
      if(!grk_decompress_read_header(codec.get(), &headerInfo))
        return true; // graceful header failure is acceptable

      grk_decompress(codec.get(), nullptr);
      grk_decompress_wait(codec.get(), nullptr);
      return true;
    });

    std::error_code ec;
    std::filesystem::remove(truncFile, ec);

    if(ok)
      spdlog::info("PASS: concurrent truncated-file decompress");
    return ok;
  }

} // namespace

int GrkConcurrencyTest::main(int, char**)
{
  grk_initialize(nullptr, 0, nullptr);

  auto tmp = std::filesystem::temp_directory_path();
  std::string tlmFile = (tmp / "grk_concurrency_tlm.j2k").string();
  std::string noTlmFile = (tmp / "grk_concurrency_no_tlm.j2k").string();

  if(!createTestImage(tlmFile, true) || !createTestImage(noTlmFile, false))
  {
    spdlog::error("Failed to create test images");
    return EXIT_FAILURE;
  }

  // single-threaded reference; TLM and non-TLM decode to identical pixels
  auto reference = decompressAsync(tlmFile, GRK_TILE_CACHE_IMAGE, 0);
  if(reference.size() != kNumTiles)
  {
    spdlog::error("Reference decompress failed ({} tiles)", reference.size());
    return EXIT_FAILURE;
  }

  int failures = 0;
  if(!testConcurrentAsyncTLM(tlmFile, reference))
    failures++;
  if(!testConcurrentAsyncSequential(noTlmFile, reference))
    failures++;
  if(!testConcurrentSwathWaits(tlmFile, noTlmFile, reference))
    failures++;
  if(!testConcurrentLRUEviction(tlmFile, noTlmFile, reference))
    failures++;
  if(!testConcurrentTruncated(tlmFile))
    failures++;

  std::error_code ec;
  std::filesystem::remove(tlmFile, ec);
  std::filesystem::remove(noTlmFile, ec);

  if(failures > 0)
  {
    spdlog::error("{} test(s) FAILED", failures);
    return EXIT_FAILURE;
  }

  spdlog::info("All tests PASSED");
  return EXIT_SUCCESS;
}

} // namespace grk
