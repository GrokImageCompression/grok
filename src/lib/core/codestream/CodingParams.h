/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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

#include "Quantizer.h"

namespace grk
{
/**
 * Type of elements storing in the MCT data
 */
enum J2K_MCT_ELEMENT_TYPE
{
   MCT_TYPE_INT16 = 0, /** MCT data is stored as signed shorts*/
   MCT_TYPE_INT32 = 1, /** MCT data is stored as signed integers*/
   MCT_TYPE_FLOAT = 2, /** MCT data is stored as floats*/
   MCT_TYPE_DOUBLE = 3 /** MCT data is stored as doubles*/
};

/**
 * Type of MCT array
 */
enum J2K_MCT_ARRAY_TYPE
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
   uint8_t csty;
   /** number of resolutions */
   uint8_t numresolutions;
   /** log2(code-blocks width) */
   uint8_t cblkw;
   /** log2(code-blocks height) */
   uint8_t cblkh;
   /** code-block mode */
   uint8_t cblk_sty;
   /** discrete wavelet transform identifier */
   uint8_t qmfbid;
   // true if quantization marker has been read for this component,
   // false otherwise
   bool quantizationMarkerSet;
   // true if quantization marker was read from QCC otherwise false
   bool fromQCC;
   // true if quantization marker was read from tile header
   bool fromTileHeader;
   /** quantisation style */
   uint8_t qntsty;
   /** stepsizes used for quantization */
   grk_stepsize stepsizes[GRK_MAXBANDS];
   // number of step sizes read from QCC marker
   uint8_t numStepSizes;
   /** number of guard bits */
   uint8_t numgbits;
   /** Region Of Interest shift */
   uint8_t roishift;
   /** precinct width (power of 2 exponent, < 16) */
   uint32_t precWidthExp[GRK_MAXRLVLS];
   /** precinct height (power of 2 exponent, < 16) */
   uint32_t precHeightExp[GRK_MAXRLVLS];
   /** the dc_level_shift **/
   int32_t dc_level_shift_;
};

/**
 * MCT data
 */
struct grk_mct_data
{
   J2K_MCT_ELEMENT_TYPE element_type_;
   J2K_MCT_ARRAY_TYPE array_type_;
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
 tiles (information like COD, COC in main header)
 */
struct TileCodingParams
{
   TileCodingParams();
   ~TileCodingParams();

   bool advanceTilePartCounter(uint16_t tileIndex, uint8_t tilePartIndex);
   bool copy(const TileCodingParams* rhs, const GrkImage* image);
   void setIsHT(bool ht, bool reversible, uint8_t guardBits);
   bool isHT(void);
   uint32_t getNumProgressions(void);
   bool hasPoc(void);

   /** coding style */
   uint8_t csty;
   /** progression order */
   GRK_PROG_ORDER prg;
   /** number of layers */
   uint16_t max_layers_;
   uint16_t numLayersToDecompress;
   /** multi-component transform identifier */
   uint8_t mct;
   /** rates of layers */
   double rates[maxCompressLayersGRK];
   /** number of progression order changes */
   uint32_t numpocs;
   /** progression order changes */
   grk_progression progressionOrderChange[GRK_MAXRLVLS];
   /** number of ppt markers (reserved size) */
   uint32_t ppt_markers_count;
   /** ppt markers data (table indexed by Zppt) */
   grk_ppx* ppt_markers;
   /** packet header store there for future use in t2_decode_packet */
   uint8_t* ppt_data;
   /** used to keep a track of the allocated memory */
   uint8_t* ppt_buffer;
   /** Number of bytes stored inside ppt_data*/
   size_t ppt_data_size;
   /** size of ppt_data*/
   size_t ppt_len;
   /** fixed_quality */
   double distortion[maxCompressLayersGRK];
   // quantization style as read from main QCD marker
   uint32_t main_qcd_qntsty;
   // number of step sizes as read from main QCD marker
   uint32_t main_qcd_numStepSizes;
   /** tile-component coding parameters */
   TileComponentCodingParams* tccps;
   // current tile part index, based on count of tile parts
   // (-1 if never incremented)
   // NOTES:
   // 1. tile parts must appear in code stream in strictly increasing
   // order
   // 2. tile part index must be  <= 255
   uint8_t tilePartCounter_;
   /** number of tile parts for the tile. */
   uint8_t numTileParts_;
   SparseBuffer* compressedTileData_;
   /** compressing norms */
   double* mct_norms;
   /** the mct decoding matrix */
   float* mct_decoding_matrix_;
   /** the mct coding matrix */
   float* mct_coding_matrix_;
   /** mct records */
   grk_mct_data* mct_records_;
   /** the number of mct records. */
   uint32_t nb_mct_records_;
   /** the max number of mct records. */
   uint32_t nb_max_mct_records_;
   /** mcc records */
   grk_simple_mcc_decorrelation_data* mcc_records_;
   /** the number of mct records. */
   uint32_t nb_mcc_records_;
   /** the max number of mct records. */
   uint32_t nb_max_mcc_records_;
   /** If cod == true --> there was a COD marker for the present tile */
   bool cod;
   /** If ppt == true --> there was a PPT marker for the present tile */
   bool ppt;
   Quantizer* qcd_;

 private:
   bool ht_;
};

struct EncodingParams
{
   /** Maximum rate for each component.
	* If == 0, component size limitation is not considered */
   size_t max_comp_size_;
   /** Position of tile part flag in progression order*/
   uint32_t newTilePartProgressionPosition;
   /** Flag determining tile part generation*/
   uint8_t newTilePartProgressionDivider_;
   /** allocation by rate/distortion */
   bool allocationByRateDistortion_;
   /** allocation by fixed_quality */
   bool allocationByFixedQuality_;
   /** Enabling Tile part generation*/
   bool enableTilePartGeneration_;
   /* write plt marker */
   bool writePLT;
   /* write TLM marker */
   bool writeTLM;
   /* rate control algorithm */
   uint32_t rateControlAlgorithm;
};

struct DecodingParams
{
   /** if != 0, then original dimension divided by 2^(reduce); if == 0 or not used, image is
	* decompressed to the full resolution */
   uint8_t reduce_;
   /** if != 0, then only the first "layer" layers are decompressed; if == 0 or not used, all the
	* quality layers are decompressed */
   uint16_t layers_to_decompress_;

   uint32_t randomAccessFlags_;
};

/**
 * Coding parameters
 */
struct CodingParams
{
   CodingParams();
   ~CodingParams();
   grk_rect32 getTileBounds(const GrkImage* p_image, uint32_t tile_x, uint32_t tile_y) const;

   /** Rsiz*/
   uint16_t rsiz;
   /* Pcap */
   uint32_t pcap;
   /* Ccap */
   uint16_t ccap[32];
   /** XTOsiz */
   uint32_t tx0;
   /** YTOsiz */
   uint32_t ty0;
   /** XTsiz */
   uint32_t t_width;
   /** YTsiz */
   uint32_t t_height;
   /** comments */
   size_t num_comments;
   char* comment[GRK_NUM_COMMENTS_SUPPORTED];
   uint16_t comment_len[GRK_NUM_COMMENTS_SUPPORTED];
   bool isBinaryComment[GRK_NUM_COMMENTS_SUPPORTED];
   // note: maximum number of tiles is 65535
   /** number of tiles in width */
   uint16_t t_grid_width;
   /** number of tiles in height */
   uint16_t t_grid_height;
   PPMMarker* ppm_marker;
   /** tile coding parameters */
   TileCodingParams* tcps;
   union
   {
	  DecodingParams dec_;
	  EncodingParams enc_;
   } coding_params_;
   TileLengthMarkers* tlm_markers;
   PLMarkerMgr* plm_markers;
   bool wholeTileDecompress_;
};

/**
 * Status of decoding process when decoding main header or tile header.
 * These values may be combined with the | operator.
 * */
enum DECOMPRESS_STATE
{
   DECOMPRESS_STATE_NONE = 0x0000, /**< no decompress state */
   DECOMPRESS_STATE_MH_SOC = 0x0001, /**< a SOC marker is expected */
   DECOMPRESS_STATE_MH_SIZ = 0x0002, /**< a SIZ marker is expected */
   DECOMPRESS_STATE_MH = 0x0004, /**< the decoding process is in the main header */
   DECOMPRESS_STATE_TPH = 0x0008, /**< the decoding process is in a tile part header */
   DECOMPRESS_STATE_TPH_SOT = 0x0010, /**< the decoding process is in a tile part header
					 and expects a SOT marker */
   DECOMPRESS_STATE_DATA = 0x0020, /**< the decoding process is expecting
				 to read tile data from the code stream */
   DECOMPRESS_STATE_EOC = 0x0040, /**< the decoding process has encountered the EOC marker */
   DECOMPRESS_STATE_NO_EOC = 0x0080, /**< the decoding process must not expect a EOC marker
				   because the code stream is truncated */
};

class CodeStreamDecompress;

struct DecompressorState
{
   DecompressorState();
   bool findNextSOT(CodeStreamDecompress* codeStream);
   uint16_t getState(void);
   void setState(uint16_t state);
   void orState(uint16_t state);
   void andState(uint16_t state);
   void setComplete(uint16_t tileIndex);

   // store decoding parameters common to all tiles (information
   // like COD, COC and RGN in main header)
   TileCodingParams* default_tcp_;

   TileSet tilesToDecompress_;

   /** Position of the last SOT marker read */
   uint64_t lastSotReadPosition;
   /**
	* Indicate that the current tile-part is assumed to be the last tile part of the code stream.
	* This is useful in the case when PSot is equal to zero. The SOT length will be computed in the
	* SOD reader function.
	*/
   bool lastTilePartInCodeStream;

 private:
   /** Decoder state: used to indicate in which part of the code stream
	*  the decompressor is (main header, tile header, end) */
   uint16_t state_;
};

struct CompressorState
{
   CompressorState() : total_tile_parts_(0) {}
   /** Total num of tile parts in whole image = num tiles* num tileparts in each tile*/
   /** used in TLMmarker*/
   uint16_t total_tile_parts_; /* numTilePartsTotal */
};

} // namespace grk
