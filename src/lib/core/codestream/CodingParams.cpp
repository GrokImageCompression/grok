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

#include "grk_includes.h"

namespace grk
{
CodingParams::CodingParams()
	: rsiz(0), pcap(0), tx0(0), ty0(0), t_width(0), t_height(0), num_comments(0), t_grid_width(0),
	  t_grid_height(0), ppm_marker(nullptr), tcps(nullptr), tlm_markers(nullptr),
	  plm_markers(nullptr), wholeTileDecompress_(true)
{
   memset(&coding_params_, 0, sizeof(coding_params_));
}
CodingParams::~CodingParams()
{
   delete[] tcps;
   for(uint32_t i = 0; i < num_comments; ++i)
	  delete[] comment[i];
   num_comments = 0;
   delete plm_markers;
   delete tlm_markers;
   delete ppm_marker;
}

// (canvas coordinates)
grk_rect32 CodingParams::getTileBounds(const GrkImage* p_image, uint32_t tile_x,
									   uint32_t tile_y) const
{
   grk_rect32 rc;

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
	: csty(0), prg(GRK_PROG_UNKNOWN), num_layers_(0), numLayersToDecompress(0), mct(0), numpocs(0),
	  ppt_markers_count(0), ppt_markers(nullptr), ppt_data(nullptr), ppt_buffer(nullptr),
	  ppt_data_size(0), ppt_len(0), main_qcd_qntsty(0), main_qcd_numStepSizes(0), tccps(nullptr),
	  tilePartCounter_(0), numTileParts_(0), compressedTileData_(nullptr), mct_norms(nullptr),
	  mct_decoding_matrix_(nullptr), mct_coding_matrix_(nullptr), mct_records_(nullptr),
	  nb_mct_records_(0), nb_max_mct_records_(0), mcc_records_(nullptr), nb_mcc_records_(0),
	  nb_max_mcc_records_(0), cod(false), ppt(false), qcd_(nullptr), ht_(false)
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
		 grk_free(ppt_markers[i].data_);
	  grk_free(ppt_markers);
   }

   delete[] ppt_buffer;
   delete[] tccps;
   grk_free(mct_coding_matrix_);
   grk_free(mct_decoding_matrix_);
   if(mcc_records_)
	  grk_free(mcc_records_);
   if(mct_records_)
   {
	  auto mct_data = mct_records_;
	  for(uint32_t i = 0; i < nb_mct_records_; ++i)
	  {
		 grk_free(mct_data->data_);
		 ++mct_data;
	  }
	  grk_free(mct_records_);
   }
   grk_free(mct_norms);
   delete compressedTileData_;
   delete qcd_;
}

bool TileCodingParams::advanceTilePartCounter(uint16_t tile_index, uint8_t tilePartIndex)
{
   /* We must avoid reading the same tile part number twice for a given tile */
   /* to avoid various issues, like grk_j2k_merge_ppt being called several times. */
   /* ISO 15444-1 A.4.2 Start of tile-part (SOT) mandates that tile parts */
   /* should appear in increasing order. */
   if(uint8_t(tilePartCounter_) != tilePartIndex)
   {
	  Logger::logger_.error("Invalid tile part index for tile number %u. "
							"Got %u, expected %u",
							tile_index, tilePartIndex, tilePartCounter_);
	  return false;
   }
   tilePartCounter_++;

   return true;
}

bool TileCodingParams::copy(const TileCodingParams* rhs, const GrkImage* image)
{
   uint32_t tccp_size = image->numcomps * (uint32_t)sizeof(TileComponentCodingParams);
   uint64_t mct_size = (uint64_t)image->numcomps * image->numcomps * sizeof(float);

   // cache tccps
   auto cachedTccps = tccps;
   auto cachedQcd = qcd_;
   *this = *rhs;
   /* Initialize some values of the current tile coding parameters*/
   cod = false;
   ppt = false;
   ppt_data = nullptr;
   /* Remove memory not owned by this tile in case of early error return. */
   mct_decoding_matrix_ = nullptr;
   nb_max_mct_records_ = 0;
   mct_records_ = nullptr;
   nb_max_mcc_records_ = 0;
   mcc_records_ = nullptr;
   // restore tccps
   tccps = cachedTccps;
   qcd_ = cachedQcd;

   /* Get the mct_decoding_matrix of the dflt_tile_cp and copy them into the current tile cp*/
   if(rhs->mct_decoding_matrix_)
   {
	  mct_decoding_matrix_ = (float*)grk_malloc(mct_size);
	  if(!mct_decoding_matrix_)
		 return false;
	  memcpy(mct_decoding_matrix_, rhs->mct_decoding_matrix_, mct_size);
   }

   /* Get the mct_record of the dflt_tile_cp and copy them into the current tile cp*/
   uint32_t mct_records_size = rhs->nb_max_mct_records_ * (uint32_t)sizeof(grk_mct_data);
   mct_records_ = (grk_mct_data*)grk_malloc(mct_records_size);
   if(!mct_records_)
	  return false;
   memcpy(mct_records_, rhs->mct_records_, mct_records_size);

   /* Copy the mct record data from dflt_tile_cp to the current tile*/

   for(uint32_t j = 0; j < rhs->nb_mct_records_; ++j)
   {
	  auto src_mct_rec = rhs->mct_records_ + j;
	  auto dest_mct_rec = mct_records_ + j;
	  if(src_mct_rec->data_)
	  {
		 dest_mct_rec->data_ = (uint8_t*)grk_malloc(src_mct_rec->data_size_);
		 if(!dest_mct_rec->data_)
			return false;
		 memcpy(dest_mct_rec->data_, src_mct_rec->data_, src_mct_rec->data_size_);
	  }
	  /* Update with each pass to free exactly what has been allocated on early return. */
	  nb_max_mct_records_ += 1;
   }

   /* Get the mcc_record of the dflt_tile_cp and copy them into the current tile cp*/
   uint32_t mcc_records_size =
	   rhs->nb_max_mcc_records_ * (uint32_t)sizeof(grk_simple_mcc_decorrelation_data);
   mcc_records_ = (grk_simple_mcc_decorrelation_data*)grk_malloc(mcc_records_size);
   if(!mcc_records_)
	  return false;
   memcpy(mcc_records_, rhs->mcc_records_, mcc_records_size);
   nb_max_mcc_records_ = rhs->nb_max_mcc_records_;

   /* Copy the mcc record data from dflt_tile_cp to the current tile*/
   for(uint32_t j = 0; j < rhs->nb_max_mcc_records_; ++j)
   {
	  auto src_mcc_rec = rhs->mcc_records_ + j;
	  auto dest_mcc_rec = mcc_records_ + j;
	  if(src_mcc_rec->decorrelation_array_)
	  {
		 uint32_t offset = (uint32_t)(src_mcc_rec->decorrelation_array_ - rhs->mct_records_);
		 dest_mcc_rec->decorrelation_array_ = mct_records_ + offset;
	  }
	  if(src_mcc_rec->offset_array_)
	  {
		 uint32_t offset = (uint32_t)(src_mcc_rec->offset_array_ - rhs->mct_records_);
		 dest_mcc_rec->offset_array_ = mct_records_ + offset;
	  }
   }
   memcpy(tccps, rhs->tccps, tccp_size);

   return true;
}

void TileCodingParams::setIsHT(bool ht, bool reversible, uint8_t guardBits)
{
   ht_ = ht;
   if(!qcd_)
	  qcd_ = T1Factory::makeQuantizer(ht, reversible, guardBits);
}

bool TileCodingParams::isHT(void)
{
   return ht_;
}
uint32_t TileCodingParams::getNumProgressions()
{
   return numpocs + 1;
}
bool TileCodingParams::hasPoc(void)
{
   return numpocs > 0;
}
TileComponentCodingParams::TileComponentCodingParams()
	: csty(0), numresolutions(0), cblkw(0), cblkh(0), cblk_sty(0), qmfbid(0),
	  quantizationMarkerSet(false), fromQCC(false), fromTileHeader(false), qntsty(0),
	  numStepSizes(0), numgbits(0), roishift(0), dc_level_shift_(0)
{
   for(uint32_t i = 0; i < GRK_MAXRLVLS; ++i)
   {
	  precWidthExp[i] = 0;
	  precHeightExp[i] = 0;
   }
}

DecompressorState::DecompressorState()
	: default_tcp_(nullptr), lastSotReadPosition(0), lastTilePartInCodeStream(false),
	  state_(DECOMPRESS_STATE_NONE)
{}

uint16_t DecompressorState::getState(void)
{
   return state_;
}
void DecompressorState::setState(uint16_t state)
{
   state_ = state;
}
void DecompressorState::orState(uint16_t state)
{
   state_ |= state;
}
void DecompressorState::andState(uint16_t state)
{
   state_ &= state;
}
void DecompressorState::setComplete(uint16_t tile_index)
{
   tilesToDecompress_.setComplete(tile_index);
}
// parse stream until EOC or next SOT
bool DecompressorState::findNextSOT(CodeStreamDecompress* codeStream)
{
   auto stream = codeStream->getStream();
   andState((uint16_t)(~DECOMPRESS_STATE_DATA));

   // if there is no EOC marker and there is also no data left, then simply return true
   if(stream->numBytesLeft() == 0 && getState() == DECOMPRESS_STATE_NO_EOC)
	  return true;

   // if EOC marker has not been read yet, then try to read the next marker
   // (should be EOC or SOT)
   if(getState() != DECOMPRESS_STATE_EOC)
   {
	  try
	  {
		 if(!codeStream->readMarker())
		 {
			Logger::logger_.warn("findNextTile: Not enough data to read another marker.\n"
								 "Tile may be truncated.");
			return true;
		 }
	  }
	  catch([[maybe_unused]] const InvalidMarkerException& ume)
	  {
		 setState(DECOMPRESS_STATE_NO_EOC);
		 Logger::logger_.warn("findNextTile: expected EOC or SOT "
							  "but found invalid marker 0x%x.",
							  codeStream->getCurrentMarker());
		 throw DecodeUnknownMarkerAtEndOfTileException();
	  }

	  switch(codeStream->getCurrentMarker())
	  {
		 // we found the EOC marker - set state accordingly and return true;
		 // we can ignore all data after EOC
		 case J2K_EOC:
			setState(DECOMPRESS_STATE_EOC);
			return true;
			break;
		 // start of another tile
		 case J2K_SOT:
			return true;
			break;
		 default: {
			auto bytesLeft = stream->numBytesLeft();
			setState(DECOMPRESS_STATE_NO_EOC);
			Logger::logger_.warn("findNextTile: expected EOC or SOT "
								 "but found marker 0x%x.\nIgnoring %u bytes "
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
