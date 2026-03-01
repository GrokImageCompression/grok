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

#include "SchedulerExcalibur.h"

namespace grk
{

/**
 * @class CompressSchedulerExcalibur
 * @brief Compresses a tile using windowed design
 *
 */
class CompressSchedulerExcalibur : public SchedulerExcalibur
{
public:
  /**
   * @brief Constructs a CompressSchedulerExcalibur
   * @param tile @ref Tile
   * @param needsRateControl true if rate control needed
   * @param tcp @ref TileCodingParams
   * @param mct_norms array of mct norms
   * @param mct_numcomps number of mct components
   */
  CompressSchedulerExcalibur(Tile* tile, bool needsRateControl, TileCodingParams* tcp,
                             const double* mct_norms, uint16_t mct_numcomps);

  /**
   * @brief Destroys a SchedulerExcalibur
   */
  virtual ~CompressSchedulerExcalibur() = default;

private:
  /**
   * @brief @ref Tile to compress
   */
  Tile* tile_;

  /**
   * @brief mutex to serialize distortion decrease from blocks
   */
  mutable std::mutex distortion_mutex_;
  /**
   * @brief true if rate control requested
   */
  bool needsRateControl_;
  /**
   * @brief vector of @ref CompressBlockExec encode blocks
   */
  std::vector<t1::CompressBlockExec*> encodeBlocks_;

  /**
   * @brief atomic counter to keep track of number of encoded blocks
   */
  std::atomic<int64_t> blockCount_;

  /**
   * @brief @ref TileCodingParams for this tile
   */
  TileCodingParams* tcp_;

  /**
   * @brief array of mct norms
   */
  const double* mct_norms_;

  /**
   * @brief number of components to apply mct to
   */
  uint16_t mct_numcomps_;
};

} // namespace grk
