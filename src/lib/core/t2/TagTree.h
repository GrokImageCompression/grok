/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#pragma once

#include <limits>

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
	Create a tag tree
	@param leavesWidth_ Width of the array of leaves of the tree
	@param leavesHeight_ Height of the array of leaves of the tree
	@return a new tag tree if successful, returns nullptr otherwise
	*/
   TagTree(uint32_t leavesWidth, uint32_t leavesHeight)
	   : leavesWidth_(leavesWidth), leavesHeight_(leavesHeight), nodeCount(0), nodes(nullptr)
   {
	  uint32_t resLeavesWidth[32];
	  uint32_t resLeavesHeight[32];
	  int8_t numLevels = 0;
	  resLeavesWidth[0] = leavesWidth_;
	  resLeavesHeight[0] = leavesHeight_;
	  nodeCount = 0;
	  uint64_t nodesPerLevel;
	  do
	  {
		 if(numLevels == 32)
		 {
			Logger::logger_.error("TagTree constructor: num level overflow");
			throw std::exception();
		 }
		 nodesPerLevel = (uint64_t)resLeavesWidth[numLevels] * resLeavesHeight[numLevels];
		 resLeavesWidth[numLevels + 1] = (uint32_t)(((uint64_t)resLeavesWidth[numLevels] + 1) >> 1);
		 resLeavesHeight[numLevels + 1] =
			 (uint32_t)(((uint64_t)resLeavesHeight[numLevels] + 1) >> 1);
		 nodeCount += nodesPerLevel;
		 ++numLevels;
	  } while(nodesPerLevel > 1);

	  if(nodeCount == 0)
	  {
		 Logger::logger_.warn("tgt_create numnodes == 0, no tree created.");
		 throw std::runtime_error("tgt_create numnodes == 0, no tree created");
	  }

	  nodes = new TagTreeNode<T>[nodeCount];
	  auto currentNode = nodes;
	  auto parentNode = nodes + (uint64_t)leavesWidth_ * leavesHeight_;
	  auto parentNodeNext = parentNode;

	  for(int8_t i = 0; i < numLevels - 1; ++i)
	  {
		 for(uint32_t j = 0; j < resLeavesHeight[i]; ++j)
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
	  for(uint64_t i = 0; i < nodeCount; ++i)
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
   void setvalue(uint64_t leafno, T value)
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
	@param threshold Threshold to use when compressing value of the leaf
	@return true if successful, otherwise false
	*/
   bool compress(BitIO* bio, uint64_t leafno, T threshold)
   {
	  TagTreeNode<T>* nodeStack[31];
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
	Decompress the value of a leaf of the tag tree up to a given threshold
	@param bio Pointer to a BIO handle
	@param leafno Number that identifies the leaf to decompress
	@param threshold Threshold to use when decoding value of the leaf
	@param value the node's value
	*/
   void decodeValue(BitIO* bio, uint64_t leafno, T threshold, T* value)
   {
	  TagTreeNode<T>* nodeStack[31];
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
   uint32_t leavesWidth_;
   uint32_t leavesHeight_;
   uint64_t nodeCount;
   TagTreeNode<T>* nodes;
};

typedef TagTree<uint8_t> TagTreeU8;
typedef TagTree<uint16_t> TagTreeU16;

} // namespace grk
