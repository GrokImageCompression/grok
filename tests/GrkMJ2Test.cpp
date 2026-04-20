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
#include <string>
#include <filesystem>
#include <vector>

#include "grok.h"
#include "spdlog/spdlog.h"
#include "GrkMJ2Test.h"

template<size_t N>
static void safe_strcpy(char (&dest)[N], const char* src)
{
  size_t len = strnlen(src, N - 1);
  memcpy(dest, src, len);
  dest[len] = '\0';
}

namespace grk
{

static constexpr uint32_t kFrameWidth = 32;
static constexpr uint32_t kFrameHeight = 32;
static constexpr uint32_t kNumFrames = 4;

static grk_image* createTestFrame(uint32_t frameIndex, bool rgb)
{
  uint16_t numcomps = rgb ? 3 : 1;
  std::vector<grk_image_comp> comps(numcomps);
  for(uint16_t c = 0; c < numcomps; ++c)
  {
    comps[c].dx = 1;
    comps[c].dy = 1;
    comps[c].w = kFrameWidth;
    comps[c].h = kFrameHeight;
    comps[c].prec = 8;
    comps[c].sgnd = 0;
  }

  auto* image =
      grk_image_new(numcomps, comps.data(), rgb ? GRK_CLRSPC_SRGB : GRK_CLRSPC_GRAY, true);
  if(!image)
    return nullptr;

  // Fill with a per-frame deterministic pattern
  for(uint16_t c = 0; c < numcomps; ++c)
  {
    auto* data = static_cast<int32_t*>(image->comps[c].data);
    for(uint32_t i = 0; i < kFrameWidth * kFrameHeight; ++i)
      data[i] = static_cast<int32_t>((i + frameIndex * 37 + c * 71) % 256);
  }
  return image;
}

static bool verifyFramePixels(const grk_image* image, uint32_t frameIndex, bool rgb)
{
  uint16_t numcomps = rgb ? 3 : 1;
  if(image->numcomps != numcomps)
  {
    spdlog::error("Frame {}: expected {} components, got {}", frameIndex, numcomps,
                  image->numcomps);
    return false;
  }
  if(image->comps[0].w != kFrameWidth || image->comps[0].h != kFrameHeight)
  {
    spdlog::error("Frame {}: unexpected dimensions {}x{}", frameIndex, image->comps[0].w,
                  image->comps[0].h);
    return false;
  }
  for(uint16_t c = 0; c < numcomps; ++c)
  {
    auto* data = static_cast<int32_t*>(image->comps[c].data);
    if(!data)
    {
      spdlog::error("Frame {} component {}: null data", frameIndex, c);
      return false;
    }
    for(uint32_t i = 0; i < kFrameWidth * kFrameHeight; ++i)
    {
      int32_t expected = static_cast<int32_t>((i + frameIndex * 37 + c * 71) % 256);
      if(data[i] != expected)
      {
        spdlog::error("Frame {} comp {} pixel {}: expected {}, got {}", frameIndex, c, i, expected,
                      data[i]);
        return false;
      }
    }
  }
  return true;
}

//==============================================================================
// Test 1: MJ2 grayscale round-trip (lossless)
//==============================================================================
static int testMJ2GrayRoundTrip(const std::string& tmpDir)
{
  spdlog::info("=== Test: MJ2 grayscale round-trip ===");

  std::string mj2Path = tmpDir + "/gray_test.mj2";

  // Compress frames
  grk_cparameters cparams{};
  grk_compress_set_default_params(&cparams);
  cparams.cod_format = GRK_FMT_MJ2;
  cparams.irreversible = false;
  cparams.numlayers = 1;
  cparams.layer_rate[0] = 0;

  grk_object* codec = nullptr;
  for(uint32_t f = 0; f < kNumFrames; ++f)
  {
    grk_image* image = createTestFrame(f, false);
    if(!image)
    {
      spdlog::error("Failed to create frame {}", f);
      if(codec)
        grk_object_unref(codec);
      return 1;
    }

    if(f == 0)
    {
      grk_stream_params streamParams{};
      safe_strcpy(streamParams.file, mj2Path.c_str());
      codec = grk_compress_init(&streamParams, &cparams, image);
      if(!codec)
      {
        spdlog::error("Failed to init MJ2 compressor");
        grk_object_unref(&image->obj);
        return 1;
      }
      if(!grk_compress(codec, nullptr))
      {
        spdlog::error("Failed to compress first frame");
        grk_object_unref(&image->obj);
        grk_object_unref(codec);
        return 1;
      }
    }
    else
    {
      if(!grk_compress_frame(codec, image, nullptr))
      {
        spdlog::error("Failed to compress frame {}", f);
        grk_object_unref(&image->obj);
        grk_object_unref(codec);
        return 1;
      }
    }
    grk_object_unref(&image->obj);
  }

  if(!grk_compress_finish(codec))
  {
    spdlog::error("Failed to finalize MJ2");
    grk_object_unref(codec);
    return 1;
  }
  grk_object_unref(codec);
  codec = nullptr;

  // Decompress and verify
  grk_stream_params streamParams{};
  safe_strcpy(streamParams.file, mj2Path.c_str());
  grk_decompress_parameters dparams{};
  codec = grk_decompress_init(&streamParams, &dparams);
  if(!codec)
  {
    spdlog::error("Failed to init MJ2 decompressor");
    return 1;
  }

  grk_header_info headerInfo{};
  if(!grk_decompress_read_header(codec, &headerInfo))
  {
    spdlog::error("Failed to read MJ2 header");
    grk_object_unref(codec);
    return 1;
  }

  if(!grk_decompress(codec, nullptr))
  {
    spdlog::error("Failed to decompress MJ2");
    grk_object_unref(codec);
    return 1;
  }

  uint32_t numSamples = grk_decompress_num_samples(codec);
  if(numSamples != kNumFrames)
  {
    spdlog::error("Expected {} frames, got {}", kNumFrames, numSamples);
    grk_object_unref(codec);
    return 1;
  }

  for(uint32_t s = 0; s < numSamples; ++s)
  {
    auto* img = grk_decompress_get_sample_image(codec, s);
    if(!img)
    {
      spdlog::error("Failed to get sample image {}", s);
      grk_object_unref(codec);
      return 1;
    }
    if(!verifyFramePixels(img, s, false))
    {
      grk_object_unref(codec);
      return 1;
    }
  }

  grk_object_unref(codec);
  spdlog::info("MJ2 grayscale round-trip: PASSED ({} frames)", kNumFrames);
  return 0;
}

//==============================================================================
// Test 2: MJ2 RGB round-trip (lossless)
//==============================================================================
static int testMJ2RGBRoundTrip(const std::string& tmpDir)
{
  spdlog::info("=== Test: MJ2 RGB round-trip ===");

  std::string mj2Path = tmpDir + "/rgb_test.mj2";

  grk_cparameters cparams{};
  grk_compress_set_default_params(&cparams);
  cparams.cod_format = GRK_FMT_MJ2;
  cparams.irreversible = false;
  cparams.numlayers = 1;
  cparams.layer_rate[0] = 0;
  cparams.mct = 1;

  grk_object* codec = nullptr;
  for(uint32_t f = 0; f < kNumFrames; ++f)
  {
    grk_image* image = createTestFrame(f, true);
    if(!image)
    {
      spdlog::error("Failed to create RGB frame {}", f);
      if(codec)
        grk_object_unref(codec);
      return 1;
    }

    if(f == 0)
    {
      grk_stream_params streamParams{};
      safe_strcpy(streamParams.file, mj2Path.c_str());
      codec = grk_compress_init(&streamParams, &cparams, image);
      if(!codec)
      {
        spdlog::error("Failed to init RGB MJ2 compressor");
        grk_object_unref(&image->obj);
        return 1;
      }
      if(!grk_compress(codec, nullptr))
      {
        spdlog::error("Failed to compress first RGB frame");
        grk_object_unref(&image->obj);
        grk_object_unref(codec);
        return 1;
      }
    }
    else
    {
      if(!grk_compress_frame(codec, image, nullptr))
      {
        spdlog::error("Failed to compress RGB frame {}", f);
        grk_object_unref(&image->obj);
        grk_object_unref(codec);
        return 1;
      }
    }
    grk_object_unref(&image->obj);
  }

  if(!grk_compress_finish(codec))
  {
    spdlog::error("Failed to finalize RGB MJ2");
    grk_object_unref(codec);
    return 1;
  }
  grk_object_unref(codec);
  codec = nullptr;

  // Decompress and verify
  grk_stream_params streamParams{};
  safe_strcpy(streamParams.file, mj2Path.c_str());
  grk_decompress_parameters dparams{};
  codec = grk_decompress_init(&streamParams, &dparams);
  if(!codec)
  {
    spdlog::error("Failed to init RGB MJ2 decompressor");
    return 1;
  }

  grk_header_info headerInfo{};
  if(!grk_decompress_read_header(codec, &headerInfo))
  {
    spdlog::error("Failed to read RGB MJ2 header");
    grk_object_unref(codec);
    return 1;
  }

  if(!grk_decompress(codec, nullptr))
  {
    spdlog::error("Failed to decompress RGB MJ2");
    grk_object_unref(codec);
    return 1;
  }

  uint32_t numSamples = grk_decompress_num_samples(codec);
  if(numSamples != kNumFrames)
  {
    spdlog::error("RGB: expected {} frames, got {}", kNumFrames, numSamples);
    grk_object_unref(codec);
    return 1;
  }

  for(uint32_t s = 0; s < numSamples; ++s)
  {
    auto* img = grk_decompress_get_sample_image(codec, s);
    if(!img)
    {
      spdlog::error("RGB: failed to get sample image {}", s);
      grk_object_unref(codec);
      return 1;
    }
    if(!verifyFramePixels(img, s, true))
    {
      grk_object_unref(codec);
      return 1;
    }
  }

  grk_object_unref(codec);
  spdlog::info("MJ2 RGB round-trip: PASSED ({} frames)", kNumFrames);
  return 0;
}

//==============================================================================
// Test 3: MJ2 single-frame edge case
//==============================================================================
static int testMJ2SingleFrame(const std::string& tmpDir)
{
  spdlog::info("=== Test: MJ2 single-frame ===");

  std::string mj2Path = tmpDir + "/single_frame.mj2";

  grk_cparameters cparams{};
  grk_compress_set_default_params(&cparams);
  cparams.cod_format = GRK_FMT_MJ2;
  cparams.irreversible = false;
  cparams.numlayers = 1;
  cparams.layer_rate[0] = 0;

  grk_image* image = createTestFrame(0, false);
  if(!image)
  {
    spdlog::error("Failed to create single frame");
    return 1;
  }

  grk_stream_params streamParams{};
  safe_strcpy(streamParams.file, mj2Path.c_str());
  auto* codec = grk_compress_init(&streamParams, &cparams, image);
  if(!codec)
  {
    spdlog::error("Failed to init single-frame compressor");
    grk_object_unref(&image->obj);
    return 1;
  }

  if(!grk_compress(codec, nullptr))
  {
    spdlog::error("Failed to compress single frame");
    grk_object_unref(&image->obj);
    grk_object_unref(codec);
    return 1;
  }
  grk_object_unref(&image->obj);

  if(!grk_compress_finish(codec))
  {
    spdlog::error("Failed to finalize single-frame MJ2");
    grk_object_unref(codec);
    return 1;
  }
  grk_object_unref(codec);
  codec = nullptr;

  // Decompress
  grk_stream_params dStreamParams{};
  safe_strcpy(dStreamParams.file, mj2Path.c_str());
  grk_decompress_parameters dparams{};
  codec = grk_decompress_init(&dStreamParams, &dparams);
  if(!codec)
  {
    spdlog::error("Failed to init single-frame decompressor");
    return 1;
  }

  grk_header_info headerInfo{};
  if(!grk_decompress_read_header(codec, &headerInfo))
  {
    spdlog::error("Failed to read single-frame MJ2 header");
    grk_object_unref(codec);
    return 1;
  }

  if(!grk_decompress(codec, nullptr))
  {
    spdlog::error("Failed to decompress single-frame MJ2");
    grk_object_unref(codec);
    return 1;
  }

  uint32_t numSamples = grk_decompress_num_samples(codec);
  if(numSamples != 1)
  {
    spdlog::error("Single-frame: expected 1 sample, got {}", numSamples);
    grk_object_unref(codec);
    return 1;
  }

  auto* img = grk_decompress_get_sample_image(codec, 0);
  if(!img)
  {
    spdlog::error("Single-frame: failed to get sample image");
    grk_object_unref(codec);
    return 1;
  }

  if(!verifyFramePixels(img, 0, false))
  {
    grk_object_unref(codec);
    return 1;
  }

  grk_object_unref(codec);
  spdlog::info("MJ2 single-frame: PASSED");
  return 0;
}

//==============================================================================
// Entry point
//==============================================================================
int GrkMJ2Test::main([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
  grk_initialize(nullptr, UINT32_MAX, nullptr);

  auto tmpDir = std::filesystem::temp_directory_path() / "grk_mj2_test";
  std::filesystem::create_directories(tmpDir);

  int failures = 0;
  failures += testMJ2GrayRoundTrip(tmpDir.string());
  failures += testMJ2RGBRoundTrip(tmpDir.string());
  failures += testMJ2SingleFrame(tmpDir.string());

  // Clean up temp files
  std::filesystem::remove_all(tmpDir);

  grk_deinitialize();

  if(failures == 0)
    spdlog::info("All MJ2 tests PASSED");
  else
    spdlog::error("{} MJ2 test(s) FAILED", failures);

  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace grk
