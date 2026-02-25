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

#pragma once

#include "grk_restrict.h"

namespace grk
{

/**
 * @struct Resolution
 * @brief Stores a tile component resolution's dimensions, sub bands and other information
 *
 */
struct Resolution : public Rect32
{
  /**
   * @brief Constructs a new Resolution object
   *
   */
  Resolution(void) = default;
  /**
   * @brief Destroys the Resolution object
   *
   */
  virtual ~Resolution(void);

  virtual void print() const override;

  /**
   * @brief Initializes a Resolution
   *
   * @param tileProcessor @ref TileProcessor
   * @param tccp @ref TileComponentCodingParams
   * @param resno resolution number (0 is lowest resolution)
   * @return true if successful, otherwise false
   */
  bool init(grk_plugin_tile* currentPluginTile, bool isCompressor, uint16_t numLayers,
            TileProcessor* tileProcessor, TileComponentCodingParams* tccp, uint8_t resno);

  static Rect32 genPrecinctPartition(const Rect32& window, uint8_t precWidthExp,
                                     uint8_t precHeightExp);

  /**
   * @brief true when Resolution is initialized
   *
   */
  bool initialized_ = false;

  /**
   * @brief unreduced bands (canvas coordinates)
   *
   */
  Subband band[t1::BAND_NUM_INDICES];

  uint8_t numBands_ = 0; // 1 or 3
  Rect32 precinctPartition_;
  Rect32 precinctGrid_;
  Rect32 bandPrecinctPartition_;
  Point8 bandPrecinctExpn_;
  Point8 cblkExpn_;
  grk_plugin_tile* current_plugin_tile_ = nullptr;
  ResolutionPacketParser* packetParser_ = nullptr;
};

/* <summary>                             */
/* Determine maximum computed resolution level for inverse wavelet transform */
/* </summary>                            */
inline uint32_t max_resolution(const Resolution* GRK_RESTRICT r, uint32_t i)
{
  uint32_t mr = 0;
  while(--i)
  {
    ++r;
    uint32_t w;
    if(mr < (w = r->x1 - r->x0))
      mr = w;
    if(mr < (w = r->y1 - r->y0))
      mr = w;
  }
  return mr;
}

} // namespace grk
