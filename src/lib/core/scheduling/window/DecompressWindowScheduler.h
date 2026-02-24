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

#include "TileBlocks.h"
#include "WindowScheduler.h"

namespace grk
{

/**
 * @class DecompressWindowScheduler
 * @brief abstract class to graph and execute T1 tasks for windowed tile
 *
 * Task scheduling will be performed by derived classes
 */
class DecompressWindowScheduler : public WindowScheduler
{
public:
  /**
   * @brief Constructs a WindowScheduler
   * @param numComps number of components
   * @param streamPool code stream pool
   */
  DecompressWindowScheduler(uint16_t numComps, uint8_t prec, CoderPool* streamPool);

  /**
   * @brief Destroys a WindowScheduler
   */
  virtual ~DecompressWindowScheduler();

  bool schedule(TileProcessor* tileProcessor) override;

  void release(void) override;

private:
  DifferentialInfo* differentialInfo_;

  /**
   * @brief precision of input image
   */
  uint8_t prec_;

  CoderPool coderPool_;
  CoderPool* streamPool_;
};

} // namespace grk
