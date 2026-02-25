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
#include <vector>
#include <atomic>
#include <functional>

namespace grk
{

struct TilePartInfo
{
  uint32_t tilePartLength_ = 0;
  uint8_t tilePart_ = 0;
  uint64_t remainingTilePartBytes_ = 0;
};

class Mct;

/**
 * @struct ITileProcessor
 * @brief Interface for managing tile compression/decompression
 */
struct ITileProcessor
{
  /**
   * @brief Destroys the TileProcessor
   */
  virtual ~ITileProcessor() = default;

  /**
   * @brief Sets the marker parser for processing
   * @param parser The marker parser to set
   */
  virtual void setProcessors(MarkerParser* parser) = 0;

  /**
   * @brief Emplaces block decoding task
   * @param task The task to emplace
   */
  virtual void emplaceBlockTask(tf::Task& t) = 0;

  /**
   * @brief Initializes the TileProcessor
   * @return true if initialization succeeds, false otherwise
   */
  virtual bool init(void) = 0;

  /**
   * @brief Sets the stream for input/output operations
   * @param stream The stream to set
   * @param ownsStream True if the processor owns the stream and should manage its lifetime
   */
  virtual void setStream(IStream* stream, bool ownsStream) = 0;

  /**
   * @brief Decompresses the tile using Tile Length Markers (TLM)
   * @param tilePartFetchSeq Sequence for fetching tile parts
   * @param streamPool Pool of coders for streaming
   * @param unreducedImageBounds Bounds of the unreduced image
   * @param post Post-decompression callback function
   * @param futures Manager for tile futures
   * @return true if decompression succeeds, false otherwise
   */
  virtual bool decompressWithTLM(const std::shared_ptr<TPFetchSeq>& tilePartFetchSeq,
                                 CoderPool* streamPool, Rect32 unreducedImageBounds,
                                 std::function<void()> post, TileFutureManager& futures) = 0;

  /**
   * @brief Prepares for decompression using Tile Length Markers (TLM)
   * @param tilePartFetchSeq Sequence for fetching tile parts
   * @return true if preparation succeeds, false otherwise
   */
  virtual bool decompressPrepareWithTLM(const std::shared_ptr<TPFetchSeq>& tilePartFetchSeq) = 0;

  /**
   * @brief Performs post-T1 processing
   * @return true if post-processing succeeds, false otherwise
   */
  virtual bool doPostT1(void) = 0;

  /**
   * @brief Prepares the processor for decompression
   */
  virtual void prepareForDecompression(void) = 0;

  /**
   * @brief Parses a tile part
   * @param parsers Vector of unique marker parsers
   * @param bifurcatedStream Bifurcated stream for reading
   * @param mainMarkerId ID of the main marker
   * @param tilePartInfo Information about the tile part
   * @return true if parsing succeeds, false otherwise
   */
  virtual bool parseTilePart(std::vector<std::unique_ptr<MarkerParser>>* parsers,
                             IStream* bifurcatedStream, uint16_t mainMarkerId,
                             TilePartInfo tilePartInfo) = 0;

  /**
   * @brief Reads the Start of Tile (SOT) marker
   * @param stream Input stream
   * @param headerData Header data buffer
   * @param headerSize Size of the header data
   * @param tilePartInfo Reference to tile part information (updated on success)
   * @param needToReadIndexAndLength True if index and length need to be read
   * @return true if reading succeeds, false otherwise
   */
  virtual bool readSOT(IStream* stream, uint8_t* headerData, uint16_t headerSize,
                       TilePartInfo& tilePartInfo, bool needToReadIndexAndLength) = 0;

  /**
   * @brief Schedules T2/T1 decompression tasks
   * @param coderPool Pool of coders
   * @param unreducedImageBounds Bounds of the unreduced image
   * @param post Post-scheduling callback function
   * @param futures Manager for tile futures
   * @return true if scheduling succeeds, false otherwise
   */
  virtual bool scheduleT2T1(CoderPool* coderPool, Rect32 unreducedImageBounds,
                            std::function<void()> post, TileFutureManager& futures) = 0;

  /**
   * @brief Performs post-T2+T1 decompression processing
   * @param scratch Scratch image for processing
   */
  virtual void post_decompressT2T1(GrkImage* scratch) = 0;

  /**
   * @brief Updates the differential decompression state
   * @param unreducedImageBounds Bounds of the unreduced image
   * @return true if update succeeds, false otherwise
   */
  virtual bool differentialUpdate(Rect32 unreducedImageBounds) = 0;

  /**
   * @brief Gets the associated GrkImage for the tile
   * @return Pointer to the GrkImage
   */
  virtual GrkImage* getImage(void) = 0;

  /**
   * @brief Sets the associated GrkImage for the tile
   * @param img Pointer to the GrkImage to set
   */
  virtual void setImage(GrkImage* img) = 0;

  /**
   * @brief Gets the unreduced tile window
   * @return Rect32 representing the unreduced tile window
   */
  virtual Rect32 getUnreducedTileWindow(void) = 0;

  /**
   * @brief Gets the Tile Coding Parameters (TCP)
   * @return Pointer to TileCodingParams
   */
  virtual TileCodingParams* getTCP(void) = 0;

  /**
   * @brief Gets the maximum number of decompress resolutions
   * @return Maximum number of resolutions
   */
  virtual uint8_t getMaxNumDecompressResolutions(void) = 0;

  /**
   * @brief Gets the associated stream
   * @return Pointer to IStream
   */
  virtual IStream* getStream(void) = 0;

  /**
   * @brief Gets the tile index
   * @return The tile index
   */
  virtual uint16_t getIndex(void) const = 0;

  /**
   * @brief Increments the tile index
   */
  virtual void incrementIndex(void) = 0;

  /**
   * @brief Gets the associated Tile
   * @return Pointer to Tile
   */
  virtual Tile* getTile(void) = 0;

  /**
   * @brief Gets the progression state
   * @return The progression state
   */
  virtual grk_progression_state getProgressionState() = 0;

  /**
   * @brief Gets the codec scheduler
   * @return Pointer to CodecScheduler
   */
  virtual CodecScheduler* getScheduler(void) = 0;

  /**
   * @brief Checks if the processor is in compressor mode
   * @return true if compressor, false otherwise
   */
  virtual bool isCompressor(void) = 0;

  /**
   * @brief Gets the number of processed packets
   * @return Number of processed packets
   */
  virtual uint64_t getNumProcessedPackets(void) = 0;

  /**
   * @brief Increments the number of processed packets by 1
   */
  virtual void incNumProcessedPackets(void) = 0;

  /**
   * @brief Increments the number of processed packets by a specified amount
   * @param numPackets Number of packets to add
   */
  virtual void incNumProcessedPackets(uint64_t numPackets) = 0;

  /**
   * @brief Increments the number of read data packets by 1
   */
  virtual void incNumReadDataPackets(void) = 0;

  /**
   * @brief Gets the tile cache strategy
   * @return The tile cache strategy value
   */
  virtual uint32_t getTileCacheStrategy(void) = 0;

  /**
   * @brief Gets the current plugin tile
   * @return Pointer to the current grk_plugin_tile
   */
  virtual grk_plugin_tile* getCurrentPluginTile(void) const = 0;

  /**
   * @brief Sets the current plugin tile
   * @param tile Pointer to the grk_plugin_tile to set
   */
  virtual void setCurrentPluginTile(grk_plugin_tile* tile) = 0;

  /**
   * @brief Gets the coding parameters
   * @return Pointer to CodingParams
   */
  virtual CodingParams* getCodingParams(void) = 0;

  /**
   * @brief Gets the header image
   * @return Pointer to GrkImage
   */
  virtual GrkImage* getHeaderImage(void) = 0;

  /**
   * @brief Gets the packet length cache
   * @return Shared pointer to PacketLengthCache<uint32_t>
   */
  virtual std::shared_ptr<PacketLengthCache<uint32_t>> getPacketLengthCache(void) = 0;

  /**
   * @brief Checks if MCT decompression is needed for a specific component
   * @param compno Component number
   * @return true if needed, false otherwise
   */
  virtual bool needsMctDecompress(uint16_t compno) = 0;

  /**
   * @brief Checks if MCT decompression is needed overall
   * @return true if needed, false otherwise
   */
  virtual bool needsMctDecompress(void) = 0;

  /**
   * @brief Gets the MCT (Multi-Component Transform) object
   * @return Pointer to Mct
   */
  virtual Mct* getMCT(void) = 0;

  /**
   * @brief Releases all resources (image and tile)
   */
  virtual void release(void) = 0;

  /**
   * @brief Releases select resources based on strategy
   * @param strategy Tile cache strategy
   */
  virtual void release(uint32_t strategy) = 0;

  /**
   * @brief Reads a PLT marker (Packet length, tile-part header)
   * @param headerData Header data buffer
   * @param headerSize Size of the header data
   * @return true if reading succeeds, false otherwise
   */
  virtual bool readPLT(uint8_t* headerData, uint16_t headerSize) = 0;

  /**
   * @brief Checks if all SOT markers for the tile are parsed
   * @return true if all parsed (or tile is truncated), false otherwise
   */
  virtual bool allSOTMarkersParsed() = 0;

  /**
   * @brief Sets the processor to truncated state if not all tile parts are parsed
   */
  virtual void setTruncated(void) = 0;

  /**
   * @brief Checks if an error has occurred
   * @return true if error, false otherwise
   */
  virtual bool hasError(void) = 0;

  /**
   * @brief Checks if the processor is initialized
   * @return true if initialized, false otherwise
   */
  virtual bool isInitialized(void) = 0;
};

} // namespace grk