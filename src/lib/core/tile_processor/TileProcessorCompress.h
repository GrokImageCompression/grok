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

#include "TileProcessor.h"
#include "ITileProcessorCompress.h"

namespace grk
{

/**
 Tile processor for compression
 */
/**
 * @struct TileProcesorCompress
 * @brief Manages tile compression
 */
struct TileProcessorCompress : public ITileProcessorCompress, public TileProcessor
{
  /**
   * @brief Constructs a TileProcessorCompress
   * @param index tile index
   * @param codeStream @ref CodeStream
   * @param stream @ref IStream
   */
  TileProcessorCompress(uint16_t index, TileCodingParams* tcp, CodeStream* codeStream,
                        IStream* stream);

  /**
   * @brief Destroys a TileProcessorCompress
   */
  ~TileProcessorCompress() override;

  bool init(void) override;

  PacketTracker* getPacketTracker(void) override;
  bool preCompressTile(size_t thread_id) override;
  bool canWritePocMarker(void) override;
  bool writeTilePartT2(uint32_t* tileBytesWritten) override;
  bool doCompress(void) override;
  bool ingestUncompressedData(uint8_t* p_src, uint64_t src_length) override;
  bool needsRateControl(void) override;
  uint32_t getPreCalculatedTileLen(void) override;
  bool canPreCalculateTileLen(void) override;
  void setFirstPocTilePart(bool res) override;
  void setProgIterNum(uint32_t num) override;
  uint8_t getTilePartCounter(void) const override;
  void incTilePartCounter(void) override;

private:
  void transferTileDataFromImage(void);
  void dcLevelShiftCompress();
  void scheduleCompressT1();
  bool compressT2(uint32_t* packet_bytes_written);
  bool rateAllocate(uint32_t* allPacketBytes, bool disableRateControl);
  bool layerNeedsRateControl(uint16_t layno);
  bool makeSingleLosslessLayer();
  void makeLayerFinal(uint16_t layno);
  bool pcrdBisectSimple(uint32_t* p_data_written, bool disableRateControl);
  void makeLayerSimple(uint16_t layno, double thresh, bool finalAttempt);
  bool pcrdBisectFeasible(uint32_t* p_data_written, bool disableRateControl);
  bool makeLayerFeasible(uint16_t layno, uint16_t thresh, bool finalAttempt);
  void prepareBlockForFirstLayer(t1::CodeblockCompress* cblk);

  uint32_t preCalculatedTileLen_ = 0;
  /** Compression Only
   *  true for first POC tile part, otherwise false*/
  bool first_poc_tile_part_ = true;
  /**
   *  index of tile part being currently coded.
   *  tilePartCounter_ holds the total number of tile parts encoded thus far
   *  while the compressor is compressing the current tile part.*/
  uint8_t tilePartCounter_ = 0;
  //*  Current progression iterator number */
  uint32_t prog_iter_num = 0;
  /** position of the tile part flag in progression order*/
  uint8_t newTilePartProgressionPosition_ = 0;
  // track which packets have already been written to the code stream
  PacketTracker* packetTracker_ = nullptr;
};

} // namespace grk
