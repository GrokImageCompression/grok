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

#include <set>
#include <cstdint>
#include "geometry.h"

namespace grk
{

/**
 * @class TileWindow
 * @brief Stores state of rectangular window of tiles slated for decompression
 * Each tile's slated or completely parsed state is stored
 *
 */
class TileWindow
{
public:
  /**
   * @brief Constructs new TileWindow
   */
  TileWindow();

  /**
   * @brief Destroys TileWindow
   */
  virtual ~TileWindow() = default;

  /**
   * @brief Initializes TileWindow
   * @param allTiles total number of tiles in window
   */
  void init(Rect16 allTiles);

  /**
   * @brief Slates tiles inside window
   * @param tiles tile grid
   */
  void slate(Rect16 tiles);

  /**
   * @brief Slates tile at tile index
   * @param tile_index tile index
   */
  void slate(uint16_t tile_index);

  /**
   * @brief Checks if tile at given tile index is slated
   * @param tile_index tile index
   * @return true if tile is slated for decompression
   */
  bool isSlated(uint16_t tile_index);

  /**
   * @brief Gets the total number of tiles in window
   * @return uint16_t
   */
  uint16_t getTotalNumTiles(void);

  std::set<uint16_t>& getSlatedTiles(void);

  Rect16 getSlatedTileRect(void);

private:
  /**
   * @brief Slates tile at window grid point
   * @param tile tile grid point
   */
  void slate(Point16 tile);

  /**
   * @brief Gets tile index from tile grid coordinates
   * @param tile tile grid coordinates
   * @return uint16_t tile index
   */
  uint16_t index(Point16 tile);

  /**
   * @brief tiles slated for decompression
   */
  std::set<uint16_t> tilesSlatedForDecompression_;

  /**
   * @brief rectangular grid of all slated tiles
   */
  Rect16 allTiles_;

  /**
   * @brief tiles slated for decompression, in Rect16 format
   *
   */
  Rect16 slatedTiles_;
};

} // namespace grk
