/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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

#include <vector>
#include <map>
#include "CodingParams.h"
#include <map>

namespace grk
{
const uint32_t default_numbers_segments = 10;
const uint32_t default_header_size = 4096;
const uint32_t default_number_mcc_records = 10;
const uint32_t default_number_mct_records = 10;

// includes marker and marker length (4 bytes)
const uint32_t sot_marker_segment_len = 12U;
const uint32_t grk_marker_length = 4U;

const uint32_t SPCod_SPCoc_len = 5U;
const uint32_t cod_coc_len = 5U;
const uint32_t tlmMarkerBytesPerTilePart = 6;

const uint32_t GRK_COMP_PARAM_DEFAULT_CBLOCKW = 64;
const uint32_t GRK_COMP_PARAM_DEFAULT_CBLOCKH = 64;
const GRK_PROG_ORDER GRK_COMP_PARAM_DEFAULT_PROG_ORDER = GRK_LRCP;
const uint32_t GRK_COMP_PARAM_DEFAULT_NUMRESOLUTION = 6;

#define J2K_CP_CSTY_PRT 0x01
#define J2K_CP_CSTY_SOP 0x02
#define J2K_CP_CSTY_EPH 0x04
#define J2K_CCP_CSTY_PRT 0x01
#define J2K_CCP_QNTSTY_NOQNT 0 // no quantization
#define J2K_CCP_QNTSTY_SIQNT 1 // derived quantization
#define J2K_CCP_QNTSTY_SEQNT 2 // expounded quantization

#define J2K_MS_SOC 0xff4f /**< SOC marker value */
#define J2K_MS_SOT 0xff90 /**< SOT marker value */
#define J2K_MS_SOD 0xff93 /**< SOD marker value */
#define J2K_MS_EOC 0xffd9 /**< EOC marker value */
#define J2K_MS_CAP 0xff50 /**< CAP marker value */
#define J2K_MS_SIZ 0xff51 /**< SIZ marker value */
#define J2K_MS_COD 0xff52 /**< COD marker value */
#define J2K_MS_COC 0xff53 /**< COC marker value */
#define J2K_MS_RGN 0xff5e /**< RGN marker value */
#define J2K_MS_QCD 0xff5c /**< QCD marker value */
#define J2K_MS_QCC 0xff5d /**< QCC marker value */
#define J2K_MS_POC 0xff5f /**< POC marker value */
#define J2K_MS_TLM 0xff55 /**< TLM marker value */
#define J2K_MS_PLM 0xff57 /**< PLM marker value */
#define J2K_MS_PLT 0xff58 /**< PLT marker value */
#define J2K_MS_PPM 0xff60 /**< PPM marker value */
#define J2K_MS_PPT 0xff61 /**< PPT marker value */
#define J2K_MS_SOP 0xff91 /**< SOP marker value */
#define J2K_MS_EPH 0xff92 /**< EPH marker value */
#define J2K_MS_CRG 0xff63 /**< CRG marker value */
#define J2K_MS_COM 0xff64 /**< COM marker value */
#define J2K_MS_CBD 0xff78 /**< CBD marker value */
#define J2K_MS_MCC 0xff75 /**< MCC marker value */
#define J2K_MS_MCT 0xff74 /**< MCT marker value */
#define J2K_MS_MCO 0xff77 /**< MCO marker value */
#define J2K_MS_UNK 0 /**< UNKNOWN marker value */

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
	virtual bool initCompress(grk_cparameters* p_param, GrkImage* p_image) = 0;
	virtual bool startCompress(void) = 0;
	virtual bool compress(grk_plugin_tile* tile) = 0;
	virtual bool compressTile(uint16_t tileIndex, uint8_t* p_data, uint64_t data_size) = 0;
	virtual bool endCompress(void) = 0;
};

struct ICodeStreamDecompress
{
  public:
	virtual ~ICodeStreamDecompress() = default;
	virtual bool readHeader(grk_header_info* header_info) = 0;
	virtual GrkImage* getImage(uint16_t tileIndex) = 0;
	virtual GrkImage* getImage(void) = 0;
	virtual void initDecompress(grk_dparameters* p_param) = 0;
	virtual bool setDecompressWindow(grkRectU32 window) = 0;
	virtual bool decompress(grk_plugin_tile* tile) = 0;
	virtual bool decompressTile(uint16_t tileIndex) = 0;
	virtual bool endDecompress(void) = 0;
	virtual void dump(uint32_t flag, FILE* outputFileStream) = 0;
};

class TileCache;

class CodeStream
{
  public:
	CodeStream(IBufferedStream* stream);
	virtual ~CodeStream();

	TileProcessor* currentProcessor(void);
	IBufferedStream* getStream();
	GrkImage* getHeaderImage(void);
	grk_plugin_tile* getCurrentPluginTile();
	CodingParams* getCodingParams(void);

  protected:
	bool exec(std::vector<PROCEDURE_FUNC>& p_procedure_list);
	CodingParams m_cp;
	CodeStreamInfo* codeStreamInfo;
	std::vector<PROCEDURE_FUNC> m_procedure_list;
	std::vector<PROCEDURE_FUNC> m_validation_list;
	// stores header image information (decompress/compress)
	// decompress: components are subsampled and resolution-reduced
	GrkImage* m_headerImage;
	TileProcessor* m_currentTileProcessor;
	IBufferedStream* m_stream;
	std::map<uint32_t, TileProcessor*> m_processors;
	bool m_multiTile;
	grk_plugin_tile* current_plugin_tile;
};

/** @name Exported functions */
/*@{*/
/* ----------------------------------------------------------------------- */

/* ----------------------------------------------------------------------- */
/*@}*/

/*@}*/

} // namespace grk
