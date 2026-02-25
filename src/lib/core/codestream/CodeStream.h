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

#include <unordered_map>
#include <functional>

#include "CodingParams.h"

namespace grk
{
const uint32_t default_numbers_segments = 10;
const uint32_t default_header_size = 4096;
const uint32_t default_number_mcc_records = 10;
const uint32_t default_number_mct_records = 10;

// includes marker and marker length (4 bytes)
const uint32_t sotMarkerSegmentLen = 12U;

const uint32_t SPCodSPCocLen = 5U;
const uint32_t codSocLen = 5U;
const uint32_t tlmMarkerBytesPerTilePart = 6;

const uint32_t GRK_COMP_PARAM_DEFAULT_CBLOCKW = 64;
const uint32_t GRK_COMP_PARAM_DEFAULT_CBLOCKH = 64;
const GRK_PROG_ORDER GRK_DEFAULT_PROG_ORDER = GRK_LRCP;
const uint32_t GRK_DEFAULT_NUMRESOLUTION = 6;

const uint8_t CP_CSTY_PRT = 0x01; // custom precinct values are set
const uint8_t CP_CSTY_SOP = 0x02; // SOP markers are used
const uint8_t CP_CSTY_EPH = 0x04; // EPH markers are used
const uint8_t CCP_CSTY_PRECINCT = 0x01; // custom precinct values are set
const uint8_t CCP_QNTSTY_NOQNT = 0x00; // no quantization
const uint8_t CCP_QNTSTY_SIQNT = 0x01; // derived quantization
const uint8_t CCP_QNTSTY_SEQNT = 0x02; // expounded quantization

const uint16_t SOC = 0xff4f; /** SOC marker */
const uint16_t SOT = 0xff90; /** SOT marker */
const uint16_t SOD = 0xff93; /** SOD marker */
const uint16_t EOC = 0xffd9; /** EOC marker */
const uint16_t CAP = 0xff50; /** CAP marker */
const uint16_t SIZ = 0xff51; /** SIZ marker */
const uint16_t COD = 0xff52; /** COD marker */
const uint16_t COC = 0xff53; /** COC marker */
const uint16_t RGN = 0xff5e; /** RGN marker */
const uint16_t QCD = 0xff5c; /** QCD marker */
const uint16_t QCC = 0xff5d; /** QCC marker */
const uint16_t POC = 0xff5f; /** POC marker */
const uint16_t TLM = 0xff55; /** TLM marker */
const uint16_t PLM = 0xff57; /** PLM marker */
const uint16_t PLT = 0xff58; /** PLT marker */
const uint16_t PPM = 0xff60; /** PPM marker */
const uint16_t PPT = 0xff61; /** PPT marker */
const uint16_t SOP = 0xff91; /** SOP marker */
const uint16_t EPH = 0xff92; /** EPH marker */
const uint16_t CRG = 0xff63; /** CRG marker */
const uint16_t COM = 0xff64; /** COM marker */
const uint16_t CBD = 0xff78; /** CBD marker */
const uint16_t MCC = 0xff75; /** MCC marker */
const uint16_t MCT = 0xff74; /** MCT marker */
const uint16_t MCO = 0xff77; /** MCO marker */
const uint16_t UNK = 0; /** UNKNOWN marker */

// number of bytes needed to store marker
const uint8_t MARKER_BYTES = 2;

// number of bytes needed to store length of marker (excluding marker itself)
const uint8_t MARKER_LENGTH_BYTES = 2;
const uint8_t MARKER_BYTES_PLUS_MARKER_LENGTH_BYTES = MARKER_BYTES + MARKER_LENGTH_BYTES;

class GrkImage;

template<typename S, typename D>
void write(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
{
  uint8_t* dest_data = (uint8_t*)p_dest_data;
  S* src_data = (S*)p_src_data;
  for(uint32_t i = 0; i < nb_elem; ++i)
  {
    D temp = (D) * (src_data++);
    grk_write(dest_data, temp);
    dest_data += sizeof(D);
  }
}

const uint32_t MCT_ELEMENT_SIZE[] = {2, 4, 4, 8};
typedef std::function<bool(void)> PROCEDURE_FUNC;

class TileCache;

class CodeStream
{
public:
  CodeStream(IStream* stream);
  virtual ~CodeStream();

  IStream* getStream();
  GrkImage* getHeaderImage(void);
  grk_plugin_tile* getCurrentPluginTile();
  CodingParams* getCodingParams(void);

protected:
  bool exec(std::vector<PROCEDURE_FUNC>& procedureList);
  CodingParams cp_;
  std::vector<PROCEDURE_FUNC> procedureList_;
  std::vector<PROCEDURE_FUNC> validationList_;
  // stores header image information (decompress/compress)
  // decompress: components are subsampled and resolution-reduced
  GrkImage* headerImage_;
  IStream* stream_;
  std::unordered_map<uint32_t, ITileProcessor*> processors_;
  grk_plugin_tile* current_plugin_tile;
};

} // namespace grk
