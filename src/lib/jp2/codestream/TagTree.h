/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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

namespace grk {

const uint32_t tag_tree_uninitialized_node_value = 999;

/**
 Tag node
 */
struct TagTreeNode {

	TagTreeNode() :
			parent(nullptr), value(0), low(0), known(0) {
	}

	TagTreeNode *parent;
	int64_t value;
	int64_t low;
	uint32_t known;
};

/**
 Tag tree
 */
class TagTree {

public:

	/**
	 Create a tag tree
	 @param numleafsh Width of the array of leafs of the tree
	 @param numleafsv Height of the array of leafs of the tree
	 @return a new tag tree if successful, returns nullptr otherwise
	 */
	TagTree(uint64_t numleafsh, uint64_t numleafsv);
	~TagTree();

	/**
	 * Reinitialises a tag tree
	 *
	 * @param	num_leafs_h		the width of the array of leafs of the tree
	 * @param	num_leafs_v		the height of the array of leafs of the tree
	 * @return	true if successful, false otherwise
	 */
	bool init(uint64_t num_leafs_h, uint64_t num_leafs_v);

	/**
	 Reset a tag tree (set all leaves to 0)
	 */
	void reset();
	/**
	 Set the value of a leaf of a tag tree
	 @param leafno Number that identifies the leaf to modify
	 @param value New value of the leaf
	 */
	void setvalue(uint64_t leafno, int64_t value);
	/**
	 Encode the value of a leaf of the tag tree up to a given threshold
	 @param bio Pointer to a BIO handle
	 @param leafno Number that identifies the leaf to compress
	 @param threshold Threshold to use when encoding value of the leaf
	 @return true if successful, otherwise false
	 */
	bool compress(BitIO *bio, uint64_t leafno, int64_t threshold);
	/**
	 Decode the value of a leaf of the tag tree up to a given threshold
	 @param bio Pointer to a BIO handle
	 @param leafno Number that identifies the leaf to decompress
	 @param threshold Threshold to use when decoding value of the leaf
	 @param decoded 1 if the node's value < threshold, 0 otherwise
	 */
	void decompress(BitIO *bio, uint64_t leafno, int64_t threshold,
			uint8_t *decoded);

	/**
	 Decode the value of a leaf of the tag tree up to a given threshold
	 @param bio Pointer to a BIO handle
	 @param leafno Number that identifies the leaf to decompress
	 @param threshold Threshold to use when decoding value of the leaf
	 @param value the node's value
	 */
	void decodeValue(BitIO *bio, uint64_t leafno, int64_t threshold,
			uint64_t *value);

private:

	uint64_t numleafsh;
	uint64_t numleafsv;
	uint64_t numnodes;
	TagTreeNode *nodes;
	uint64_t nodes_size; /* maximum size taken by nodes */

};

}

