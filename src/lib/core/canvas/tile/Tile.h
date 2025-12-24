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

namespace grk
{

/*
 * @struct Tile
 * @brief Stores information about a JPEG 2000 tile and its components
 *
 * Tile dimensions live in canvas coordinates, and are equal to the
 * full, non-windowed, unreduced tile dimensions.
 *
 * @ref TileComponent dimensions are reduced
 * if there is a resolution reduction.
 *
 */
struct Tile : public Rect32
{
  Tile() : Tile(0) {}

  explicit Tile(uint16_t numcomps) : numcomps_(numcomps), comps_(nullptr), distortion_(0)
  {
    if(numcomps)
      comps_ = new TileComponent[numcomps];
  }

  virtual ~Tile()
  {
    delete[] comps_;
  }

  double getLayerDistortion(uint16_t layer)
  {
    return getLayerDistortion()[layer];
  }

  void setLayerDistortion(uint16_t layer, double disto)
  {
    getLayerDistortion()[layer] = disto;
  }

  void incLayerDistortion(uint16_t layer, double distoDelta)
  {
    setLayerDistortion(layer, getLayerDistortion(layer) + distoDelta);
  }

  /**
   * @brief number of components
   */
  uint16_t numcomps_;

  /**
   * @brief array of @ref TileComponent
   */
  TileComponent* comps_;

  /**
   * @brief total tile distortion
   */
  double distortion_;

private:
  std::unique_ptr<double[]>& getLayerDistortion()
  {
    return layerDistortion_ ? layerDistortion_
                            : (layerDistortion_ = std::make_unique<double[]>(maxCompressLayersGRK),
                               std::ranges::fill_n(layerDistortion_.get(), maxCompressLayersGRK, 0),
                               layerDistortion_);
  }

  /**
   * @brief distortion by layer
   */
  std::unique_ptr<double[]> layerDistortion_;
};

} // namespace grk
