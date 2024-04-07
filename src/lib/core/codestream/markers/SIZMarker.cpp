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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "grk_includes.h"

namespace grk
{
/**
 * Apply resolution reduction to header image components
 *
 * @param headerImage	header image
 * @param p_cp			the coding parameters from which to update the image.
 */
void SIZMarker::subsampleAndReduceHeaderImageComponents(GrkImage* headerImage,
														const CodingParams* p_cp)
{
   // 1. calculate canvas coordinates of image
   uint32_t x0 = std::max<uint32_t>(p_cp->tx0, headerImage->x0);
   uint32_t y0 = std::max<uint32_t>(p_cp->ty0, headerImage->y0);

   /* validity of p_cp members used here checked in j2k_read_siz. Can't overflow. */
   uint32_t x1 = p_cp->tx0 + (p_cp->t_grid_width - 1U) * p_cp->t_width;
   uint32_t y1 = p_cp->ty0 + (p_cp->t_grid_height - 1U) * p_cp->t_height;

   /* (use saturated add to prevent overflow) */
   x1 = std::min<uint32_t>(satAdd<uint32_t>(x1, p_cp->t_width), headerImage->x1);
   y1 = std::min<uint32_t>(satAdd<uint32_t>(y1, p_cp->t_height), headerImage->y1);

   auto imageBounds = grk_rect32(x0, y0, x1, y1);

   // 2. sub-sample and apply resolution reduction
   uint32_t reduce = p_cp->coding_params_.dec_.reduce_;
   for(uint32_t i = 0; i < headerImage->numcomps; ++i)
   {
	  auto comp = headerImage->comps + i;
	  auto compBounds = imageBounds.scaleDownCeil(comp->dx, comp->dy);
	  comp->w = ceildivpow2<uint32_t>(compBounds.width(), reduce);
	  comp->h = ceildivpow2<uint32_t>(compBounds.height(), reduce);
	  comp->x0 = ceildivpow2<uint32_t>(compBounds.x0, reduce);
	  comp->y0 = ceildivpow2<uint32_t>(compBounds.y0, reduce);
   }
}

bool SIZMarker::read(CodeStreamDecompress* codeStream, uint8_t* headerData, uint16_t header_size)
{
   assert(codeStream != nullptr);
   assert(headerData != nullptr);

   uint32_t numComps;
   uint32_t nb_comp_remain;
   uint32_t remaining_size;
   uint16_t numTiles;
   auto decompressState = codeStream->getDecompressorState();
   auto image = codeStream->getHeaderImage();
   auto cp = codeStream->getCodingParams();

   /* minimum size == 39 - 3 (= minimum component parameter) */
   if(header_size < 36)
   {
	  Logger::logger_.error("Error with SIZ marker size");
	  return false;
   }

   remaining_size = header_size - 36U;
   numComps = remaining_size / 3;
   nb_comp_remain = remaining_size % 3;
   if(nb_comp_remain != 0)
   {
	  Logger::logger_.error("Error with SIZ marker size");
	  return false;
   }

   uint16_t tmp;
   grk_read(headerData, &tmp); /* Rsiz (capabilities) */
   headerData += 2;
   // sanity check on RSIZ
   if(tmp & GRK_PROFILE_PART2)
   {
	  // no sanity check on part 2 profile at the moment
	  // profile = GRK_PROFILE_PART2;
	  // uint16_t part2_extensions = tmp & GRK_PROFILE_PART2_EXTENSIONS_MASK;
   }
   else
   {
	  if(tmp & 0x3000)
	  {
		 Logger::logger_.warn("SIZ marker segment's Rsiz word must have bits 12 and 13 equal to 0");
		 Logger::logger_.warn("unless the Part-2 flag (bit-15) is set.");
	  }
	  uint16_t profile = tmp & GRK_PROFILE_MASK;
	  if((profile > GRK_PROFILE_CINEMA_LTS) && !GRK_IS_BROADCAST(profile) && !GRK_IS_IMF(profile))
	  {
		 Logger::logger_.error("Non-compliant Rsiz value 0x%x in SIZ marker", tmp);
		 return false;
	  }
   }

   cp->rsiz = tmp;
   grk_read(headerData, &image->x1); /* Xsiz */
   headerData += 4;
   grk_read(headerData, &image->y1); /* Ysiz */
   headerData += 4;
   grk_read(headerData, &image->x0); /* X0siz */
   headerData += 4;
   grk_read(headerData, &image->y0); /* Y0siz */
   headerData += 4;
   grk_read(headerData, &cp->t_width); /* XTsiz */
   headerData += 4;
   grk_read(headerData, &cp->t_height); /* YTsiz */
   headerData += 4;
   grk_read(headerData, &cp->tx0); /* XT0siz */
   headerData += 4;
   grk_read(headerData, &cp->ty0); /* YT0siz */
   headerData += 4;
   grk_read(headerData, &tmp); /* Csiz */
   headerData += 2;
   if(tmp == 0)
   {
	  Logger::logger_.error("SIZ marker: number of components cannot be zero");
	  return false;
   }
   if(tmp <= maxNumComponentsJ2K)
	  image->numcomps = tmp;
   else
   {
	  Logger::logger_.error(
		  "SIZ marker: number of components %u is greater than maximum allowed number of "
		  "components %u",
		  tmp, maxNumComponentsJ2K);
	  return false;
   }
   if(image->numcomps != numComps)
   {
	  Logger::logger_.error(
		  "SIZ marker: signalled number of components is not compatible with remaining "
		  "number of components ( %u vs %u)",
		  image->numcomps, numComps);
	  return false;
   }
   if((image->x0 >= image->x1) || (image->y0 >= image->y1))
   {
	  std::stringstream ss;
	  ss << "SIZ marker: negative or zero image dimensions (" << (int64_t)image->x1 - image->x0
		 << " x " << (int64_t)image->y1 - image->y0 << ")";
	  Logger::logger_.error("%s", ss.str().c_str());
	  return false;
   }
   if((cp->t_width == 0U) || (cp->t_height == 0U))
   {
	  Logger::logger_.error("SIZ marker: invalid tile size (%u, %u)", cp->t_width, cp->t_height);
	  return false;
   }
   if(cp->tx0 > image->x0 || cp->ty0 > image->y0)
   {
	  Logger::logger_.error("SIZ marker: tile origin (%u,%u) cannot lie in the region"
							" to the right and bottom of image origin (%u,%u)",
							cp->tx0, cp->ty0, image->x0, image->y0);
	  return false;
   }
   uint32_t tx1 = satAdd<uint32_t>(cp->tx0, cp->t_width);
   uint32_t ty1 = satAdd<uint32_t>(cp->ty0, cp->t_height);
   if(tx1 <= image->x0 || ty1 <= image->y0)
   {
	  Logger::logger_.error("SIZ marker: first tile (%u,%u,%u,%u) must overlap"
							" image (%u,%u,%u,%u)",
							cp->tx0, cp->ty0, tx1, ty1, image->x0, image->y0, image->x1, image->y1);
	  return false;
   }
   image->comps = new grk_image_comp[image->numcomps];
   memset(image->comps, 0, image->numcomps * sizeof(grk_image_comp));
   auto img_comp = image->comps;

   /* Read the component information */
   for(uint16_t i = 0; i < image->numcomps; ++i)
   {
	  uint8_t val;
	  grk_read(headerData++, &val); /* Ssiz_i */
	  img_comp->prec = (uint8_t)((val & 0x7f) + 1);
	  img_comp->sgnd = val >> 7;
	  grk_read(headerData++, &val); /* XRsiz_i */
	  img_comp->dx = val; /* should be between 1 and 255 */
	  grk_read(headerData++, &val); /* YRsiz_i */
	  img_comp->dy = val; /* should be between 1 and 255 */
	  if(img_comp->dx == 0 || img_comp->dy == 0)
	  {
		 Logger::logger_.error("Invalid values for comp = %u : dx=%u dy=%u\n (should be positive "
							   "according to the JPEG2000 standard)",
							   i, img_comp->dx, img_comp->dy);
		 return false;
	  }

	  if(img_comp->prec == 0 || img_comp->prec > GRK_MAX_SUPPORTED_IMAGE_PRECISION)
	  {
		 Logger::logger_.error(
			 "Unsupported precision for comp = %u : prec=%u (this library only supports "
			 "precisions between 1 and %u)",
			 i, img_comp->prec, GRK_MAX_SUPPORTED_IMAGE_PRECISION);
		 return false;
	  }
	  ++img_comp;
   }
   /* Compute the number of tiles */
   uint32_t t_grid_width = ceildiv<uint32_t>(image->x1 - cp->tx0, cp->t_width);
   uint32_t t_grid_height = ceildiv<uint32_t>(image->y1 - cp->ty0, cp->t_height);
   if(t_grid_width == 0 || t_grid_height == 0)
   {
	  Logger::logger_.error(
		  "Invalid grid of tiles: %u x %u. JPEG 2000 standard requires at least one tile "
		  "in grid. ",
		  t_grid_width, t_grid_height);
	  return false;
   }
   if((uint64_t)t_grid_width * t_grid_height > (uint64_t)maxNumTilesJ2K)
   {
	  Logger::logger_.error(
		  "Invalid grid of tiles : %u x %u.  JPEG 2000 standard specifies maximum of %u tiles",
		  t_grid_width, t_grid_height, maxNumTilesJ2K);
	  return false;
   }
   cp->t_grid_width = (uint16_t)t_grid_width;
   cp->t_grid_height = (uint16_t)t_grid_height;
   numTiles = cp->t_grid_width * cp->t_grid_height;
   decompressState->tilesToDecompress_.init(grk_rect16(0, 0, cp->t_grid_width, cp->t_grid_height));
   cp->tcps = new TileCodingParams[numTiles];
   decompressState->default_tcp_->tccps = new TileComponentCodingParams[image->numcomps];
   decompressState->default_tcp_->mct_records_ =
	   (grk_mct_data*)grk_calloc(default_number_mct_records, sizeof(grk_mct_data));
   if(!decompressState->default_tcp_->mct_records_)
   {
	  Logger::logger_.error("Not enough memory to take in charge SIZ marker");
	  return false;
   }
   decompressState->default_tcp_->nb_max_mct_records_ = default_number_mct_records;
   decompressState->default_tcp_->mcc_records_ = (grk_simple_mcc_decorrelation_data*)grk_calloc(
	   default_number_mcc_records, sizeof(grk_simple_mcc_decorrelation_data));
   if(!decompressState->default_tcp_->mcc_records_)
   {
	  Logger::logger_.error("Not enough memory to take in charge SIZ marker");
	  return false;
   }
   decompressState->default_tcp_->nb_max_mcc_records_ = default_number_mcc_records;
   /* set up default dc level shift */
   for(uint16_t i = 0; i < image->numcomps; ++i)
	  if(!image->comps[i].sgnd)
		 decompressState->default_tcp_->tccps[i].dc_level_shift_ = 1 << (image->comps[i].prec - 1);
   for(uint16_t i = 0; i < numTiles; ++i)
	  (cp->tcps + i)->tccps = new TileComponentCodingParams[image->numcomps];
   decompressState->setState(DECOMPRESS_STATE_MH);
   subsampleAndReduceHeaderImageComponents(image, cp);

   return true;
}

bool SIZMarker::write(CodeStreamCompress* codeStream, BufferedStream* stream)
{
   uint32_t i;
   uint32_t size_len;

   assert(stream != nullptr);
   assert(codeStream != nullptr);

   auto image = codeStream->getHeaderImage();
   auto cp = codeStream->getCodingParams();
   size_len = 40 + 3U * image->numcomps;
   /* write SOC identifier */

   /* SIZ */
   if(!stream->writeShort(J2K_MS_SIZ))
	  return false;

   /* L_SIZ */
   if(!stream->writeShort((uint16_t)(size_len - MARKER_BYTES)))
	  return false;
   /* Rsiz (capabilities) */
   if(!stream->writeShort(cp->rsiz))
	  return false;
   /* Xsiz */
   if(!stream->writeInt(image->x1))
	  return false;
   /* Ysiz */
   if(!stream->writeInt(image->y1))
	  return false;
   /* X0siz */
   if(!stream->writeInt(image->x0))
	  return false;
   /* Y0siz */
   if(!stream->writeInt(image->y0))
	  return false;
   /* XTsiz */
   if(!stream->writeInt(cp->t_width))
	  return false;
   /* YTsiz */
   if(!stream->writeInt(cp->t_height))
	  return false;
   /* XT0siz */
   if(!stream->writeInt(cp->tx0))
	  return false;
   /* YT0siz */
   if(!stream->writeInt(cp->ty0))
	  return false;
   /* Csiz */
   if(!stream->writeShort((uint16_t)image->numcomps))
	  return false;
   for(i = 0; i < image->numcomps; ++i)
   {
	  auto comp = image->comps + i;
	  /* TODO here with MCT ? */
	  uint8_t bpc = (uint8_t)(comp->prec - 1);
	  if(comp->sgnd)
		 bpc = (uint8_t)(bpc + (1 << 7));
	  if(!stream->writeByte(bpc))
		 return false;
	  /* XRsiz_i */
	  if(!stream->writeByte((uint8_t)comp->dx))
		 return false;
	  /* YRsiz_i */
	  if(!stream->writeByte((uint8_t)comp->dy))
		 return false;
   }

   return true;
}

} // namespace grk
