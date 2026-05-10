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

#include "ConcurrentQueue.h"
#include "TFSingleton.h"
#include "CompressedChunkCache.h"
#include "SelectiveFetchRanges.h"
#include <map>

namespace grk
{

/**
 * @class CodeStreamDecompress
 * @brief Manages decompression
 *
 * @see doc/IncrementalStripeCompositing.md for the incremental band-write pipeline.
 * @see doc/TileCache.md for tile caching and LRU eviction.
 */
class CodeStreamDecompress final : public CodeStream, public IDecompressor
{
public:
  /**
   * @brief Constructs a CodeStreamDecompress
   * @param stream @ref CodeStreamDecompress
   */
  explicit CodeStreamDecompress(IStream* stream);

  /**
   * @brief Destroys a CodeStreamDecompress
   */
  ~CodeStreamDecompress()
  {
    if(decompressQueue_)
      decompressQueue_->close();
    if(decompressConsumer_.joinable())
      decompressConsumer_.join();
    if(decompressWorker_.joinable())
      decompressWorker_.join();
    TFSingleton::get().wait_for_all();
  }

  void init(grk_decompress_parameters* param) override;

  void setBandCallback(grk_io_band_callback callback, void* user_data) override;
  grk_io_band_callback getBandCallback() const override
  {
    return ioBandCallback_;
  }
  void* getBandUserData() const override
  {
    return ioBandUserData_;
  }

  grk_progression_state getProgressionState(uint16_t tile_index) override;

  bool setProgressionState(grk_progression_state state) override;

  /**
   * @brief Gets tile processor for specified tile index
   * @param tile_index tile index
   * @return @ref ITileProcessor
   */
  ITileProcessor* getTileProcessor(uint16_t tile_index);

  /**
   * @brief Initializes tile completeness set
   * @param region region to decompress
   */
  void initTilesToDecompress(Rect16 region);

  void setNumComponents(uint16_t numComps);

  /**
   * @brief Initializes default @ref TileCodingParams
   * @return true if initialization succeeded
   */
  bool initDefaultTCP();

  bool readHeader(grk_header_info* header_info) override;

  /**
   * @brief Checks if header needs to be read
   * @return true if header needs to be read
   */
  bool needsHeaderRead(void) const;

  GrkImage* getImage(uint16_t tile_index, bool wait) override;

  GrkImage* getImage(void) override;

  GrkImage* getCompositeNoWait(void);

  /**
   * @brief Gets header @ref GrkImage
   * (where main and tile header information is stored)
   * @return @ref GrkImage
   */
  GrkImage* getHeaderImage(void);

  bool decompress(grk_plugin_tile* tile) override;

  bool decompressTile(uint16_t tile_index) override;

  /**
   * @brief Post processes decompressed image
   *
   * @param img @ref GrkImage
   * @return true if successful
   */
  bool postProcess(GrkImage* img);

  void dump(uint32_t flag, FILE* outputFileStream) override;

  void wait(grk_wait_swath* swath) override;

  void scheduleSwathCopy(const grk_wait_swath* swath, grk_swath_buffer* buf) override;

  void waitSwathCopy() override;

  /**
   * @brief Sets the Post Post Process object
   *
   * @param func
   */
  void setPostPostProcess(std::function<bool(GrkImage*)> func);

protected:
  /**
   * @brief Sets the decompress region
   *
   * @param region region in canvas coordinates, relative to image origin
   * @return true if region is not set by user, or if it is set successfully
   */
  bool setDecompressRegion(RectD region);

  /**
   * @brief Decompresses all tiles
   *
   * @return true if successful
   */
  bool decompressImpl(std::set<uint16_t> pendingTiles);

  bool decompressTileImpl(uint16_t tile_index);

  /**
   * @brief Dumps main header info to file
   * @param outputFileStream output file stream
   */
  void dumpMainHeader(FILE* outputFileStream);

  /**
   * @brief Dumps an image header structure.
   * @param image image header to dump.
   * @param dev_dump_flag flag to describe if we are in the case of this function is use
   * outside dump function
   * @param outputFileStream output stream where dump the elements.
   */
  void dumpImageHeader(GrkImage* image, bool dev_dump_flag, FILE* outputFileStream);

  /**
   * @brief Dumps tile info to file
   *
   * @param default_tile
   * @param numcomps
   * @param outputFileStream
   */
  void dumpTileHeader(TileCodingParams* defaultTile, uint32_t numcomps, FILE* outputFileStream);

  /**
   * @brief Dumps a component image header structure.
   * @param comp	the component image header to dump.
   * @param dev_dump_flag flag to describe if we are in the case of this function is use
   * outside dump function
   * @param outputFileStream	output stream where dump the elements.
   */
  void dumpImageComponentHeader(grk_image_comp* comp, bool dev_dump_flag, FILE* outputFileStream);

private:
  void postReadHeader(void);

  void onRowCompleted(uint16_t tileIndexBegin);
  void scheduleTileBatch();
  static constexpr int32_t maxRowsAhead_ = 2;

  void wait(uint16_t tile_index);
  /**
   * @brief Prepares to read first slated tile part
   *
   */
  void decompressSequentialPrepare(void);

  bool schedule(ITileProcessor* tileProcessor, bool multiTile);

  /**
   * @brief Parses next slated tile
   *
   * @return true if successful
   */
  bool sequentialParseAndSchedule(bool multiTile);

  /**
   * @brief Reads unknown marker
   *
   * @return true
   * @return false
   */
  bool readUNK(void);

  /**
   * @brief Updates differential decompress state
   *
   * @param scratch
   */
  void differentialUpdate(GrkImage* scratch);

  /**
   * @brief Reads a CBD marker (Component bit depth definition)
   * @param headerData header data
   * @param headerSize size of header data
   * @return true if successful
   */
  bool readCBD(uint8_t* headerData, uint16_t headerSize);

  /**
   * @brief Reads header - all markers until first SOT
   *
   * @return true if successful
   */
  bool readHeaderProcedure(void);
  /**
   * @brief Reads a SOC marker (Start of Codestream)
   */
  bool readSOC();
  /**
   * @brief Reads a SIZ marker (image and tile size)
   * @param headerData header data
   * @param headerSize size of header data
   * @return true if successful
   */
  bool readSIZ(uint8_t* headerData, uint16_t headerSize);
  /**
   * @brief Reads a CAP marker
   * @param headerData header data
   * @param headerSize size of header data
   * @return true if successful
   */
  bool readCAP(uint8_t* headerData, uint16_t headerSize);

  /**
   * @brief Reads a CRG marker (Component registration)
   * @param headerData   header data
   * @param headerSize     size of header data
   * @return true if successful
   */
  bool readCRG(uint8_t* headerData, uint16_t headerSize);
  /**
   * @brief Reads a TLM marker (Tile Length Marker)
   * @param headerData header data
   * @param headerSize size of header data
   * @return true if successful
   */
  bool readTLM(uint8_t* headerData, uint16_t headerSize);
  /**
   * @brief Reads a PLM marker (Packet length, main header marker)
   * @param headerData header data
   * @param headerSize size of header data
   * @return true if successful
   */
  bool readPLM(uint8_t* headerData, uint16_t headerSize);

  /**
   * @brief Reads a PPM marker (Packed headers, main header)
   * @param headerData header data
   * @param headerSize size of header data
   * @return true if successful
   */
  bool readPPM(uint8_t* headerData, uint16_t headerSize);

  /**
   * @brief Read SOT (Start of tile part) marker
   * @param headerData header data
   * @param headerSize size of header data
   * @return true if successful
   */
  bool readSOT(uint8_t* headerData, uint16_t headerSize);

  /**
   * @brief Merges all PPM markers read (Packed headers, main header)
   * @param       p_cp      main coding parameters.
   * @return true if successful
   */
  bool mergePpm(CodingParams* p_cp);

  /**
   * @brief Activates scratch image
   *
   * @param singleTile
   * @param scratch
   * @return true
   * @return false
   */
  bool activateScratch(bool singleTile, GrkImage* scratch);

  /**
   * @brief Creates a Post Task object
   *
   * @param tileProcessor
   * @return std::function<void()>
   */
  std::function<void()> postMultiTile(ITileProcessor* tileProcessor);
  std::function<void()> postMultiTile(void);

  std::function<void()> postSingleTile(ITileProcessor* tileProcessor);

  std::function<bool()>
      genDecompressTileTLMTask(ITileProcessor* tileProcessor,
                               const std::shared_ptr<TPFetchSeq>& tilePartFetchSeq,
                               Rect32 unreducedImageBounds,
                               std::function<std::function<void()>(ITileProcessor*)> postGenerator);

  void decompressSequential(const std::set<uint16_t>& pendingTiles);
  void decompressTLM(const std::set<uint16_t>& pendingTiles);

  bool startTLMDecompress(std::set<uint16_t>& pendingTiles);
  bool startSequentialDecompress(std::set<uint16_t>& pendingTiles);

  bool doTileBatching(void);

  /**
   * @brief @ref MarkerParser
   */
  MarkerParser markerParser_;

  /**
   * @brief @ref TileWindow
   */
  TileWindow tilesToDecompress_;

  /**
   * @brief @ref MarkerCache
   */
  std::unique_ptr<MarkerCache> markerCache_;

  /**
   * @brief Tile processor currently being parsed
   *
   */
  ITileProcessor* currTileProcessor_ = nullptr;

  TilePartInfo currTilePartInfo_;
  int32_t currTileIndex_ = -1;

  /**
   * @brief Default @ref TileCodingParams
   *
   * Store decoding parameters common to all tiles (information
   * like COD, COC and RGN in main header)
   */
  std::unique_ptr<TileCodingParams> defaultTcp_;

  /** @brief Selective fetch tile-parts (kept alive for async Phase 2 fetch) */
  TPSEQ_VEC selectiveTileParts_;

  /**
   * @brief true if there was an error reading the main header
   */
  bool headerError_ = false;
  /**
   * @brief true if main header was successfully read
   */
  bool headerRead_ = false;

  /**
   * @brief multi tile composite @ref GrkImage
   * Used to composite multiple tiles - subsampled and reduced for region decompression
   *
   */
  std::unique_ptr<GrkImage, RefCountedDeleter<GrkImage>> multiTileComposite_;

  /**
   * @brief Holds unreduced unsubsampled decompress region , if set
   *
   */
  Rect32 region_;

  std::function<void()> postMulti_;

  /**
   * @brief @ref TileCache
   */
  std::unique_ptr<TileCache> tileCache_;

  /**
   * @brief callback for io pixels
   *
   */
  grk_io_pixels_callback ioBufferCallback_ = nullptr;

  /**
   * @brief io user data
   *
   */
  void* ioUserData_ = nullptr;

  /**
   * @brief callback invoked when a tile-row band is ready for incremental writing
   */
  grk_io_band_callback ioBandCallback_ = nullptr;
  void* ioBandUserData_ = nullptr;

  // band ordering for incremental writes
  std::mutex bandOrderMutex_;
  std::condition_variable bandDrainCV_;
  uint16_t nextBandTileY_ = 0;
  struct PendingBand_
  {
    uint32_t yBegin, yEnd;
    uint16_t tileX0, numCols;
  };
  std::map<uint16_t, PendingBand_> pendingBands_;

  /**
   * @brief callback to reclaim io buffers
   *
   */
  grk_io_register_reclaim_callback grkRegisterReclaimCallback_ = nullptr;

  /**
   * @brief post post-decompress method
   *
   */
  std::function<bool(GrkImage*)> postPostProcess_;

  /**
   * @brief futures for scheduled tiles
   *
   */
  TileFutureManager decompressTileFutureManager_;

  /**
   * @brief futures for swath copy tasks scheduled via scheduleSwathCopy()
   *
   */
  TileFutureManager swathCopyFutureManager_;

  std::shared_ptr<TPFetchSeq> tilePartFetchFlat_;
  std::shared_ptr<std::unordered_map<uint16_t, std::shared_ptr<TPFetchSeq>>> tilePartFetchByTile_;
  std::vector<std::future<bool>> fetchByTileFutures_;

  bool fetchByTile(std::set<uint16_t>& slated, Rect32 unreducedImageBounds,
                   std::function<std::function<void()>(ITileProcessor*)> postGenerator);

  /**
   * @brief Two-phase PLT-based selective tile fetch for reduced resolution decompression.
   *
   * Phase 1 (header probe): Fetches a small prefix (4 KB) of each tile-part to extract
   * the PLT marker (packet lengths) and SOD offset without downloading full tile data.
   *
   * Range computation: Uses PLT packet lengths and the tile's progression order to compute
   * the minimal byte ranges needed for the target resolution level (reduce > 0).
   * For resolution-first progressions (RLCP, RPCL) this produces a single contiguous range;
   * for layer-first (LRCP, CPRL, PCRL) it may produce disjoint ranges.
   *
   * Phase 1 data reuse: When the computed ranges fit entirely within the 4 KB already
   * fetched in Phase 1, the data is reused directly — no Phase 2 HTTP request is issued.
   * This is common for small tiles or low-resolution decompressions where all needed
   * packets are contained in the initial header fetch.
   *
   * Phase 2 (selective fetch): For tiles whose needed data exceeds the Phase 1 buffer,
   * truncated or disjoint HTTP range requests are issued to fetch only the required
   * packet data.
   *
   * @param slated           set of tile indices to decompress
   * @param unreducedImageBounds  unreduced image bounds for decompress tasks
   * @param postGenerator    factory for post-decompress callbacks
   * @return true if tiles were scheduled for decompression
   */
  bool fetchByTileSelective(std::set<uint16_t>& slated, Rect32 unreducedImageBounds,
                            std::function<std::function<void()>(ITileProcessor*)> postGenerator);

  /**
   * @brief Enqueue a tile for decompression via the decompress pipeline.
   *
   * Updates the compressed chunk cache, advances the max-fetched tile row,
   * and pushes a decompress task onto the decompress queue.
   *
   * @param tileIndex         tile index
   * @param decompressSeq     tile-part fetch sequence with data and MemStreams
   * @param numTileCols       number of tile columns in the grid
   * @param unreducedImageBounds  unreduced image bounds for decompress tasks
   * @param postGenerator     factory for post-decompress callbacks
   */
  void enqueueTileForDecompress(
      uint16_t tileIndex, std::shared_ptr<TPFetchSeq> decompressSeq, uint16_t numTileCols,
      Rect32 unreducedImageBounds,
      std::function<std::function<void()>(ITileProcessor*)> postGenerator);

  /**
   * @brief Build selective tile-part entries for a single tile.
   *
   * Uses PLT packet lengths from the Phase 1 header result and the tile's
   * progression order to compute the minimal fetch ranges for the target
   * resolution. Populates selectiveTileParts_[tileIndex] with either:
   *  - Truncated entries (contiguous case) — one per tile-part, trimmed to
   *    the needed prefix of packet data.
   *  - Synthetic entries (disjoint case) — header entry + one entry per
   *    disjoint data range within the tile-part.
   *  - Full tile-part entries (fallback) — when PLT is invalid or no savings
   *    are possible.
   *
   * @param tileIndex       tile index
   * @param hdr             Phase 1 header result for this tile
   * @param allTileParts    full tile-part offset/length info from TLM
   * @param reduce          resolution reduction level
   */
  void buildSelectiveTileParts(uint16_t tileIndex, const TileHeaderResult& hdr,
                               const TPSEQ_VEC& allTileParts, uint8_t reduce);

  /**
   * @brief Try to reuse Phase 1 data for tiles whose needed bytes fit in the header fetch.
   *
   * For each tile in selectiveFetchTiles, checks whether all selective entries
   * fit within the already-fetched Phase 1 buffers. If so, builds a decompression
   * sequence directly from Phase 1 data and removes the tile from the fetch set.
   *
   * Handles both contiguous and disjoint layouts.
   *
   * @param selectiveFetchTiles  tiles scheduled for Phase 2 fetch (modified in-place)
   * @param headerResults        Phase 1 header data per tile
   * @param allTileParts         full tile-part info from TLM
   * @return vector of tiles with pre-built decompression sequences
   */
  std::vector<std::pair<uint16_t, std::shared_ptr<TPFetchSeq>>> reusePhase1Data(
      std::shared_ptr<std::set<uint16_t>>& selectiveFetchTiles,
      const std::unordered_map<uint16_t, TileHeaderResult>& headerResults,
      const TPSEQ_VEC& allTileParts);

  /**
   * @brief Scratch @ref GrkImage for decompressor
   * This image may composite multiple tiles, if needed.
   */
  std::unique_ptr<GrkImage, RefCountedDeleter<GrkImage>> scratchImage_;

  std::unique_ptr<GrkImage, std::function<void(GrkImage*)>> activeImage_;

  /**
   * @brief track success of scheduled tile decompression
   *
   */
  std::atomic<bool> success_{true};

  /**
   * @brief number of decompressed tiles
   *
   */
  std::atomic<uint32_t> numTilesDecompressed_{0};

  /**
   * @brief pool of @ref ICoder
   *
   */
  CoderPool coderPool_;

  std::vector<std::unique_ptr<MarkerParser>> tileMarkerParsers_;

  /**
   * @brief global HT flag
   *
   */
  bool isHT_ = false;

  std::unique_ptr<TileCompletion> tileCompletion_;

  std::thread decompressWorker_;

  std::function<bool(std::set<uint16_t>&)> decompressStart_;

  std::shared_ptr<ChunkBuffer<>> chunkBuffer_;

  // Compressed chunk cache: stores fetched compressed data for re-decompression
  std::unique_ptr<CompressedChunkCache> compressedChunkCache_;

  // Tile batching

  std::mutex batchTileQueueMutex_;
  std::condition_variable batchTileQueueCondition_;
  uint16_t batchTileInitialRows_ = 2;
  uint16_t batchTileNextRows_ = 2;
  uint16_t batchTileHeadroomIncrement(uint16_t numRows, uint16_t tilesLeft);

  // batch TLM
  std::queue<uint16_t> batchTileQueueTLM_;

  // producer-consumer decompression pipeline
  void startDecompressConsumer(uint16_t maxInFlight);
  std::unique_ptr<ConcurrentQueue<std::function<void()>>> decompressQueue_;
  std::thread decompressConsumer_;
  std::mutex decompressThrottleMutex_;
  std::condition_variable decompressThrottleCV_;
  uint16_t decompressInFlight_ = 0;
  uint16_t maxDecompressInFlight_ = 0;

  // Row-based fetch throttle: highest tile row that has been fully fetched
  std::atomic<int32_t> maxFetchedTileRow_{-1};

  // batch sequential
  bool batchDequeueSequential(void);
  std::queue<ITileProcessor*> batchTileQueueSequential_;
  uint16_t batchTileScheduleHeadroomSequential_ = 0;
  uint16_t batchTileUnscheduledSequential_ = 0;
  int32_t batchTileScheduledRows_ = 0; // Highest row index we've scheduled up to
};

} // namespace grk
