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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory_resource>
#include <queue>
#include <vector>

namespace grk
{

template<typename T>
class TagTree
{
public:
  /**
   * @brief TagTree constructor
   *
   * @param leavesWidth Width of the array of leaves of the tree
   * @param leavesHeight Height of the array of leaves of the tree
   *
   * @return a new tag tree if successful, otherwise nullptr
   */

  TagTree(uint16_t leavesWidth, uint16_t leavesHeight, std::pmr::memory_resource* arena)
      : leavesWidth_(leavesWidth), leavesHeight_(leavesHeight), pageOffset_(arena), pool_(arena)
  {
    buildLevels();
  }

  ~TagTree() = default;

  constexpr T getUninitializedValue() const noexcept
  {
    return (std::numeric_limits<T>::max)();
  }

  /**
   Reset a tag tree (set all leaves to 0)
   */
  void reset()
  {
    std::fill(pool_.begin(), pool_.end(), unusedNode());
  }

  /**
   Set the value of a leaf of a tag tree
   @param leafno leaf to modify
   @param value  new value of leaf
   */
  void set(uint64_t leafno, T value)
  {
    uint32_t path[maxLevels];
    pathToRoot(leafno, path);
    for(uint8_t level = 0; level < numLevels_; ++level)
    {
      auto& n = nodeAt(path[level]);
      if(n.value <= value)
        break;
      n.value = value;
    }
  }

  /**
   Encode the value of a leaf of the tag tree up to a given threshold
   @param bio BIO handle
   @param leafno leaf to compress
   @param threshold Threshold to use when encoding value of the leaf
   @return true if successful, otherwise false
   */
  bool encode(t1_t2::BitIO* bio, uint64_t leafno, T threshold)
  {
    uint32_t path[maxLevels];
    pathToRoot(leafno, path);
    T low = 0;
    for(auto level = numLevels_; level-- > 0;)
    {
      auto& n = nodeAt(path[level]);
      if(n.low < low)
        n.low = low;
      else
        low = n.low;

      while(low < threshold)
      {
        if(low >= n.value)
        {
          if(!n.known)
          {
            if(!bio->write(1))
              return false;
            n.known = true;
          }
          break;
        }
        if(!bio->write(0))
          return false;
        ++low;
      }
      n.low = low;
    }
    return true;
  }

  /**
   Decode the value of a leaf of the tag tree up to a given threshold
   @param bio Pointer to a BIO handle
   @param leafno Number that identifies the leaf to decode
   @param threshold Threshold to use when decoding value of the leaf
   @param value the node's value
   */
  // a node reads bits only at its first leaf under a known parent, so other leaves are skipped
  template<typename Visit>
  void forEachLeafThatMayReadBits(Visit visit)
  {
    std::array<std::byte, 1024> pendingStorage;
    std::pmr::monotonic_buffer_resource pendingResource(pendingStorage.data(),
                                                        pendingStorage.size());
    PendingLeaves pending(std::greater<uint64_t>{}, std::pmr::vector<uint64_t>(&pendingResource));
    pending.push(0);
    while(!pending.empty())
    {
      auto leafno = pending.top();
      pending.pop();
      visit(leafno);
      pushChildFirstLeaves(leafno, pending);
    }
  }
  void decode(t1_t2::BitIO* bio, uint64_t leafno, T threshold, T* value)
  {
    auto leafValue = nodeAt(static_cast<uint32_t>(leafno)).value;
    if(leafValue < threshold) [[likely]]
    {
      *value = leafValue;
      return;
    }

    uint32_t path[maxLevels];
    pathToRoot(leafno, path);
    T low = 0;
    for(auto level = numLevels_; level-- > 0;)
    {
      auto& n = nodeAt(path[level]);
      if(n.low < low)
        n.low = low;
      else
        low = n.low;

      while(low < threshold && low < n.value) [[likely]]
      {
        if(bio->read())
        {
          n.value = low;
          break;
        }
        ++low;
      }
      n.low = low;
    }
    *value = nodeAt(static_cast<uint32_t>(leafno)).value;
  }

private:
  struct Node
  {
    T value;
    T low;
    bool known;
  };
  using PendingLeaves =
      std::priority_queue<uint64_t, std::pmr::vector<uint64_t>, std::greater<uint64_t>>;

  // 16 bit leaf dimensions give at most 16 halvings above the leaves
  static constexpr uint8_t maxLevels = 17;
  static constexpr uint8_t pageShift = 10;
  static constexpr uint32_t pageSize = 1u << pageShift;
  static constexpr uint32_t unallocatedPage = UINT32_MAX;

  uint32_t nodeIndex(uint8_t level, uint32_t row, uint32_t column) const
  {
    return levelBase_[level] + row * levelWidth_[level] + column;
  }
  void pathToRoot(uint64_t leafno, uint32_t* path) const
  {
    uint32_t row = static_cast<uint32_t>(leafno) / leavesWidth_;
    uint32_t column = static_cast<uint32_t>(leafno) % leavesWidth_;
    for(uint8_t level = 0; level < numLevels_; ++level, row >>= 1, column >>= 1)
      path[level] = nodeIndex(level, row, column);
  }
  uint64_t firstLeaf(uint8_t level, uint32_t row, uint32_t column) const
  {
    return (static_cast<uint64_t>(row) << level) * leavesWidth_ +
           (static_cast<uint64_t>(column) << level);
  }
  void pushChildFirstLeaves(uint64_t leafno, PendingLeaves& pending)
  {
    uint32_t row = static_cast<uint32_t>(leafno) / leavesWidth_;
    uint32_t column = static_cast<uint32_t>(leafno) % leavesWidth_;
    for(uint8_t level = 0; level < numLevels_; ++level, row >>= 1, column >>= 1)
    {
      if(firstLeaf(level, row, column) != leafno)
        break;
      if(level == 0 || nodeAt(nodeIndex(level, row, column)).value == getUninitializedValue())
        continue;
      uint8_t childLevel = level - 1;
      uint32_t childRowEnd = std::min<uint32_t>(2 * row + 2, levelHeight_[childLevel]);
      uint32_t childColumnEnd = std::min<uint32_t>(2 * column + 2, levelWidth_[childLevel]);
      for(uint32_t childRow = 2 * row; childRow < childRowEnd; ++childRow)
      {
        for(uint32_t childColumn = 2 * column; childColumn < childColumnEnd; ++childColumn)
        {
          bool topLeftChild = childRow == 2 * row && childColumn == 2 * column;
          if(!topLeftChild)
            pending.push(firstLeaf(childLevel, childRow, childColumn));
        }
      }
    }
  }
  // a huge precinct with a short header must not allocate every node
  Node& nodeAt(uint32_t index)
  {
    if(pageOffset_.empty()) [[likely]]
      return pool_[index];
    uint32_t pageIndex = index >> pageShift;
    uint32_t pageOffset = pageOffset_[pageIndex];
    if(pageOffset == unallocatedPage) [[unlikely]]
      pageOffset = allocatePage(pageIndex);
    return pool_[pageOffset + (index & (pageSize - 1))];
  }
  uint32_t allocatePage(uint32_t pageIndex)
  {
    auto pageOffset = static_cast<uint32_t>(pool_.size());
    pageOffset_[pageIndex] = pageOffset;
    uint32_t pageNodes = std::min(pageSize, totalNodes_ - (pageIndex << pageShift));
    pool_.resize(pool_.size() + pageNodes, unusedNode());
    return pageOffset;
  }
  Node unusedNode() const
  {
    return Node{getUninitializedValue(), 0, false};
  }
  void buildLevels()
  {
    uint16_t width = leavesWidth_;
    uint16_t height = leavesHeight_;
    totalNodes_ = 0;
    uint32_t nodesInLevel;
    numLevels_ = 0;
    do
    {
      nodesInLevel = static_cast<uint32_t>(width) * height;
      levelWidth_[numLevels_] = width;
      levelHeight_[numLevels_] = height;
      levelBase_[numLevels_] = totalNodes_;
      numLevels_++;
      totalNodes_ += nodesInLevel;
      width = (uint16_t)((width + 1) >> 1);
      height = (uint16_t)((height + 1) >> 1);
    } while(nodesInLevel > 1);
    if(totalNodes_ <= pageSize)
      pool_.assign(totalNodes_, unusedNode());
    else
      pageOffset_.assign((totalNodes_ + pageSize - 1) >> pageShift, unallocatedPage);
  }

  uint16_t leavesWidth_;
  uint16_t leavesHeight_;
  uint8_t numLevels_;
  uint32_t totalNodes_;
  std::array<uint16_t, maxLevels> levelWidth_;
  std::array<uint16_t, maxLevels> levelHeight_;
  std::array<uint32_t, maxLevels> levelBase_;
  std::pmr::vector<uint32_t> pageOffset_;
  std::pmr::vector<Node> pool_;
};

using TagTreeU8 = TagTree<uint8_t>;
using TagTreeU16 = TagTree<uint16_t>;

} // namespace grk
