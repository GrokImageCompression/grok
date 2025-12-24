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
struct Precinct;

/**
 * @struct PrecinctImpl
 * @brief Stores tag trees and code blocks belonging to a @ref Precinct
 * A reference to the @ref Precinct is also stored.
 *
 */
struct PrecinctImpl
{
  /**
   * @brief Constructs a new PrecinctImpl
   *
   * @param prec @ref Precinct
   */
  explicit PrecinctImpl(Precinct* prec);

  /**
   * @brief Destroys the PrecinctImpl
   *
   */
  virtual ~PrecinctImpl(void);

  /**
   * @brief Gets non-nominal bounds for a code block
   *
   * @param cblkno code block index
   * @return Rect32_16 code block bounds
   */
  Rect32_16 getCodeBlockBounds(uint32_t cblkno);

  /**
   * @brief Initializes code block
   *
   * @tparam T CodeBlock type
   * @param block code block
   * @param cblkno index of code block in precinct
   */
  template<typename T>
  void initCodeBlock(T* block, uint32_t cblkno);

  /**
   * @brief Deletes tag trees
   *
   */
  void deleteTagTrees();

  /**
   * @brief Gets include tag tree
   *
   * @return TagTreeU16* @ref TagTreeU16 include tag tree
   */
  TagTreeU16* getIncludeTagTree(void);

  /**
   * @brief Gets MSB tag tree
   *
   * @return TagTreeU8* @ref TagTreeU8 MSB tag tree
   */
  TagTreeU8* getIMsbTagTree(void);

  /**
   * @brief Cache of @ref CodeblockCompress
   *
   */
  BlockCache<CodeblockCompress, PrecinctImpl>* enc_;

  /**
   * @brief Cache of @ref CodeblockDecompress
   *
   */
  BlockCache<CodeblockDecompress, PrecinctImpl>* dec_;

  /**
   * @brief code block grid
   *
   */
  Rect32_16 cblk_grid_;

  /**
   * @brief associated precinct
   *
   */
  Precinct* prec_;

protected:
  bool genCodeBlockGrid(void);

private:
  bool hasCodeBlocks(void);
  TagTreeU16* incltree_; /* inclusion tree */
  TagTreeU8* imsbtree_; /* IMSB tree */
};

struct PrecinctImplCompress : public PrecinctImpl
{
  PrecinctImplCompress(Precinct* prec, uint16_t numLayers);
};

struct PrecinctImplDecompress : public PrecinctImpl
{
  PrecinctImplDecompress(Precinct* prec, uint16_t numLayers);
};

} // namespace grk
