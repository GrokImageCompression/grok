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

#include <vector>
#include <numeric>

#include "buffer.h"
#include "t1_common.h"
#include "CodeblockImpl.h"

namespace grk
{

/**
 * @struct Codeblock
 * @brief Code block information for both compression and decompression
 *
 * Block lives in canvas coordinates
 */
struct Codeblock : public Rect32_16
{
protected:
  /**
   * @brief Creates a Codeblock
   * @param numlayers Number of layers in code block
   */
  explicit Codeblock(uint16_t numLayers) : numLayers_(numLayers) {}
  /**
   * @brief Destroys a Codeblock
   */
  ~Codeblock() = default;

  /**
   * @brief Number of layers in code block
   */
  uint16_t numLayers_;

private:
  explicit Codeblock(const Codeblock& rhs) = default;
  Codeblock& operator=(const Codeblock& rhs) = default;
};

} // namespace grk
