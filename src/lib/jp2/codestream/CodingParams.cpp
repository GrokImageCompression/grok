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

#include "grk_includes.h"

namespace grk
{

CodingParams::CodingParams() : rsiz(0), pcap(0), tx0(0), ty0(0), t_width(0), t_height(0),
								num_comments(0), t_grid_width(0), t_grid_height(0),
								ppm_marker(nullptr), tcps(nullptr), tlm_markers(nullptr),
								plm_markers(nullptr)
{
	memset(&m_coding_params, 0, sizeof(m_coding_params));
}
CodingParams::~CodingParams(){
	delete[] tcps;
	for(uint32_t i = 0; i < num_comments; ++i)
		delete[]((uint8_t*)comment[i]);
	num_comments = 0;
	delete plm_markers;
	delete tlm_markers;
	delete ppm_marker;
}

// (canvas coordinates)
grkRectU32 CodingParams::getTileBounds(const GrkImage* p_image, uint32_t tile_x,
									   uint32_t tile_y) const
{
	grkRectU32 rc;

	/* find extent of tile */
	assert(tx0 + (uint64_t)tile_x * t_width < UINT_MAX);
	rc.x0 = std::max<uint32_t>(tx0 + tile_x * t_width, p_image->x0);
	assert(ty0 + (uint64_t)tile_y * t_height < UINT_MAX);
	rc.y0 = std::max<uint32_t>(ty0 + tile_y * t_height, p_image->y0);

	uint64_t temp = tx0 + (uint64_t)(tile_x + 1) * t_width;
	rc.x1 = (temp > p_image->x1) ? p_image->x1 : (uint32_t)temp;

	temp = ty0 + (uint64_t)(tile_y + 1) * t_height;
	rc.y1 = (temp > p_image->y1) ? p_image->y1 : (uint32_t)temp;

	return rc;
}

TileCodingParams::TileCodingParams()
	: csty(0), prg(GRK_PROG_UNKNOWN), numlayers(0), numLayersToDecompress(0), mct(0), numpocs(0),
	  ppt_markers_count(0), ppt_markers(nullptr), ppt_data(nullptr), ppt_buffer(nullptr),
	  ppt_data_size(0), ppt_len(0), main_qcd_qntsty(0), main_qcd_numStepSizes(0), tccps(nullptr),
	  m_tilePartIndex(-1), numTileParts(0), m_compressedTileData(nullptr), mct_norms(nullptr),
	  m_mct_decoding_matrix(nullptr), m_mct_coding_matrix(nullptr), m_mct_records(nullptr),
	  m_nb_mct_records(0), m_nb_max_mct_records(0), m_mcc_records(nullptr), m_nb_mcc_records(0),
	  m_nb_max_mcc_records(0), cod(false), ppt(false), isHT(false)
{
	for(auto i = 0; i < maxCompressLayersGRK; ++i)
		rates[i] = 0.0;
	for(auto i = 0; i < maxCompressLayersGRK; ++i)
		distortion[i] = 0;
	for(auto i = 0; i < 32; ++i)
		memset(progressionOrderChange + i, 0, sizeof(grk_progression));
}

TileCodingParams::~TileCodingParams()
{
	if(ppt_markers != nullptr)
	{
		for(uint32_t i = 0U; i < ppt_markers_count; ++i)
			grkFree(ppt_markers[i].m_data);
		grkFree(ppt_markers);
	}

	delete[] ppt_buffer;
	delete[] tccps;
	grkFree(m_mct_coding_matrix);
	grkFree(m_mct_decoding_matrix);
	if(m_mcc_records)
		grkFree(m_mcc_records);
	if(m_mct_records)
	{
		auto mct_data = m_mct_records;
		for(uint32_t i = 0; i < m_nb_mct_records; ++i)
		{
			grkFree(mct_data->m_data);
			++mct_data;
		}
		grkFree(m_mct_records);
	}
	grkFree(mct_norms);
	delete m_compressedTileData;
}

void TileCodingParams::setIsHT(bool ht)
{
	isHT = ht;
	qcd.setIsHT(ht);
}

bool TileCodingParams::getIsHT(void)
{
	return isHT;
}
uint32_t TileCodingParams::getNumProgressions(){
	return numpocs+1;
}
bool TileCodingParams::hasPoc(void){
	return numpocs > 0;
}
TileComponentCodingParams::TileComponentCodingParams()
	: csty(0), numresolutions(0), cblkw(0), cblkh(0), cblk_sty(0), qmfbid(0),
	  quantizationMarkerSet(false), fromQCC(false), fromTileHeader(false), qntsty(0),
	  numStepSizes(0), numgbits(0), roishift(0), m_dc_level_shift(0)
{
	for(uint32_t i = 0; i < GRK_J2K_MAXRLVLS; ++i)
	{
		precinctWidthExp[i] = 0;
		precinctHeightExp[i] = 0;
	}
}

DecompressorState::DecompressorState()
	: m_default_tcp(nullptr), m_start_tile_x_index(0), m_start_tile_y_index(0),
	  m_end_tile_x_index(0), m_end_tile_y_index(0), lastSotReadPosition(0),
	  lastTilePartInCodeStream(false), lastTilePartWasRead(false), skipTileData(false),
	  m_state(J2K_DEC_STATE_NONE)
{}

uint16_t DecompressorState::getState(void)
{
	return m_state;
}
void DecompressorState::setState(uint16_t state)
{
	m_state = state;
}
void DecompressorState::orState(uint16_t state)
{
	m_state |= state;
}
void DecompressorState::andState(uint16_t state)
{
	m_state &= state;
}
bool DecompressorState::findNextTile(CodeStreamDecompress* codeStream)
{
	auto stream = codeStream->getStream();
	lastTilePartWasRead = false;
	andState((uint16_t)(~J2K_DEC_STATE_DATA));

	// if there is no EOC marker and there is also no data left, then simply return true
	if(stream->numBytesLeft() == 0 && getState() == J2K_DEC_STATE_NO_EOC)
		return true;

	// if EOC marker has not been read yet, then try to read the next marker
	// (should be EOC or SOT)
	if(getState() != J2K_DEC_STATE_EOC)
	{
		try
		{
			if(!codeStream->readMarker())
			{
				GRK_WARN("findNextTile: Not enough data to read another marker.\n"
						 "Tile may be truncated.");
				return true;
			}
		}
		catch(InvalidMarkerException& ume)
		{
			GRK_UNUSED(ume);
			setState(J2K_DEC_STATE_NO_EOC);
			GRK_WARN("findNextTile: expected EOC or SOT "
					 "but found invalid marker 0x%x.",
					 codeStream->getCurrentMarker());
			throw DecodeUnknownMarkerAtEndOfTileException();
		}

		switch(codeStream->getCurrentMarker())
		{
			// we found the EOC marker - set state accordingly and return true;
			// we can ignore all data after EOC
			case J2K_MS_EOC:
				setState(J2K_DEC_STATE_EOC);
				return true;
				break;
			// start of another tile
			case J2K_MS_SOT:
				return true;
				break;
			default: {
				auto bytesLeft = stream->numBytesLeft();
				setState(J2K_DEC_STATE_NO_EOC);
				GRK_WARN("findNextTile: expected EOC or SOT "
						 "but found marker 0x%x.\nIgnoring %d bytes "
						 "remaining in the stream.",
						 codeStream->getCurrentMarker(), bytesLeft + 2);
				throw DecodeUnknownMarkerAtEndOfTileException();
			}
			break;
		}
	}

	return true;
}

} // namespace grk
