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

const uint8_t gain_b[4] = {0, 1, 1, 2};

/**
 * @struct ResBlocks
 * @brief store decompression blocks for a resolution
 */
struct ResBlocks
{
  /**
   * @brief constructs ResBlocks
   */
  ResBlocks(void) = default;

  /**
   * @brief clear all blocks
   */
  void clear(void)
  {
    blocks_.clear();
  }

  /**
   * @brief return true if empty
   */
  bool empty(void) const
  {
    return blocks_.empty();
  }

  /**
   * @brief unref and clear all blocks
   */
  void release(void)
  {
    blocks_.clear();
  }

  /**
   * @brief Combines another ResBlocks into this one
   */
  void combine(const ResBlocks& other)
  {
    // Append blocks from the other ResBlocks to this one
    blocks_.insert(blocks_.end(), other.blocks_.begin(), other.blocks_.end());
  }

  /**
   * @brief vector of block arrays, indexed by resolution
   */
  std::vector<std::shared_ptr<DecompressBlockExec>> blocks_;
};

typedef std::vector<ResBlocks> ComponentBlocks;
typedef std::vector<ComponentBlocks> TileBlocks;

} // namespace grk