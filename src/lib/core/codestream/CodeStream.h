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

#include "CodingParams.h"

namespace grk
{
const uint32_t default_numbers_segments = 10;
const uint32_t default_header_size = 4096;
const uint32_t default_number_mcc_records = 10;
const uint32_t default_number_mct_records = 10;

// includes marker and marker length (4 bytes)
const uint32_t sot_marker_segment_len_minus_tile_data_len = 12U;
const uint32_t sot_marker_segment_min_len = 14U;

const uint32_t SPCod_SPCoc_len = 5U;
const uint32_t cod_coc_len = 5U;
const uint32_t tlmMarkerBytesPerTilePart = 6;

const uint32_t GRK_COMP_PARAM_DEFAULT_CBLOCKW = 64;
const uint32_t GRK_COMP_PARAM_DEFAULT_CBLOCKH = 64;
const GRK_PROG_ORDER GRK_DEFAULT_PROG_ORDER = GRK_LRCP;
const uint32_t GRK_DEFAULT_NUMRESOLUTION = 6;

#define J2K_CP_CSTY_PRT 0x01
#define J2K_CP_CSTY_SOP 0x02
#define J2K_CP_CSTY_EPH 0x04
#define J2K_CCP_CSTY_PRT 0x01
#define J2K_CCP_QNTSTY_NOQNT 0 // no quantization
#define J2K_CCP_QNTSTY_SIQNT 1 // derived quantization
#define J2K_CCP_QNTSTY_SEQNT 2 // expounded quantization

const uint16_t J2K_SOC = 0xff4f; /**< SOC marker value */
const uint16_t J2K_SOT = 0xff90; /**< SOT marker value */
const uint16_t J2K_SOD = 0xff93; /**< SOD marker value */
const uint16_t J2K_EOC = 0xffd9; /**< EOC marker value */
const uint16_t J2K_CAP = 0xff50; /**< CAP marker value */
const uint16_t J2K_SIZ = 0xff51; /**< SIZ marker value */
const uint16_t J2K_COD = 0xff52; /**< COD marker value */
const uint16_t J2K_COC = 0xff53; /**< COC marker value */
const uint16_t J2K_RGN = 0xff5e; /**< RGN marker value */
const uint16_t J2K_QCD = 0xff5c; /**< QCD marker value */
const uint16_t J2K_QCC = 0xff5d; /**< QCC marker value */
const uint16_t J2K_POC = 0xff5f; /**< POC marker value */
const uint16_t J2K_TLM = 0xff55; /**< TLM marker value */
const uint16_t J2K_PLM = 0xff57; /**< PLM marker value */
const uint16_t J2K_PLT = 0xff58; /**< PLT marker value */
const uint16_t J2K_PPM = 0xff60; /**< PPM marker value */
const uint16_t J2K_PPT = 0xff61; /**< PPT marker value */
const uint16_t J2K_SOP = 0xff91; /**< SOP marker value */
const uint16_t J2K_EPH = 0xff92; /**< EPH marker value */
const uint16_t J2K_CRG = 0xff63; /**< CRG marker value */
const uint16_t J2K_COM = 0xff64; /**< COM marker value */
const uint16_t J2K_CBD = 0xff78; /**< CBD marker value */
const uint16_t J2K_MCC = 0xff75; /**< MCC marker value */
const uint16_t J2K_MCT = 0xff74; /**< MCT marker value */
const uint16_t J2K_MCO = 0xff77; /**< MCO marker value */
const uint16_t J2K_UNK = 0; /**< UNKNOWN marker value */

// number of bytes needed to store marker
const uint8_t MARKER_BYTES = 2;
// number of bytes neede to store length of marker (excluding marker itself)
const uint8_t MARKER_LENGTH_BYTES = 2;
const uint8_t MARKER_PLUS_MARKER_LENGTH_BYTES = MARKER_BYTES + MARKER_LENGTH_BYTES;

class GrkImage;

template<typename S, typename D>
void j2k_write(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
{
   uint8_t* dest_data = (uint8_t*)p_dest_data;
   S* src_data = (S*)p_src_data;
   for(uint32_t i = 0; i < nb_elem; ++i)
   {
	  D temp = (D) * (src_data++);
	  grk_write<D>(dest_data, temp, sizeof(D));
	  dest_data += sizeof(D);
   }
}

const uint32_t MCT_ELEMENT_SIZE[] = {2, 4, 4, 8};
typedef void (*j2k_mct_function)(const void* p_src_data, void* p_dest_data, uint64_t nb_elem);
typedef std::function<bool(void)> PROCEDURE_FUNC;

struct ICodeStreamCompress
{
   virtual ~ICodeStreamCompress() = default;
   virtual bool init(grk_cparameters* p_param, GrkImage* p_image) = 0;
   virtual bool start(void) = 0;
   virtual uint64_t compress(grk_plugin_tile* tile) = 0;
};

struct ICodeStreamDecompress
{
 public:
   virtual ~ICodeStreamDecompress() = default;
   virtual bool readHeader(grk_header_info* header_info) = 0;
   virtual GrkImage* getImage(uint16_t tileIndex) = 0;
   virtual GrkImage* getImage(void) = 0;
   virtual void init(grk_decompress_core_params* p_param) = 0;
   virtual bool setDecompressRegion(grk_rect_single region) = 0;
   virtual bool decompress(grk_plugin_tile* tile) = 0;
   virtual bool decompressTile(uint16_t tileIndex) = 0;
   virtual bool preProcess(void) = 0;
   virtual bool postProcess(void) = 0;
   virtual void dump(uint32_t flag, FILE* outputFileStream) = 0;
};

class TileCache;

class CodeStream
{
 public:
   CodeStream(BufferedStream* stream);
   virtual ~CodeStream();

   TileProcessor* currentProcessor(void);
   BufferedStream* getStream();
   GrkImage* getHeaderImage(void);
   grk_plugin_tile* getCurrentPluginTile();
   CodingParams* getCodingParams(void);
   static std::string markerString(uint16_t marker);

 protected:
   bool exec(std::vector<PROCEDURE_FUNC>& p_procedure_list);
   CodingParams cp_;
   CodeStreamInfo* codeStreamInfo;
   std::vector<PROCEDURE_FUNC> procedure_list_;
   std::vector<PROCEDURE_FUNC> validation_list_;
   // stores header image information (decompress/compress)
   // decompress: components are subsampled and resolution-reduced
   GrkImage* headerImage_;
   TileProcessor* currentTileProcessor_;
   BufferedStream* stream_;
   std::map<uint32_t, TileProcessor*> processors_;
   grk_plugin_tile* current_plugin_tile;
};

/** @name Exported functions */
/*@{*/
/* ----------------------------------------------------------------------- */

/* ----------------------------------------------------------------------- */
/*@}*/

/*@}*/

} // namespace grk
