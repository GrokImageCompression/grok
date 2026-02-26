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

#include "ITileProcessor.h"
#include "PacketTracker.h" // For PacketTracker forward declaration

namespace grk
{

/**
 * @struct ITileProcessorCompress
 * @brief Interface for managing tile compression
 */
struct ITileProcessorCompress : virtual public ITileProcessor
{
  /**
   * @brief Destroys the TileProcessorCompress
   */
  virtual ~ITileProcessorCompress() = default;

  /**
   * @brief Gets the packet tracker
   * @return Pointer to the PacketTracker
   */
  virtual PacketTracker* getPacketTracker(void) = 0;

  /**
   * @brief Pre-compresses the tile (compression-only preparation)
   * @param thread_id ID of the thread performing the pre-compression
   * @return true if pre-compression succeeds, false otherwise
   */
  virtual bool preCompressTile(size_t thread_id) = 0;

  /**
   * @brief Checks whether a POC marker can be written for this tile
   * @return true if POC marker is allowed, false otherwise
   */
  virtual bool canWritePocMarker(void) = 0;

  /**
   * @brief Writes the T2 part of the current tile part
   * @param tileBytesWritten [out] number of bytes written for this tile part
   * @return true if writing succeeds, false otherwise
   */
  virtual bool writeTilePartT2(uint32_t* tileBytesWritten) = 0;

  /**
   * @brief Performs the full tile compression (T1 + T2 + rate allocation)
   * @return true if compression succeeds, false otherwise
   */
  virtual bool doCompress(void) = 0;

  /**
   * @brief Ingests uncompressed image data into the tile
   * @param p_src pointer to source uncompressed data
   * @param src_length length of the source data in bytes
   * @return true if ingestion succeeds, false otherwise
   */
  virtual bool ingestUncompressedData(uint8_t* p_src, uint64_t src_length) = 0;

  /**
   * @brief Checks whether rate control is required for this tile
   * @return true if rate control is needed, false otherwise
   */
  virtual bool needsRateControl(void) = 0;

  /**
   * @brief Gets the pre-calculated tile length (for rate control optimization)
   * @return pre-calculated tile length in bytes
   */
  virtual uint32_t getPreCalculatedTileLen(void) = 0;

  /**
   * @brief Checks whether the tile length can be pre-calculated
   * @return true if pre-calculation is possible, false otherwise
   */
  virtual bool canPreCalculateTileLen(void) = 0;

  /**
   * @brief Sets whether this tile part is the first POC tile part
   * @param res true for first POC tile part, false otherwise
   */
  virtual void setFirstPocTilePart(bool res) = 0;

  /**
   * @brief Sets the current progression iterator number
   * @param num new progression iterator value
   */
  virtual void setProgIterNum(uint32_t num) = 0;

  /**
   * @brief Gets the current tile-part counter
   * @return current tile-part counter value
   */
  virtual uint8_t getTilePartCounter(void) const = 0;

  /**
   * @brief Increments the tile-part counter
   */
  virtual void incTilePartCounter(void) = 0;
};

} // namespace grk