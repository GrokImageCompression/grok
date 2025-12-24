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

#include "PacketCache.h"
#include <memory>
#include <mutex>

namespace grk
{

typedef void (*mct_function)(const void* p_src_data, void* p_dest_data, uint64_t nb_elem);
/**
 * Type of elements storing in the MCT data
 */
enum MCT_ELEMENT_TYPE
{
  MCT_TYPE_INT16 = 0, /** MCT data is stored as signed shorts*/
  MCT_TYPE_INT32 = 1, /** MCT data is stored as signed integers*/
  MCT_TYPE_FLOAT = 2, /** MCT data is stored as floats*/
  MCT_TYPE_DOUBLE = 3 /** MCT data is stored as doubles*/
};

/**
 * Type of MCT array
 */
enum MCT_ARRAY_TYPE
{
  MCT_TYPE_DEPENDENCY = 0,
  MCT_TYPE_DECORRELATION = 1,
  MCT_TYPE_OFFSET = 2
};

/**
 Tile-component coding parameters
 */
struct TileComponentCodingParams
{
  TileComponentCodingParams();
  /** coding style */
  uint8_t csty_;
  /** number of resolutions */
  uint8_t numresolutions_;
  /** log2(code-blocks width) */
  uint8_t cblkw_expn_;
  /** log2(code-blocks height) */
  uint8_t cblkh_expn_;
  /** code-block mode */
  uint8_t cblkStyle_;
  /** discrete wavelet transform identifier */
  uint8_t qmfbid_;
  // true if quantization marker has been read for this component,
  // false otherwise
  bool quantizationMarkerSet_;
  // true if quantization marker was read from QCC otherwise false
  bool fromQCC_;
  // true if quantization marker was read from tile header
  bool fromTileHeader_;
  /** quantisation style */
  uint8_t qntsty_;
  /** stepsizes used for quantization */
  grk_stepsize stepsizes_[GRK_MAXBANDS];
  // number of step sizes read from QCC marker
  uint8_t numStepSizes_;
  /** number of guard bits */
  uint8_t numgbits_;
  /** Region Of Interest shift */
  uint8_t roishift_;
  /** precinct width (power of 2 exponent, < 16) */
  uint8_t precWidthExp_[GRK_MAXRLVLS];
  /** precinct height (power of 2 exponent, < 16) */
  uint8_t precHeightExp_[GRK_MAXRLVLS];
  /** the dc_level_shift **/
  int32_t dcLevelShift_;
};

/**
 * MCT data
 */
struct grk_mct_data
{
  MCT_ELEMENT_TYPE element_type_;
  MCT_ARRAY_TYPE array_type_;
  uint32_t index_;
  uint8_t* data_;
  uint32_t data_size_;
};

/**
 * MCC decorrelation data
 */
struct grk_simple_mcc_decorrelation_data
{
  uint32_t index_;
  uint32_t nb_comps_;
  grk_mct_data* decorrelation_array_;
  grk_mct_data* offset_array_;
  uint32_t is_irreversible_ : 1;
};

/**
 Tile coding parameters :
 this structure is used to store coding/decoding parameters common to all
 tiles (information like COD, COC in main header). It also stores compressed
 packets for a particular tile.
 */
struct TileCodingParams
{
  TileCodingParams(CodingParams* cp);
  TileCodingParams(const TileCodingParams& rhs);
  ~TileCodingParams();

  bool advanceTilePartCounter(uint16_t tileIndex, uint8_t tilePartIndex);
  bool initDefault(GrkImage* headerImage);

  /**
   * Reads a PPT marker (Packed packet headers, tile-part header)
   * @param headerData header data
   * @param headerSize size of header data
   * @return true if successful
   */
  bool readPpt(uint8_t* headerData, uint16_t headerSize);

  /**
   * @brief Merges all PPT markers read (Packed headers, tile-part header)
   * @param tcp @ref TileCodingParams
   * @return true if successful
   */
  bool mergePpt(void);

  /**
   * @brief Reads a SPCod or SPCoc element, i.e. the coding style of a given component of a tile.
   * @param compno          component number
   * @param headerData header data
   * @param headerSize size of header data
   * @return true if successful
   */
  bool readSPCodSPCoc(uint16_t compno, uint8_t* headerData, uint16_t* headerSize);

  /**
   * @brief Reads a SQcd or SQcc element, i.e. the quantization values of a band
   * in the QCD or QCC.
   * @param fromTileHeader true if marker is from tile header
   * @param	fromQCC true if reading QCC, otherwise false (reading QCD)
   * @param compno  the component number to output.
   * @param headerData the data buffer.
   * @param headerSize pointer to the size of the data buffer,
   *        it is changed by the function.
   * @return true if successful
   */
  bool readSQcdSQcc(bool fromTileHeader, bool fromQCC, uint16_t compno, uint8_t* headerData,
                    uint16_t* headerSize);

  /**
   * @brief Reads a COD marker (Coding Style defaults)
   * @param headerData header data
   * @param headerSize size of header data
   * @return true if successful
   */
  bool readCod(uint8_t* headerData, uint16_t headerSize);

  /**
   * @brief Reads a COC marker (Coding Style Component)
   * @param headerData header data
   * @param headerSize size of header data
   * @return true if successful
   */
  bool readCoc(uint8_t* headerData, uint16_t headerSize);

  /**
   * @brief Reads a QCD marker (Quantization defaults)
   * @param fromTileHeader true if marker is from tile header
   * @param headerData header data
   * @param headerSize size of header data
   * @return true if successful
   */
  bool readQcd(bool fromTileHeader, uint8_t* headerData, uint16_t headerSize);

  /**
   * @brief Reads a QCC marker (Quantization component)
   * @brief fromTileHeader true if marker from tile header
   * @param headerData header data
   * @param headerSize size of header data
   * @return true if successful
   */
  bool readQcc(bool fromTileHeader, uint8_t* headerData, uint16_t headerSize);

  /**
   * @brief Reads a RGN marker (Region Of Interest)
   * @param headerData header data
   * @param headerSize size of header data
   * @return true if successful
   */
  bool readRgn(uint8_t* headerData, uint16_t headerSize);

  /**
   * @brief Reads a POC marker (Progression Order Change)
   * @param headerData header data
   * @param headerSize size of header data
   * @param tilePartIndex the tile part index (-1 for main header)
   * @return true if successful
   */
  bool readPoc(uint8_t* headerData, uint16_t headerSize, int tilePartIndex);

  /**
   * @brief Reads a MCT marker (Multiple Component Transform)
   * @param headerData header data
   * @param headerSize size of header data
   * @return true if successful
   */
  bool readMct(uint8_t* headerData, uint16_t headerSize);

  /**
   * @brief Reads a MCO marker (Multiple Component Transform Ordering)
   * @param headerData header data
   * @param headerSize size of header data
   * @return true if successful
   */
  bool readMco(uint8_t* headerData, uint16_t headerSize);

  /**
   * @brief Reads a MCC marker (Multiple Component Collection)
   * @param headerData header data
   * @param headerSize size of header data
   * @return true if successful
   */
  bool readMcc(uint8_t* headerData, uint16_t headerSize);

  bool addMct(uint32_t index);

  void updateLayersToDecompress(void);

  bool validateQuantization(void);

  void setIsHT(bool ht, bool reversible, uint8_t guardBits);
  bool isHT(void);
  uint32_t getNumProgressions(void);
  bool hasPoc(void);
  void finalizePocs(void);

  CodingParams* cp_ = nullptr;

  bool wholeTileDecompress_ = true;

  /** coding style */
  uint8_t csty_ = 0;
  /** progression order */
  GRK_PROG_ORDER prg_ = GRK_PROG_UNKNOWN;
  /** number of layers */
  uint16_t numLayers_ = 0;
  /* layers slated for decompression */
  uint16_t layersToDecompress_ = 0;
  /** multi-component transform identifier */
  uint8_t mct_ = 0;
  /** rates of layers */
  double rates_[maxCompressLayersGRK];
  /** number of progression order changes */
  uint32_t numpocs_ = 0;
  /** progression order changes */
  grk_progression progressionOrderChange_[GRK_MAXRLVLS];
  /** number of ppt markers (reserved size) */
  uint32_t pptMarkersCount_ = 0;
  /** ppt markers data (table indexed by Zppt) */
  grk_ppx* pptMarkers_ = nullptr;
  /** packet header store there for future use in t2_decode_packet */
  uint8_t* pptData_ = nullptr;
  /** used to keep a track of the allocated memory */
  uint8_t* pptBuffer_ = nullptr;
  /** size of ppt_data*/
  size_t pptLength_ = 0;
  /** fixed_quality */
  double distortion_[maxCompressLayersGRK];
  // quantization style as read from main QCD marker
  uint32_t mainQcdQntsty = 0;
  // number of step sizes as read from main QCD marker
  uint32_t mainQcdNumStepSizes = 0;
  /** tile-component coding parameters */
  TileComponentCodingParams* tccps_ = nullptr;
  // tile part counter
  // NOTES: tile parts must appear in code stream in strictly increasing
  // order
  uint8_t tilePartCounter_ = 0;
  /** number of tile parts for the tile, signlled by TLM or SOT marker. */
  uint8_t signalledNumTileParts_ = 0;
  // packets
  PacketCache* packets_ = nullptr;
  /** compressing norms */
  double* mct_norms_ = nullptr;
  /** the mct decoding matrix */
  float* mctDecodingMatrix_ = nullptr;
  /** the mct coding matrix */
  float* mctCodingMatrix_ = nullptr;
  /** mct records */
  grk_mct_data* mctRecords_ = nullptr;
  /** the number of mct records. */
  uint32_t numMctRecords_ = 0;
  /** the max number of mct records. */
  uint32_t numMaxMctRecords_ = 0;
  /** mcc records */
  grk_simple_mcc_decorrelation_data* mccRecords_ = nullptr;
  /** the number of mct records. */
  uint32_t numMccRecords_ = 0;
  /** the max number of mct records. */
  uint32_t numMaxMccRecords_ = 0;
  /** If cod == true --> there was a COD marker for the present tile */
  uint32_t cod_ = 0;
  /** If ppt == true --> there was a PPT marker for the present tile */
  bool ppt_ = false;
  Quantizer* qcd_ = nullptr;
  uint16_t numComps_ = 0;

private:
  bool ht_ = false;
  std::mutex pocMutex_;
  std::vector<std::vector<grk_progression>> pocLists_;
};

struct EncodingParams
{
  /** Maximum rate for each component.
   * If == 0, component rate limitation is not considered */
  size_t maxComponentRate_;
  /** Position of tile part flag in progression order*/
  uint8_t newTilePartProgressionPosition_;
  /** Flag determining tile part generation*/
  uint8_t newTilePartProgressionDivider_;
  /** allocation by rate/distortion */
  bool allocationByRateDistortion_;
  /** allocation by fixed_quality */
  bool allocationByFixedQuality_;
  /** Enabling Tile part generation*/
  bool enableTilePartGeneration_;
  /* write plt marker */
  bool writePlt_;
  /* write TLM marker */
  bool writeTlm_;
  /* rate control algorithm */
  uint32_t rateControlAlgorithm_;
};

struct DecodingParams
{
  /** if != 0, then original dimension divided by 2^(reduce);
   *  if == 0 or not used, image is decompressed to the full resolution */
  uint8_t reduce_;
  /** if != 0, then only the first "layersToDecompress_" layers are decompressed;
   *  if == 0 or not used, all the quality layers are decompressed */
  uint16_t layersToDecompress_;
  uint32_t disableRandomAccessFlags_;
  bool skipAllocateComposite_;
};

struct TLMMarker;

class TileCodingParamsPool
{
public:
  TileCodingParamsPool() = default;

  TileCodingParams* get(uint16_t tileIndex)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = tileMap_.find(tileIndex);

    if(it == tileMap_.end()) // Check if tile exists
    {
      auto tcp = std::make_unique<TileCodingParams>(nullptr);
      it = tileMap_.emplace(tileIndex, std::move(tcp)).first;
    }
    return it->second.get(); // Return raw pointer
  }

private:
  std::unordered_map<uint16_t, std::unique_ptr<TileCodingParams>> tileMap_;
  mutable std::mutex mutex_; // Mutex for thread safety
};

class TileCache;

/**
 * Coding parameters
 */
struct CodingParams
{
  CodingParams();
  ~CodingParams();
  Rect32 getTileBounds(Rect32 imageBounds, uint16_t tile_x, uint16_t tile_y) const;

  /**
   * @brief Reads a COM marker (comments)
   * @param headerData header data
   * @param headerSize size of header data
   * @return true if successful
   */
  bool readCom(uint8_t* headerData, uint16_t headerSize);

  /**
   * @brief Gets the number of tile parts for a given tile index from TLM marker
   * @param tileIndex Index of the tile
   * @return Number of tile parts, or 0 if invalid
   */
  uint8_t getNumTilePartsFromTLM(uint16_t tileIndex) const noexcept;

  bool hasTLM(void) const noexcept;

  void init(grk_decompress_parameters* parameters, std::unique_ptr<TileCache>& tileCache);

  uint16_t rsiz_; /** Rsiz*/
  uint32_t pcap_; /* Pcap */
  uint16_t ccap_[32]; /* Ccap */
  uint32_t tx0_; /** XTOsiz */
  uint32_t ty0_; /** YTOsiz */
  uint32_t t_width_; /** XTsiz */
  uint32_t t_height_; /** YTsiz */
  std::mutex commentMutex;
  size_t numComments_; /** comments */
  char* comment_[GRK_NUM_COMMENTS_SUPPORTED];
  uint16_t commentLength_[GRK_NUM_COMMENTS_SUPPORTED];
  bool isBinaryComment_[GRK_NUM_COMMENTS_SUPPORTED];
  uint16_t t_grid_width_; /** number of tiles in width */
  uint16_t t_grid_height_; /** number of tiles in height */

  double dw_x0 = 0; /* decompress window left boundary*/
  double dw_x1 = 0; /* decompress window right boundary*/
  double dw_y0 = 0; /* decompress window top boundary*/
  double dw_y1 = 0; /* decompress window bottom boundary*/
  std::unique_ptr<PPMMarker> ppmMarkers_;
  TileCodingParamsPool tcps_; /** default tile coding parameters */
  union
  {
    DecodingParams dec_;
    EncodingParams enc_;
  } codingParams_;
  std::unique_ptr<TLMMarker> tlmMarkers_;
  std::unique_ptr<PLMarker> plmMarkers_;
  bool asynchronous_;
  bool simulate_synchronous_;
  grk_decompress_callback decompressCallback_;
  void* decompressCallbackUserData_;
};

} // namespace grk
