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

#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "grk_includes.h"
#include "StreamIO.h"
#include "MarkerCache.h"
#include "FetchCommon.h"
#include "TPFetchSeq.h"
#include "GrkImageMeta.h"
#include "GrkImage.h"
#include "ICompressor.h"
#include "IDecompressor.h"
#include "MarkerParser.h"
#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct TileProcessor;
}
#include "CodeStream.h"
#include "PacketLengthCache.h"
#include "ICoder.h"
#include "CoderPool.h"
#include "CodeblockCompress.h"
#include "CodecScheduler.h"
#include "TileProcessor.h"
#include "TileProcessorCompress.h"
#include "CodeStreamCompress.h"
#include "TileCache.h"
#include "TileCompletion.h"
#include "CodeStreamDecompress.h"

namespace grk
{

bool SIZMarker::read(CodeStreamDecompress* codeStream, uint8_t* headerData, uint16_t headerSize)
{
  assert(codeStream != nullptr);
  assert(headerData != nullptr);

  uint16_t numComps;
  uint32_t nb_comp_remain;
  uint32_t remaining_size;
  auto headerImage = codeStream->getHeaderImage();
  if(headerImage->numcomps > 0)
  {
    grklog.error("Only one SIZ marker allowed");
    return false;
  }
  auto cp = codeStream->getCodingParams();

  /* minimum size == 39 - 3 (= minimum component parameter) */
  if(headerSize < 36)
  {
    grklog.error("Error with SIZ marker size");
    return false;
  }

  remaining_size = headerSize - 36U;
  numComps = uint16_t(remaining_size / 3);
  nb_comp_remain = remaining_size % 3;
  if(nb_comp_remain != 0)
  {
    grklog.error("Error with SIZ marker size");
    return false;
  }

  uint16_t tmp;
  grk_read(&headerData, &tmp); /* Rsiz (capabilities) */
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
      grklog.warn("SIZ marker segment's Rsiz word must have bits 12 and 13 equal to 0");
      grklog.warn("unless the Part-2 flag (bit-15) is set.");
    }
    uint16_t profile = tmp & GRK_PROFILE_MASK;
    if((profile > GRK_PROFILE_CINEMA_LTS) && !GRK_IS_BROADCAST(profile) && !GRK_IS_IMF(profile))
    {
      grklog.error("Non-compliant Rsiz value 0x%x in SIZ marker", tmp);
      return false;
    }
  }

  cp->rsiz_ = tmp;
  grk_read(&headerData, &headerImage->x1); /* Xsiz */
  grk_read(&headerData, &headerImage->y1); /* Ysiz */
  grk_read(&headerData, &headerImage->x0); /* X0siz */
  grk_read(&headerData, &headerImage->y0); /* Y0siz */
  grk_read(&headerData, &cp->t_width_); /* XTsiz */
  grk_read(&headerData, &cp->t_height_); /* YTsiz */
  grk_read(&headerData, &cp->tx0_); /* XT0siz */
  grk_read(&headerData, &cp->ty0_); /* YT0siz */
  grk_read(&headerData, &tmp); /* Csiz */
  if(tmp == 0)
  {
    grklog.error("SIZ marker: number of components cannot be zero");
    return false;
  }
  if(tmp <= maxNumComponentsJ2K)
    headerImage->numcomps = tmp;
  else
  {
    grklog.error("SIZ marker: number of components %u is greater than maximum allowed number of "
                 "components %u",
                 tmp, maxNumComponentsJ2K);
    return false;
  }
  if(headerImage->numcomps != numComps)
  {
    grklog.error("SIZ marker: signalled number of components is not compatible with remaining "
                 "number of components ( %u vs %u)",
                 headerImage->numcomps, numComps);
    return false;
  }
  codeStream->setNumComponents(headerImage->numcomps);
  if((headerImage->x0 >= headerImage->x1) || (headerImage->y0 >= headerImage->y1))
  {
    std::stringstream ss;
    ss << "SIZ marker: negative or zero image dimensions ("
       << (int64_t)headerImage->x1 - headerImage->x0 << " x "
       << (int64_t)headerImage->y1 - headerImage->y0 << ")";
    grklog.error("%s", ss.str().c_str());
    return false;
  }
  if((cp->t_width_ == 0U) || (cp->t_height_ == 0U))
  {
    grklog.error("SIZ marker: invalid tile size (%u, %u)", cp->t_width_, cp->t_height_);
    return false;
  }
  if(cp->tx0_ > headerImage->x0 || cp->ty0_ > headerImage->y0)
  {
    grklog.error("SIZ marker: tile origin (%u,%u) cannot lie in the region"
                 " to the right and bottom of image origin (%u,%u)",
                 cp->tx0_, cp->ty0_, headerImage->x0, headerImage->y0);
    return false;
  }
  uint32_t tx1 = satAdd<uint32_t>(cp->tx0_, cp->t_width_);
  uint32_t ty1 = satAdd<uint32_t>(cp->ty0_, cp->t_height_);
  if(tx1 <= headerImage->x0 || ty1 <= headerImage->y0)
  {
    grklog.error("SIZ marker: first tile (%u,%u,%u,%u) must overlap"
                 " image (%u,%u,%u,%u)",
                 cp->tx0_, cp->ty0_, tx1, ty1, headerImage->x0, headerImage->y0, headerImage->x1,
                 headerImage->y1);
    return false;
  }
  headerImage->comps = new grk_image_comp[headerImage->numcomps];
  memset(headerImage->comps, 0, headerImage->numcomps * sizeof(grk_image_comp));
  auto img_comp = headerImage->comps;

  /* Read the component information */
  for(uint16_t i = 0; i < headerImage->numcomps; ++i)
  {
    uint8_t val;
    grk_read(&headerData, &val); /* Ssiz_i */
    img_comp->prec = (uint8_t)((val & 0x7f) + 1);
    img_comp->sgnd = val >> 7;
    grk_read(&headerData, &val); /* XRsiz_i */
    img_comp->dx = val; /* should be between 1 and 255 */
    grk_read(&headerData, &val); /* YRsiz_i */
    img_comp->dy = val; /* should be between 1 and 255 */
    if(img_comp->dx == 0 || img_comp->dy == 0)
    {
      grklog.error("Invalid values for comp = %u : dx=%u dy=%u\n (should be positive "
                   "according to the JPEG2000 standard)",
                   i, img_comp->dx, img_comp->dy);
      return false;
    }

    if(img_comp->prec > GRK_MAX_SUPPORTED_IMAGE_PRECISION)
    {
      grklog.error("Unsupported precision %u for comp = %u", img_comp->prec, i);
      grklog.error("Grok only supports precisions between 1 and %u inclusive",
                   GRK_MAX_SUPPORTED_IMAGE_PRECISION);

      return false;
    }
    ++img_comp;
  }
  /* Compute the number of tiles */
  uint32_t t_grid_width = ceildiv<uint32_t>(headerImage->x1 - cp->tx0_, cp->t_width_);
  uint32_t t_grid_height = ceildiv<uint32_t>(headerImage->y1 - cp->ty0_, cp->t_height_);
  if(t_grid_width == 0 || t_grid_height == 0)
  {
    grklog.error("Invalid grid of tiles: %u x %u. JPEG 2000 standard requires at least one tile "
                 "in grid. ",
                 t_grid_width, t_grid_height);
    return false;
  }
  if((uint64_t)t_grid_width * t_grid_height > (uint64_t)maxNumTilesJ2K)
  {
    grklog.error(
        "Invalid grid of tiles : %u x %u.  JPEG 2000 standard specifies maximum of %u tiles",
        t_grid_width, t_grid_height, maxNumTilesJ2K);
    return false;
  }
  cp->t_grid_width_ = (uint16_t)t_grid_width;
  cp->t_grid_height_ = (uint16_t)t_grid_height;
  codeStream->initTilesToDecompress(Rect16(0, 0, cp->t_grid_width_, cp->t_grid_height_));
  if(!codeStream->initDefaultTCP())
    return false;

  return headerImage->subsampleAndReduce(cp->codingParams_.dec_.reduce_);
}

bool SIZMarker::write(CodeStreamCompress* codeStream, IStream* stream)
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
  if(!stream->write(SIZ))
    return false;

  /* L_SIZ */
  if(!stream->write((uint16_t)(size_len - MARKER_BYTES)))
    return false;
  /* Rsiz (capabilities) */
  if(!stream->write(cp->rsiz_))
    return false;
  /* Xsiz */
  if(!stream->write(image->x1))
    return false;
  /* Ysiz */
  if(!stream->write(image->y1))
    return false;
  /* X0siz */
  if(!stream->write(image->x0))
    return false;
  /* Y0siz */
  if(!stream->write(image->y0))
    return false;
  /* XTsiz */
  if(!stream->write(cp->t_width_))
    return false;
  /* YTsiz */
  if(!stream->write(cp->t_height_))
    return false;
  /* XT0siz */
  if(!stream->write(cp->tx0_))
    return false;
  /* YT0siz */
  if(!stream->write(cp->ty0_))
    return false;
  /* Csiz */
  if(!stream->write(image->numcomps))
    return false;
  for(i = 0; i < image->numcomps; ++i)
  {
    auto comp = image->comps + i;
    /* TODO here with MCT ? */
    uint8_t bpc = (uint8_t)(comp->prec - 1);
    if(comp->sgnd)
      bpc = (uint8_t)(bpc + (1 << 7));
    if(!stream->write8u(bpc))
      return false;
    /* XRsiz_i */
    if(!stream->write8u(comp->dx))
      return false;
    /* YRsiz_i */
    if(!stream->write8u(comp->dy))
      return false;
  }

  return true;
}

} // namespace grk
