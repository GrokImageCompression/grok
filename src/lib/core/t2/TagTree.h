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

#include <limits>
#include <stdexcept>
#include <iostream>
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

  TagTree(uint16_t leavesWidth, uint16_t leavesHeight)
      : leavesWidth_(leavesWidth), leavesHeight_(leavesHeight)
  {
    buildTree();
    reset();
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
    for(auto& n : nodes_)
    {
      n.value = getUninitializedValue();
      n.low = 0;
      n.known = false;
    }
    for(auto& v : leafCache_)
      v = getUninitializedValue();
  }

  /**
   Set the value of a leaf of a tag tree
   @param leafno leaf to modify
   @param value  new value of leaf
   */
  void set(uint64_t leafno, T value)
  {
    uint32_t node = static_cast<uint32_t>(leafno);
    while(node != UINT32_MAX && nodes_[node].value > value)
    {
      nodes_[node].value = value;
      node = parents_[node];
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
    // exact original encode logic, using flat indices
    uint32_t nodeStack[16];
    int stackPtr = 0;
    uint32_t node = static_cast<uint32_t>(leafno);
    while(parents_[node] != UINT32_MAX)
    {
      nodeStack[stackPtr++] = node;
      node = parents_[node];
    }
    T low = 0;
    while(true)
    {
      auto& n = nodes_[node];
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
      if(stackPtr == 0)
        break;
      node = nodeStack[--stackPtr];
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
  void decode(t1_t2::BitIO* bio, uint64_t leafno, T threshold, T* value)
  {
    if(leafCache_[leafno] < threshold) [[likely]]
    {
      *value = leafCache_[leafno];
      return;
    }

    *value = getUninitializedValue();

    uint32_t nodeStack[16];
    int stackPtr = 0;
    uint32_t node = static_cast<uint32_t>(leafno);

    // climb to root (exact same path as encode)
    while(parents_[node] != UINT32_MAX)
    {
      nodeStack[stackPtr++] = node;
      node = parents_[node];
    }

    T low = 0;
    while(true)
    {
      auto& n = nodes_[node];

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

      if(stackPtr == 0) [[unlikely]]
        break;

      node = nodeStack[--stackPtr]; // descend to child
    }

    *value = nodes_[node].value; // now guaranteed to be the leaf node
    if(*value < threshold)
      leafCache_[leafno] = *value;
  }

private:
  struct Node
  {
    T value;
    T low;
    bool known;
  };

  void buildTree()
  {
    // same level calculation as original
    uint16_t resW[16]{}, resH[16]{};
    int8_t levels = 0;
    resW[0] = leavesWidth_;
    resH[0] = leavesHeight_;
    uint64_t totalNodes = 0;
    uint32_t nodesPerLevel;

    do
    {
      nodesPerLevel = static_cast<uint32_t>(resW[levels]) * resH[levels];
      resW[levels + 1] = (uint16_t)((resW[levels] + 1) >> 1);
      resH[levels + 1] = (uint16_t)((resH[levels] + 1) >> 1);
      totalNodes += nodesPerLevel;
      ++levels;
    } while(nodesPerLevel > 1);

    nodes_.resize(totalNodes);
    parents_.resize(totalNodes, UINT32_MAX);
    leafCache_.resize(static_cast<uint64_t>(leavesWidth_) * leavesHeight_);

    // build parents (exact same linking logic as original, but with indices)
    uint64_t parentBase = static_cast<uint64_t>(leavesWidth_) * leavesHeight_;
    uint64_t cur = 0;

    for(int8_t lvl = 0; lvl < levels - 1; ++lvl)
    {
      uint32_t w = resW[lvl];
      uint32_t h = resH[lvl];
      for(uint32_t j = 0; j < h; ++j)
      {
        for(uint32_t k = 0; k < w; ++k)
        {
          parents_[cur] = static_cast<uint32_t>(parentBase + (j >> 1) * resW[lvl + 1] + (k >> 1));
          ++cur;
        }
      }
      parentBase += static_cast<uint64_t>(resW[lvl + 1]) * resH[lvl + 1];
    }
  }

  uint16_t leavesWidth_;
  uint16_t leavesHeight_;
  std::vector<Node> nodes_;
  std::vector<uint32_t> parents_; // UINT32_MAX = root
  std::vector<T> leafCache_;
};

using TagTreeU8 = TagTree<uint8_t>;
using TagTreeU16 = TagTree<uint16_t>;

} // namespace grk