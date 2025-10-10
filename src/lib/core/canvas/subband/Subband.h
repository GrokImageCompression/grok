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
 * @struct Subband
 * @brief Stores sub band bounds and precincts
 *
 */
struct Subband : public Rect32
{
  /**
   * @brief Construct a new Subband object
   *
   */
  Subband() = default;

  /**
   * @brief Copy-constructs a new Subband object
   *
   * @param rhs right hand side of copy
   */
  Subband(const Subband& rhs);

  /**
   * @brief Destroys a Subband object
   *
   */
  virtual ~Subband() = default;

  /**
   * @brief operator=
   *
   * @param rhs right hand side of operator=
   * @return Subband&
   */
  Subband& operator=(const Subband& rhs);

  void print() const override;

  /**
   * @brief Returns true if this subband is empty i.e. one or more dimensions
   * of the subband is zero
   *
   * @return true if empty, otherwise false
   */
  bool empty();

  /**
   * @brief Gets a #ref Precinct if it has already been created, otherwise returns nullptr
   *
   * @param precinctIndex precinct index
   * @return Precinct* @ref Precinct if it exists, otherwise nullptr
   */
  Precinct* tryGetPrecinct(uint64_t precinctIndex);

  /**
   * @brief Generates band precinct bounds (canvas coordinates)
   *
   * @param precinctIndex precinct index
   * @param bandPrecinctPartition band precinct partition in canvas coordinates
   * @param bandPrecinctExpn log2 of nominal band precinct dimensions
   * @param precinctGridWidth precinct grid width
   * @return Rect32_16 precinct bounds in canvas coordinates
   */
  Rect32_16 generateBandPrecinctBounds(uint64_t precinctIndex, Rect32 bandPrecinctPartition,
                                       Point8 bandPrecinctExpn, uint32_t precinctGridWidth);

  /**
   * @brief Creates a @ref Precinct
   *
   * @param numLayers number of layers
   * @param precinctIndex precinct index
   * @param bandPrecinctPartition precinct partition top left hand corner
   * @param bandPrecinctExpn log2 nominal precinct dimensions
   * @param precinctGridWidth precinct grid width
   * @param cblk_expn lo2 of code block dimensions
   * @return Precinct* @ref Precinct
   */
  Precinct* createPrecinct(bool isCompressor, uint16_t numLayers, uint64_t precinctIndex,
                           Rect32 bandPrecinctPartition, Point8 bandPrecinctExpn,
                           uint32_t precinctGridWidth, Point8 cblk_expn);

  /**
   * @brief band orientation
   *
   */
  eBandOrientation orientation_ = BAND_ORIENT_LL;

  /**
   * @brief flat vector of precincts
   *
   */
  std::vector<Precinct*> precincts_;

  /**
   * @brief maps global precinct index to precincts vector index
   *
   */
  std::unordered_map<uint64_t, uint64_t> precinctMap_;

  /**
   * @brief band max number of bit planes
   *
   */
  uint8_t maxBitPlanes_ = 0;

  /**
   * @brief quantization step size
   *
   */
  float stepsize_ = 0;
};

} // namespace grk
