/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

/**
 * @class CodeStreamDecompress
 * @brief Manages decompression
 */
class CodeStreamDecompress final : public CodeStream, public IDecompressor
{
public:
  /**
   * @brief Constructs a CodeStreamDecompress
   * @param stream @ref CodeStreamDecompress
   */
  CodeStreamDecompress(IStream* stream);

  /**
   * @brief Destroys a CodeStreamDecompress
   */
  ~CodeStreamDecompress() = default;

  void init(grk_decompress_parameters* param) override;

  grk_progression_state getProgressionState(uint16_t tile_index) override;

  bool setProgressionState(grk_progression_state state) override;

  /**
   * @brief Gets tile processor for specified tile index
   * @param tile_index tile index
   * @return @ref TileProcessor
   */
  TileProcessor* getTileProcessor(uint16_t tile_index);

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
  bool decompressImpl(std::set<uint16_t> slated);

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

  void onRowCompleted(uint16_t tileIndexBegin, uint16_t tileIndexEnd);

  void wait(uint16_t tile_index);
  /**
   * @brief Prepares to read first slated tile part
   *
   */
  void decompressSequentialPrepare(void);

  bool schedule(TileProcessor* tileProcessor, bool multiTile);

  /**
   * @brief Parses next slated tile
   *
   * @return true if successful
   */
  bool scheduleNextSlatedTile(bool multiTile);

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
  std::function<void()> postMultiTile(TileProcessor* tileProcessor);
  std::function<void()> postMultiTile(void);

  std::function<void()> postSingleTile(TileProcessor* tileProcessor);

  std::function<bool()>
      genDecompressTileTLMTask(TileProcessor* tileProcessor,
                               const std::shared_ptr<TPFetchSeq>& tilePartFetchSeq,
                               Rect32 unreducedImageBounds,
                               std::function<std::function<void()>(TileProcessor*)> postGenerator);

  void decompressSequential(void);
  void decompressTLM(const std::set<uint16_t>& slated);

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
  TileProcessor* currTileProcessor_ = nullptr;

  TilePartInfo currTilePartInfo_;
  int32_t currTileIndex_ = -1;

  /**
   * @brief Default @ref TileCodingParams
   *
   * Store decoding parameters common to all tiles (information
   * like COD, COC and RGN in main header)
   */
  std::unique_ptr<TileCodingParams> defaultTcp_;

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

  std::shared_ptr<TPFetchSeq> tilePartFetchFlat_;
  std::shared_ptr<std::unordered_map<uint16_t, std::shared_ptr<TPFetchSeq>>> tilePartFetchByTile_;
  std::vector<std::future<bool>> fetchByTileFutures_;

  bool fetchByTile(std::set<uint16_t>& slated, Rect32 unreducedImageBounds,
                   std::function<std::function<void()>(TileProcessor*)> postGenerator);

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
  std::atomic<bool> success_;

  /**
   * @brief number of decompressed tiles
   *
   */
  std::atomic<uint32_t> numTilesDecompressed_;

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

  std::shared_ptr<ChunkBuffer<>> chunkBuffer_;

  // Tile batching

  // batch TLM
  std::queue<uint16_t> batchTileQueueTLM_;

  // batch sequential
  bool batchDequeueSequential(void);
  std::queue<TileProcessor*> batchTileQueueSequential_;
  uint16_t batchTileScheduleHeadroomSequential_ = 0;
  uint16_t batchTileUnscheduledSequential_ = 0;

  // batch
  std::mutex batchTileQueueMutex_;
  std::condition_variable batchTileQueueCondition_;
  uint16_t batchTileInitialRows_ = 2;
  uint16_t batchTileNextRows_ = 2;
  uint16_t batchTileHeadroomIncrement(uint16_t numRows, uint16_t tilesLeft);
};

} // namespace grk
