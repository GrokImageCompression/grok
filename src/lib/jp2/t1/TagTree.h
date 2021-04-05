/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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
template<typename T> struct TagTreeNode
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
template<typename T> class TagTree
{
  public:
	/**
	 Create a tag tree
	 @param numleafsh Width of the array of leafs of the tree
	 @param numleafsv Height of the array of leafs of the tree
	 @return a new tag tree if successful, returns nullptr otherwise
	 */
	TagTree(uint32_t mynumleafsh, uint32_t mynumleafsv)
		: numleafsh(mynumleafsh), numleafsv(mynumleafsv), numnodes(0), nodes(nullptr)
	{
		uint32_t nplh[32];
		uint32_t nplv[32];
		int8_t numlvls = 0;
		nplh[0] = numleafsh;
		nplv[0] = numleafsv;
		numnodes = 0;
		uint64_t n;
		do
		{
			n = (uint64_t)nplh[numlvls] * nplv[numlvls];
			nplh[numlvls + 1] = (uint32_t)(((uint64_t)nplh[numlvls] + 1) / 2);
			nplv[numlvls + 1] = (uint32_t)(((uint64_t)nplv[numlvls] + 1) / 2);
			numnodes += n;
			++numlvls;
		} while(n > 1);

		if(numnodes == 0)
		{
			GRK_WARN("tgt_create numnodes == 0, no tree created.");
			throw std::runtime_error("tgt_create numnodes == 0, no tree created");
		}

		nodes = new TagTreeNode<T>[numnodes];
		auto node = nodes;
		auto parent_node = nodes + (uint64_t)numleafsh * numleafsv;
		auto parent_node0 = parent_node;

		for(int8_t i = 0; i < numlvls - 1; ++i)
		{
			for(uint32_t j = 0; j < nplv[i]; ++j)
			{
				int64_t k = nplh[i];
				while(--k >= 0)
				{
					node->parent = parent_node;
					++node;
					if(--k >= 0)
					{
						node->parent = parent_node;
						++node;
					}
					++parent_node;
				}
				if((j & 1) || j == nplv[i] - 1)
				{
					parent_node0 = parent_node;
				}
				else
				{
					parent_node = parent_node0;
					parent_node0 += nplh[i];
				}
			}
		}
		node->parent = 0;
		reset();
	}
	~TagTree()
	{
		delete[] nodes;
	}

	constexpr T getUninitializedValue(void){
		return (std::numeric_limits<T>::max)();
	}
	/**
	 Reset a tag tree (set all leaves to 0)
	 */
	void reset()
	{
		for(uint64_t i = 0; i < numnodes; ++i)
		{
			auto current_node = nodes + i;
			current_node->value = getUninitializedValue();
			current_node->low = 0;
			current_node->known = false;
		}
	}
	/**
	 Set the value of a leaf of a tag tree
	 @param leafno Number that identifies the leaf to modify
	 @param value New value of the leaf
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
	 @param bio Pointer to a BIO handle
	 @param leafno Number that identifies the leaf to compress
	 @param threshold Threshold to use when compressing value of the leaf
	 @return true if successful, otherwise false
	 */
	bool compress(BitIO* bio, uint64_t leafno, T threshold)
	{
		TagTreeNode<T>* stk[31];
		auto stkptr = stk;
		auto node = nodes + leafno;
		while(node->parent)
		{
			*stkptr++ = node;
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
						if(!bio->write(1, 1))
							return false;
						node->known = true;
					}
					break;
				}
				if(!bio->write(0, 1))
					return false;
				++low;
			}

			node->low = low;
			if(stkptr == stk)
				break;
			node = *--stkptr;
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
		TagTreeNode<T>* stk[31];
		*value = getUninitializedValue();
		auto stkptr = stk;
		auto node = nodes + leafno;
		while(node->parent)
		{
			*stkptr++ = node;
			node = node->parent;
		}
		T low = 0;
		while(true)
		{
			if(node->low <  low)
				node->low = low;
			else
				low = node->low;
			while(low < threshold && low < node->value)
			{
				uint32_t temp = 0;
				bio->read(&temp, 1);
				if(temp) {
					node->value = T(low);
					break;
				}
				else {
					++low;
				}
			}
			node->low = low;
			if(stkptr == stk)
				break;
			node = *--stkptr;
		}
		*value = node->value;
	}
  private:
	uint32_t numleafsh;
	uint32_t numleafsv;
	uint64_t numnodes;
	TagTreeNode<T>* nodes;
};

typedef TagTree<uint8_t> TagTreeU8;
typedef TagTree<uint16_t> TagTreeU16;


} // namespace grk
