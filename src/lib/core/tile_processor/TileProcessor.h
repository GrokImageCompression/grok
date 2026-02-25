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

#include <mutex>
#include <memory>

#include "PacketTracker.h"
#include "ITileProcessor.h"

namespace grk
{

/**
 * @struct TileProcesor
 * @brief Manages tile compression/decompression
 */
struct TileProcessor : virtual public ITileProcessor
{
  /**
   * @brief Constructs a TileProcessor
   * @param index tile index
   * @param tcp @ref TileCodingParams
   * @param codeStream @ref CodeStream
   * @param stream @ref IStream
   * @param isCompressor true if the tile will be compressed
   * @param tileCacheStrategy tile cache strategy
   */
  TileProcessor(uint16_t index, TileCodingParams* tcp, CodeStream* codeStream, IStream* stream,
                bool isCompressor, uint32_t tileCacheStrategy);

  /**
   * @brief Destroys a TileProcessor
   */
  virtual ~TileProcessor();

  void setProcessors(MarkerParser* parser);

  void emplaceBlockTask(tf::Task& t);

  /**
   * @brief Initializes a TileProcessor
   */
  virtual bool init(void);

  void setStream(IStream* stream, bool ownsStream);

  bool decompressWithTLM(const std::shared_ptr<TPFetchSeq>& tilePartFetchSeq, CoderPool* streamPool,
                         Rect32 unreducedImageBounds, std::function<void()> post,
                         TileFutureManager& futures);

  bool decompressPrepareWithTLM(const std::shared_ptr<TPFetchSeq>& tilePartFetchSeq);

  /**
   * @brief Performs post T1 processing
   * @return true if successful
   */
  bool doPostT1(void);

  /**
   * @brief Prepares for decompresion
   *
   * If this fails, then TileProcessor doesn't get initialized.
   *
   */
  void prepareForDecompression(void);

  /**
   * @brief Parses tile part
   *
   * @return true if successful
   */
  bool parseTilePart(std::vector<std::unique_ptr<MarkerParser>>* parsers, IStream* bifurcatedStream,
                     uint16_t mainMarkerId, TilePartInfo tilePartInfo);

  /**
   * @brief Reads SOT marker
   *
   * @param stream
   * @param headerData
   * @param headerSize
   * @param tilePartLength
   * @return true
   */
  bool readSOT(IStream* stream, uint8_t* headerData, uint16_t headerSize,
               TilePartInfo& tilePartInfo, bool needToReadIndexAndLength);

  /**
   * @brief Schedule T2/T1 decompression
   *
   * @param coderPool pool of coders
   * @param unreducedImageBounds
   * @param post
   * @param futures
   * @return true if successful
   */
  bool scheduleT2T1(CoderPool* coderPool, Rect32 unreducedImageBounds, std::function<void()> post,
                    TileFutureManager& futures);
  /**
   * @brief Performs post T2+T1 processing
   *
   * @param scratch
   */
  void post_decompressT2T1(GrkImage* scratch);

  /**
   * @brief Updates differential decompress state
   *
   * @param unreducedImageBounds
   * @return true
   * @return false
   */
  bool differentialUpdate(Rect32 unreducedImageBounds);

  /**
   * @brief Gets the tile @ref GrkImage
   *
   * @return GrkImage*
   */
  GrkImage* getImage(void);

  /**
   * @brief Gets the tile @ref GrkImage
   *
   * @param img @ref GrkImage
   */
  void setImage(GrkImage* img);

  /**
   * @brief Get the Unreduced Tile Window object
   *
   * @return Rect32
   */
  Rect32 getUnreducedTileWindow(void);

  /**
   * @brief
   *
   * @return TileCodingParams*
   */
  TileCodingParams* getTCP(void);

  /**
   * @brief Get the Max Num Decompress Resolutions object
   *
   * @return uint8_t
   */
  uint8_t getMaxNumDecompressResolutions(void);

  /**
   * @brief Get the Stream object
   *
   * @return IStream*
   */
  IStream* getStream(void);

  /**
   * @brief Get the Index object
   *
   * @return uint16_t
   */
  uint16_t getIndex(void) const;

  /**
   * @brief
   *
   */
  void incrementIndex(void);

  /**
   * @brief Get the Tile object
   *
   * @return Tile*
   */
  Tile* getTile(void);

  grk_progression_state getProgressionState();

  /**
   * @brief Get the Scheduler object
   *
   * @return CodecScheduler*
   */
  CodecScheduler* getScheduler(void);

  /**
   * @brief
   *
   * @return true
   * @return false
   */
  bool isCompressor(void);

  /**
   * @brief Get the Num Processed Packets object
   *
   * @return uint64_t
   */
  uint64_t getNumProcessedPackets(void);

  /**
   * @brief
   *
   */
  void incNumProcessedPackets(void);

  /**
   * @brief
   *
   * @param numPackets
   */
  void incNumProcessedPackets(uint64_t numPackets);

  /**
   * @brief
   *
   */
  void incNumReadDataPackets(void);

  /**
   * @brief Gets the Tile Cache Strategy object
   *
   * @return uint32_t
   */
  uint32_t getTileCacheStrategy(void);

  /**
   * @brief Gets the Current Plugin Tile object
   *
   * @return grk_plugin_tile*
   */
  grk_plugin_tile* getCurrentPluginTile(void) const;

  /**
   * @brief Set the Current Plugin Tile object
   *
   * @param tile
   */
  void setCurrentPluginTile(grk_plugin_tile* tile);

  /**
   * @brief Get the Coding Params object
   *
   * @return CodingParams*
   */
  CodingParams* getCodingParams(void);

  /**
   * @brief Get the Header Image object
   *
   * @return GrkImage*
   */
  GrkImage* getHeaderImage(void);

  /**
   * @brief Get the Packet Length Cache object
   *
   * @return std::shared_ptr<PacketLengthCache<uint32_t>>
   */
  std::shared_ptr<PacketLengthCache<uint32_t>> getPacketLengthCache(void);

  /**
   * @brief
   *
   * @param compno
   * @return true
   * @return false
   */
  bool needsMctDecompress(uint16_t compno);

  /**
   * @brief
   *
   * @return true
   * @return false
   */
  bool needsMctDecompress(void);

  /**
   * @brief gets @ref Mct
   *
   * @return Mct*
   */
  Mct* getMCT(void);

  /**
   * @brief Releases resources - image and tile
   *
   */
  void release(void);

  /**
   * @brief release select resources
   *
   * @param strategy tile cache strategy
   */
  void release(uint32_t strategy);

  /**
   * @brief Reads a PLT marker (Packet length, tile-part header)
   * @param headerData header data
   * @param headerSize size of header data
   * @return true if successful
   */
  bool readPLT(uint8_t* headerData, uint16_t headerSize);

  /**
   * @brief Checks if tile is completely parsed.
   * If the tile is truncated, then it is considered
   * completely parsed.
   *
   * @return true if completely parsed, otherwise false
   */
  bool allSOTMarkersParsed();

  /**
   * @brief Sets processor to truncated if not all tile parts have
   * been parsed
   *
   */
  void setTruncated(void);

  bool hasError(void);

  bool isInitialized(void);

protected:
  /**
   * @brief header @ref GrkImage
   *
   */
  GrkImage* headerImage_ = nullptr;

  /**
   * @brief @ref grk_plugin_tile
   *
   */
  grk_plugin_tile* current_plugin_tile_ = nullptr;

  /**
   * @brief @ref CodingParams
   *
   */
  CodingParams* cp_ = nullptr;

  /**
   * @brief @ref PacketLengthCache
   *
   */
  std::shared_ptr<PacketLengthCache<uint32_t>> packetLengthCache_;

  /**
   * @brief @ref Tile
   *
   */
  Tile* tile_ = nullptr;

  /** index of tile being currently compressed/decompressed */
  uint16_t tileIndex_ = 0;

  /**
   * @brief @ref TileCodingParams
   *
   */
  TileCodingParams* tcp_ = nullptr;

  /**
   * @brief @ref IStream
   *
   */
  IStream* stream_ = nullptr;

  /**
   * @brief @ref MCT
   *
   */
  Mct* mct_ = nullptr;

  /**
   * @brief @ref CodecScheduler
   *
   */
  CodecScheduler* scheduler_ = nullptr;

private:
  std::vector<tf::Task> blockTasks_;

  bool initialized_ = false;

  void prepareConcurrentParsing(void);

  std::atomic<bool> success_ = true;

  /**
   * @brief root @ref FlowComponent
   *
   */
  FlowComponent* rootFlow_ = nullptr;

  std::unique_ptr<FlowComponent> tileHeaderParseFlow_;
  std::unique_ptr<FlowComponent> prepareFlow_;
  std::unique_ptr<FlowComponent> t2ParseFlow_;
  std::unique_ptr<FlowComponent> allocAndScheduleFlow_;

  /**
   * @brief post decompression @ref FlowComponent
   *
   */
  FlowComponent* postDecompressFlow_ = nullptr;

  /**
   * @brief deallocate buffers
   *
   */
  void deallocBuffers();

  /**
   * @brief Create a Tile Window Buffers object
   *
   * @return true if successful
   */
  bool createDecompressTileComponentWindows(void);

  /**
   * @brief Get the Num Read Data Packets object
   *
   * @return number of read data packets
   */
  uint64_t getNumReadDataPackets(void);

  /**
   * @brief @ref MarkerParser
   *
   */
  MarkerParser* markerParser_ = nullptr;

  /**
   * @brief number of packets processed
   *
   */
  uint64_t numProcessedPackets_ = 0;

  /**
   * @brief number of data packets read
   *
   */
  std::atomic<uint64_t> numReadDataPackets_ = 0;

  TilePartInfo tilePartInfo_;
  uint64_t startPos_ = 0;

  /** number of SOT markers parsed */
  uint8_t numSOTsParsed_ = 0;

  /**
   * @brief true if one of this tile's tile parts is truncated
   *
   */
  bool truncated_ = false;

  /**
   * @brief @ref GrkImage for this tile
   *
   */
  GrkImage* image_ = nullptr;

  /**
   * @brief true if tile will be compressed
   *
   */
  bool isCompressor_;

  /**
   * @brief unreduced image window
   *
   */
  Rect32 unreducedImageWindow_;

  /**
   * @brief tile cache strategy
   *
   */
  uint32_t tileCacheStrategy_ = 0;

  void decompress_synch_plugin_with_host(void);

  std::shared_ptr<TPFetchSeq> tilePartFetchSeq_;
  TPSeq tilePartSeq_;

  std::vector<uint8_t> threadTilePart_;
  std::mutex pltMutex_;
};

} // namespace grk
