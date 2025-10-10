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

#pragma once

namespace grk
{

/**
 * @struct IDecompressor
 * @brief Decompress interface
 *
 */
struct IDecompressor
{
public:
  /**
   * @brief Destroys the IDecompressor object
   *
   */
  virtual ~IDecompressor() = default;
  /**
   * @brief Reads header
   * @param @ref grk_header_info
   * @return true if read succeeds
   */
  virtual bool readHeader(grk_header_info* header_info) = 0;
  /**
   * @brief Gets @ref GrkImage for specified tile
   * @param tile_index tile index
   * @return @ref GrkImage
   */
  virtual GrkImage* getImage(uint16_t tile_index, bool wait) = 0;
  /**
   * @brief Gets composite @ref GrkImage for all tiles in decompress region
   * @return @ref GrkImage
   */
  virtual GrkImage* getImage(void) = 0;

  /**
   * @brief Waits for asynchronous decompression to complete
   *
   */
  virtual void wait(grk_wait_swath* swath) = 0;

  /**
   * @brief Initializes decompressor
   * @param @ref grk_decompress_parameters
   */
  virtual void init(grk_decompress_parameters* param) = 0;

  /**
   * @brief Gets the @ref grk_progression_state for a tile
   *
   * @param tile_index
   * @return grk_progression_state
   */
  virtual grk_progression_state getProgressionState(uint16_t tile_index) = 0;

  /**
   * @brief Sets the @ref grk_progression_state for a tile
   *
   * @param state @ref grk_progression_state
   * @return true if successful
   */
  virtual bool setProgressionState(grk_progression_state state) = 0;

  /**
   * @brief Decompresses image / image region
   *
   * @param tile @ref grk_plugin_tile
   * @return true if successful
   */
  virtual bool decompress(grk_plugin_tile* tile) = 0;

  /**
   * @brief Decompresses a single tile
   *
   * @param tile_index tile index
   * @return true if successful
   */
  virtual bool decompressTile(uint16_t tile_index) = 0;

  /**
   * @brief Dumps image tags to file
   *
   * @param flag flag indicating main header, tile header etc.
   * @param outputFileStream FILE stream
   */
  virtual void dump(uint32_t flag, FILE* outputFileStream) = 0;
};

} // namespace grk
