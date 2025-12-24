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

struct PrecinctImpl;

/**
 * @struct Precinct
 * @brief Lightweight precinct struct storing only bounds, some book-keeping variables
 * and a pointer to the @ref PrecinctImpl.
 *
 */
struct Precinct : public Rect32_16
{
  /**
   * @brief Constructs a new Precinct object
   *
   * @param numLayers number of layers
   * @param bounds precinct bounds
   * @param cblk_expn log2 of nominal code block dimensions
   */
  Precinct(uint16_t numLayers, const Rect32_16& bounds, Point8 cblk_expn);

  /**
   * @brief Destroys the Precinct object
   *
   */
  virtual ~Precinct();

  /**
   * @brief Deletes tag trees
   *
   */
  void deleteTagTrees();

  /**
   * @brief Gets code block bounds
   *
   * @param cblkno code block index in precinct
   * @return Rect32_16 bounds
   */
  Rect32_16 getCodeBlockBounds(uint32_t cblkno);

  /**
   * @brief Gets inclusion @ref TagTreeU16
   *
   * @return TagTreeU16* @ref TagTreeU16
   */
  TagTreeU16* getInclTree(void);

  /**
   * @brief Get msb @ref TagTreeU8
   *
   * @return TagTreeU8* @ref TagTreeU8
   */
  TagTreeU8* getImsbTree(void);

  /**
   * @brief Gets code block grid width
   *
   * @return uint16_t grid width
   */
  uint16_t getCblkGridwidth(void);

  /**
   * @brief Gets code block grid height
   *
   * @return uint16_t grid height
   */
  uint16_t getCblkGridHeight(void);

  /**
   * @brief Gets nominal size (area) of code block
   *
   * @return uint16_t nominal size
   */
  uint16_t getNominalBlockSize(void);

  /**
   * @brief Gets the number of code blocks in the precinct
   *
   * @return uint32_t number of code blocks
   */
  uint32_t getNumCblks(void);

  /**
   * @brief Gets @ref CodeblockCompress for a code block
   * A @ref CodeblockCompress will be created if it hasn't
   * already been created yet.
   *
   * @param cblkno code block index
   * @return CodeblockCompress* @ref CodeblockCompress
   */
  CodeblockCompress* getCompressedBlock(uint32_t cblkno);

  /**
   * @brief Gets @ref CodeblockDecompress for a code block
   * A @ref CodeBlockDecompress will be created if it hasn't been
   * created yet.
   *
   * @param cblkno code block index
   * @return CodeblockDecompress* @ref CodeblockDecompress
   */
  CodeblockDecompress* getDecompressedBlock(uint32_t cblkno);

  /**
   * @brief Gets a @ref CodeblockDecompress if it exists, otherwise
   * returns nullptr
   *
   * @param cblkno code block index
   * @return CodeblockDecompress*
   */
  CodeblockDecompress* tryGetDecompressedBlock(uint32_t cblkno);

  /**
   * @brief Gets log2 of nominal code block dimensions
   *
   * @return Point8 log2 of block dimensions
   */
  Point8 getCblkExpn(void);

  /**
   * @brief Gets the code block grid
   *
   * @return Rect32_16
   */
  Rect32_16 getCblkGrid(void);

protected:
  uint16_t numLayers_;

private:
  PrecinctImpl* impl_ = nullptr;
  PrecinctImpl* getImpl(void);
  virtual PrecinctImpl* makeImpl(void) = 0;
  Point8 cblk_expn_;
};

struct PrecinctCompress : public Precinct
{
  using Precinct::Precinct;

private:
  PrecinctImpl* makeImpl(void);
};

struct PrecinctDecompress : public Precinct
{
  using Precinct::Precinct;

private:
  PrecinctImpl* makeImpl(void);
};

} // namespace grk
