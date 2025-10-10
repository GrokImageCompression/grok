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

#include <limits>
#include <stdexcept>
#include <iostream>

namespace grk
{

/**
 Tag node
 */
template<typename T>
struct TagTreeNode
{
  TagTreeNode() : parent(nullptr), value(0), low(0), known(false) {}

  TagTreeNode* parent;
  T value;
  T low;
  bool known;
};

/**
 Tag tree
 */
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
      : leavesWidth_(leavesWidth), leavesHeight_(leavesHeight), nodeCount(0), nodes(nullptr)
  {
    uint16_t resLeavesWidth[16];
    uint16_t resLeavesHeight[16];
    int8_t numLevels = 0;
    resLeavesWidth[0] = leavesWidth_;
    resLeavesHeight[0] = leavesHeight_;
    nodeCount = 0;
    uint32_t nodesPerLevel;

    do
    {
      if(numLevels == 16)
      {
        grklog.error("TagTree constructor: num level overflow");
        throw std::runtime_error("TagTree constructor: num level overflow");
      }
      nodesPerLevel = static_cast<uint32_t>(resLeavesWidth[numLevels]) * resLeavesHeight[numLevels];
      resLeavesWidth[numLevels + 1] = (uint16_t)((resLeavesWidth[numLevels] + 1) >> 1);
      resLeavesHeight[numLevels + 1] = (uint16_t)((resLeavesHeight[numLevels] + 1) >> 1);
      nodeCount += nodesPerLevel;
      ++numLevels;
    } while(nodesPerLevel > 1);

    if(nodeCount == 0)
    {
      grklog.warn("tgt_create numnodes == 0, no tree created.");
      throw std::runtime_error("tgt_create numnodes == 0, no tree created");
    }

    nodes = new TagTreeNode<T>[nodeCount];
    auto currentNode = nodes;
    auto parentNode = nodes + static_cast<uint32_t>(leavesWidth_) * leavesHeight_;
    auto parentNodeNext = parentNode;

    for(int8_t i = 0; i < numLevels - 1; ++i)
    {
      for(uint16_t j = 0U; j < resLeavesHeight[i]; ++j)
      {
        int64_t k = resLeavesWidth[i];
        while(--k >= 0)
        {
          currentNode->parent = parentNode;
          ++currentNode;
          if(--k >= 0)
          {
            currentNode->parent = parentNode;
            ++currentNode;
          }
          ++parentNode;
        }
        if((j & 1) || j == resLeavesHeight[i] - 1)
        {
          parentNodeNext = parentNode;
        }
        else
        {
          parentNode = parentNodeNext;
          parentNodeNext += resLeavesWidth[i];
        }
      }
    }
    currentNode->parent = nullptr;
    reset();
  }

  ~TagTree()
  {
    delete[] nodes;
  }

  constexpr T getUninitializedValue(void)
  {
    return (std::numeric_limits<T>::max)();
  }

  /**
   Reset a tag tree (set all leaves to 0)
   */
  void reset()
  {
    for(auto i = 0U; i < nodeCount; ++i)
    {
      auto current_node = nodes + i;
      current_node->value = getUninitializedValue();
      current_node->low = 0;
      current_node->known = false;
    }
  }

  /**
   Set the value of a leaf of a tag tree
   @param leafno leaf to modify
   @param value  new value of leaf
   */
  void set(uint64_t leafno, T value)
  {
    auto node = nodes + leafno;
    while(node && node->value > value)
    {
      node->value = value;
      node = node->parent;
    }
  }

  /**
   Encode the value of a leaf of the tag tree up to a given threshold
   @param bio BIO handle
   @param leafno leaf to compress
   @param threshold Threshold to use when encoding value of the leaf
   @return true if successful, otherwise false
   */
  bool encode(BitIO* bio, uint64_t leafno, T threshold)
  {
    TagTreeNode<T>* nodeStack[15];
    auto nodeStackPtr = nodeStack;
    auto node = nodes + leafno;
    while(node->parent)
    {
      *nodeStackPtr++ = node;
      node = node->parent;
    }
    T low = 0;
    while(true)
    {
      if(node->low < low)
        node->low = low;
      else
        low = node->low;

      while(low < threshold)
      {
        if(low >= node->value)
        {
          if(!node->known)
          {
            if(!bio->write(1))
              return false;
            node->known = true;
          }
          break;
        }
        if(!bio->write(0))
          return false;
        ++low;
      }
      node->low = low;
      if(nodeStackPtr == nodeStack)
        break;
      node = *--nodeStackPtr;
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
  void decode(BitIO* bio, uint64_t leafno, T threshold, T* value)
  {
    TagTreeNode<T>* nodeStack[15];
    *value = getUninitializedValue();
    auto nodeStackPtr = nodeStack;
    auto node = nodes + leafno;
    // climb to top of tree
    while(node->parent)
    {
      *nodeStackPtr++ = node;
      node = node->parent;
    }
    // descend to bottom of tree
    T low = 0;
    while(true)
    {
      if(node->low < low)
        node->low = low;
      else
        low = node->low;
      while(low < threshold && low < node->value)
      {
        if(bio->read())
        {
          node->value = low;
          break;
        }
        low++;
      }
      node->low = low;
      if(nodeStackPtr == nodeStack)
        break;
      node = *--nodeStackPtr;
    }
    *value = node->value;
  }

private:
  uint16_t leavesWidth_;
  uint16_t leavesHeight_;
  uint64_t nodeCount;
  TagTreeNode<T>* nodes;
};

using TagTreeU8 = TagTree<uint8_t>;
using TagTreeU16 = TagTree<uint16_t>;

} // namespace grk
