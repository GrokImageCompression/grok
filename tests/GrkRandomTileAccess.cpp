/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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
#include <array>
#include "grk_apps_config.h"
#include "grok.h"
#include "spdlog/spdlog.h"
#include "GrkRandomTileAccess.h"

template<size_t N>
void safe_strcpy(char (&dest)[N], const char* src)
{
  size_t len = strnlen(src, N - 1);
  memcpy(dest, src, len);
  dest[len] = '\0';
}

namespace grk
{

// RAII wrapper for codec
struct CodecDeleter
{
  void operator()(grk_object* codec) const
  {
    if(codec)
    {
      grk_object_unref(codec);
    }
  }
};
using CodecPtr = std::unique_ptr<grk_object, CodecDeleter>;

static int32_t testTile(uint16_t tileIndex, const grk_image* image, grk_object* codec)
{
  spdlog::info("Decompressing tile {}", tileIndex);

  if(!grk_decompress_tile(codec, tileIndex))
  {
    spdlog::error("Failed to decompress tile {}", tileIndex);
    return EXIT_FAILURE;
  }

  for(uint16_t index = 0; index < image->numcomps; ++index)
  {
    if(image->comps[index].data == nullptr)
    {
      spdlog::error("Tile {} component {} has no data", tileIndex, index);
      return EXIT_FAILURE;
    }
  }

  spdlog::info("Tile {} decoded successfully", tileIndex);
  return EXIT_SUCCESS;
}

int GrkRandomTileAccess::main(int argc, char** argv)
{
  // Initialize Grok
  grk_initialize(nullptr, 0, nullptr);

  // RAII for cleanup
  struct GrkCleanup
  {
    ~GrkCleanup()
    {
      grk_deinitialize();
    }
  } cleanup;

  if(argc != 2)
  {
    spdlog::error("Usage: {} <input_file>", argv[0]);
    return EXIT_FAILURE;
  }

  std::string input_file(argv[1]);
  grk_decompress_parameters parameters{};

  // Test four corner tiles
  std::array<uint16_t, 4> tiles{};
  for(uint32_t i = 0; i < 4; ++i)
  {
    // Set up codec with RAII
    grk_stream_params stream_params{};
    safe_strcpy(stream_params.file, input_file.data());

    CodecPtr codec(grk_decompress_init(&stream_params, &parameters));
    if(!codec)
    {
      spdlog::error("Failed to initialize decompressor for {}", input_file);
      return EXIT_FAILURE;
    }

    // Read header
    grk_header_info headerInfo{};
    if(!grk_decompress_read_header(codec.get(), &headerInfo))
    {
      spdlog::error("Failed to read header from {}", input_file);
      return EXIT_FAILURE;
    }

    if(i == 0)
    { // Only log tile grid info once
      spdlog::info("File contains {}x{} tiles", headerInfo.t_grid_width, headerInfo.t_grid_height);
    }

    // Calculate corner tile indices
    tiles[0] = 0; // Top-left
    tiles[1] = static_cast<uint16_t>(headerInfo.t_grid_width - 1); // Top-right
    tiles[2] = static_cast<uint16_t>(headerInfo.t_grid_width * headerInfo.t_grid_height -
                                     1); // Bottom-right
    tiles[3] = static_cast<uint16_t>(tiles[2] - headerInfo.t_grid_width); // Bottom-left

    const grk_image* image = grk_decompress_get_image(codec.get());
    if(!image)
    {
      spdlog::error("Failed to get image data for {}", input_file);
      return EXIT_FAILURE;
    }

    int32_t rc = testTile(tiles[i], image, codec.get());
    if(rc != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }
  }

  spdlog::info("All corner tiles decoded successfully");
  return EXIT_SUCCESS;
}

} // namespace grk