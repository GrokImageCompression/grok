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

#include "WholeTileScheduler.h"
#include "TileBlocks.h"

namespace grk
{

/**
 * @class DecompressScheduler
 * @brief schedule and run T1 decompression
 *
 */
class DecompressScheduler : public WholeTileScheduler
{
public:
  /**
   * @brief Constructs a DecompressScheduler
   *
   * @param numcomps number of components
   * @param prec precision of input image
   * @param streamPool code stream pool
   */
  DecompressScheduler(uint16_t numcomps, uint8_t prec, CoderPool* streamPool);

  /**
   * @brief Destroys a DecompressScheduler
   */
  ~DecompressScheduler();

  bool schedule(TileProcessor* tileProcessor) override;

  void release(void) override;

private:
  /**
   * @brief Generates a new @ref FlowComponet for pre/post processing
   */
  FlowComponent* genPrePostProc(void);

  /**
   * @brief precision of input image
   */
  uint8_t prec_;

  /**
   * @brief @ref TileBlocks
   */
  TileBlocks blocksByTile_;

  /**
   * @brief vector of @ref WaveletReverse pointers
   */
  std::vector<WaveletReverse*> waveletReverse_;

  DifferentialInfo* differentialInfo_;

  /**
   * @brief @ref FlowComponent for pre/post processing
   */
  FlowComponent* prePostProc_;

  CoderPool coderPool_;
  CoderPool* streamPool_;
};

} // namespace grk
