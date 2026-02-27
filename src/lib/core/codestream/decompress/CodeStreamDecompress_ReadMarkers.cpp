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

#include "grk_restrict.h"
#include "simd.h"
#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "grk_includes.h"
#include "ISparseCanvas.h"
#include "IStream.h"
#include "StreamIO.h"
#include "MarkerCache.h"
#include "FetchCommon.h"
#include "TPFetchSeq.h"
#include "GrkImage.h"
#include "ICompressor.h"
#include "IDecompressor.h"
#include "MarkerParser.h"
#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct ITileProcessor;
}
#include "CodingParams.h"
#include "CodeStream.h"
#include "PacketLengthCache.h"
#include "TLMMarker.h"

#include "ICoder.h"
#include "CoderPool.h"
#include "BitIO.h"

#include "TagTree.h"

#include "CodeblockCompress.h"

#include "CodeblockDecompress.h"
#include "Precinct.h"
#include "Subband.h"
#include "Resolution.h"

#include "TileFutureManager.h"
#include "FlowComponent.h"

#include "CodecScheduler.h"
#include "TileComponentWindow.h"
#include "ITileProcessor.h"
#include "TileCache.h"
#include "TileCompletion.h"
#include "CodeStreamDecompress.h"
#include "CoderFactory.h"

namespace grk
{

bool CodeStreamDecompress::readHeaderProcedure(void)
{
  try
  {
    bool has_siz = false;
    bool has_cod = false;
    bool has_qcd = false;

    /* Try to read the SOC marker, the code stream must begin with SOC marker */
    if(!readSOC())
    {
      grklog.error("Code stream must begin with SOC marker ");
      return false;
    }
    if(!markerParser_.readId(false))
      return false;

    if(markerParser_.currId() != SIZ)
    {
      grklog.error("Code-stream must contain a valid SIZ marker segment, immediately after the SOC "
                   "marker ");
      return false;
    }

    /* read until first SOT is detected */
    while(markerParser_.currId() != SOT)
    {
      // 1. get handler handler
      auto handler = markerParser_.currentProcessor();
      if(!handler)
      {
        if(!readUNK())
          return false;
        if(markerParser_.currId() == SOT)
          break;
        handler = markerParser_.currentProcessor();
      }
      if(handler->id == SIZ)
        has_siz = true;
      else if(handler->id == COD)
        has_cod = true;
      else if(handler->id == QCD)
        has_qcd = true;

      // 2. read rest of marker segment
      uint16_t markerBodyLength;
      if(!MarkerParser::readShort(stream_, &markerBodyLength))
        return false;
      else if(markerBodyLength == MARKER_LENGTH_BYTES)
      {
        grklog.error("Zero-size marker in header.");
        return false;
      }
      markerBodyLength = (uint16_t)(markerBodyLength - MARKER_LENGTH_BYTES);

      // 3. handle marker
      if(!markerParser_.process(handler, markerBodyLength))
        return false;

      // 4. add the marker to code stream index
      uint16_t markerSegmentLength = MARKER_BYTES_PLUS_MARKER_LENGTH_BYTES + markerBodyLength;
      markerCache_->add(handler->id, stream_->tell() - markerSegmentLength, markerSegmentLength);

      // 5. read next marker
      if(!markerParser_.readId(false))
        return false;
    }
    if(!has_siz)
    {
      grklog.error("required SIZ marker not found in main header");
      return false;
    }
    else if(!has_cod)
    {
      grklog.error("required COD marker not found in main header");
      return false;
    }
    else if(!has_qcd)
    {
      grklog.error("required QCD marker not found in main header");
      return false;
    }
    if(!mergePpm(&cp_))
    {
      grklog.error("Failed to merge PPM data");
      return false;
    }
    auto tileStreamStart = stream_->tell() - MARKER_BYTES;
    markerCache_->setTileStreamStart(tileStreamStart);
    if(cp_.tlmMarkers_)
    {
      cp_.tlmMarkers_->readComplete(tileStreamStart);
    }
    else
    {
      auto p = std::string("/temp");
      cp_.tlmMarkers_ = std::make_unique<TLMMarker>(p, cp_.t_grid_width_ * cp_.t_grid_height_,
                                                    markerCache_->getTileStreamStart());
    }

    return true;
  }
  catch(const t1_t2::InvalidMarkerException& ime)
  {
    grklog.warn("Found invalid marker in main header : 0x%.4x", ime.marker_);
    return false;
  }
  return true;
}

void CodeStreamDecompress::postReadHeader(void)
{
  // set up tile completion based on time and image bounds
  if(headerRead_)
  {
    if(cp_.asynchronous_ && cp_.simulate_synchronous_)
    {
      auto bounds = headerImage_->getBounds();
      tileCompletion_ = std::make_unique<TileCompletion>(
          tileCache_.get(), bounds, cp_.t_width_, cp_.t_height_,
          [this](uint16_t tileIndexBegin, uint16_t tileIndexEnd) {
            onRowCompleted(tileIndexBegin, tileIndexEnd);
          },
          tilesToDecompress_.getSlatedTileRect());
    }

    setDecompressRegion(RectD(cp_.dw_x0, cp_.dw_y0, cp_.dw_x1, cp_.dw_y1));
  }
}

bool CodeStreamDecompress::readHeader(grk_header_info* headerInfo)
{
  if(headerError_)
    return false;

  if(!headerRead_)
  {
    headerRead_ = true;
    procedureList_.push_back(std::bind(&CodeStreamDecompress::readHeaderProcedure, this));
    if(!exec(procedureList_))
    {
      headerError_ = true;
      return false;
    }
    if(headerInfo)
    {
      headerImage_->has_multiple_tiles =
          headerImage_->has_multiple_tiles && !headerInfo->single_tile_decompress;

      headerImage_->decompress_fmt = headerInfo->decompress_fmt;
      if(headerImage_->color_space == GRK_CLRSPC_UNKNOWN)
        headerImage_->color_space = headerInfo->color_space;
      headerImage_->force_rgb = headerInfo->force_rgb;
      headerImage_->upsample = headerInfo->upsample;
      headerImage_->precision = headerInfo->precision;
      headerImage_->num_precision = headerInfo->num_precision;
    }
    headerImage_->copyHeaderTo(multiTileComposite_.get());
    multiTileComposite_->validateColourSpace();
    uint32_t num_threads = (uint32_t)ExecSingleton::num_threads();
    coderPool_.makeCoders(num_threads, 6, 6, [this]() -> std::shared_ptr<t1::ICoder> {
      return std::shared_ptr<t1::ICoder>(
          t1::CoderFactory::makeCoder(isHT_, false, 64, 64, tileCache_->getStrategy()));
    });

    coderPool_.makeCoders(num_threads, 5, 5, [this]() -> std::shared_ptr<t1::ICoder> {
      return std::shared_ptr<t1::ICoder>(
          t1::CoderFactory::makeCoder(isHT_, false, 32, 32, tileCache_->getStrategy()));
    });
    defaultTcp_->finalizePocs();
    if(isHT_)
    {
      if(!defaultTcp_->isHT())
      {
        for(uint16_t i = 0; i < defaultTcp_->numComps_; ++i)
        {
          auto tccp = defaultTcp_->tccps_ + i;
          defaultTcp_->setIsHT(true, tccp->qmfbid_ == 1, tccp->numgbits_);
        }
      }
    }
  }
  if(headerInfo)
  {
    auto tccp = defaultTcp_->tccps_;

    headerInfo->cblockw_init = 1U << tccp->cblkw_expn_;
    headerInfo->cblockh_init = 1U << tccp->cblkh_expn_;
    headerInfo->irreversible = tccp->qmfbid_ == 0;
    headerInfo->mct = defaultTcp_->mct_;
    headerInfo->rsiz = cp_.rsiz_;
    headerInfo->numresolutions = tccp->numresolutions_;
    headerInfo->prog_order = defaultTcp_->prg_;
    // !!! assume that coding style is constant across all tile components
    headerInfo->csty = tccp->csty_;
    // !!! assume that mode switch is constant across all tiles
    headerInfo->cblk_sty = tccp->cblkStyle_;
    for(uint32_t i = 0; i < headerInfo->numresolutions; ++i)
    {
      headerInfo->prcw_init[i] = 1U << tccp->precWidthExp_[i];
      headerInfo->prch_init[i] = 1U << tccp->precHeightExp_[i];
    }
    headerInfo->tx0 = cp_.tx0_;
    headerInfo->ty0 = cp_.ty0_;

    headerInfo->t_width = cp_.t_width_;
    headerInfo->t_height = cp_.t_height_;

    headerInfo->t_grid_width = cp_.t_grid_width_;
    headerInfo->t_grid_height = cp_.t_grid_height_;
    headerInfo->header_image = *headerImage_;

    headerInfo->num_layers = defaultTcp_->numLayers_;

    headerInfo->num_comments = cp_.numComments_;
    for(size_t i = 0; i < headerInfo->num_comments; ++i)
    {
      headerInfo->comment[i] = cp_.comment_[i];
      headerInfo->comment_len[i] = cp_.commentLength_[i];
      headerInfo->is_binary_comment[i] = cp_.isBinaryComment_[i];
    }
  }

  postReadHeader();

  return true;
}
GrkImage* CodeStreamDecompress::getHeaderImage(void)
{
  return headerImage_;
}
bool CodeStreamDecompress::needsHeaderRead(void) const
{
  return !headerError_ && !headerRead_;
}

bool CodeStreamDecompress::readCRG(uint8_t* headerData, uint16_t headerSize)
{
  assert(headerData != nullptr);
  if(headerSize != headerImage_->numcomps * 4)
  {
    grklog.error("Error reading CRG marker");
    return false;
  }
  for(uint16_t i = 0; i < headerImage_->numcomps; ++i)
  {
    auto comp = headerImage_->comps + i;
    // Xcrg_i
    grk_read(&headerData, &comp->crg_x);
    // Xcrg_i
    grk_read(&headerData, &comp->crg_y);
  }
  return true;
}

bool CodeStreamDecompress::readPLM(uint8_t* headerData, uint16_t headerSize)
{
  assert(headerData != nullptr);
  if(!cp_.plmMarkers_)
    cp_.plmMarkers_ = std::make_unique<PLMarker>();

  return cp_.plmMarkers_->readPLM(headerData, headerSize);
}

bool CodeStreamDecompress::readPPM(uint8_t* headerData, uint16_t headerSize)
{
  if(!cp_.ppmMarkers_)
    cp_.ppmMarkers_ = std::make_unique<PPMMarker>();

  return cp_.ppmMarkers_->read(headerData, headerSize);
}

bool CodeStreamDecompress::mergePpm(CodingParams* p_cp)
{
  return p_cp->ppmMarkers_ ? p_cp->ppmMarkers_->merge() : true;
}

bool CodeStreamDecompress::readSOT(uint8_t* headerData, uint16_t headerSize)
{
  if(headerSize != sotMarkerSegmentLen - MARKER_BYTES_PLUS_MARKER_LENGTH_BYTES)
  {
    grklog.error("Error reading SOT marker: header size %d must equal %d", headerSize,
                 sotMarkerSegmentLen - MARKER_BYTES_PLUS_MARKER_LENGTH_BYTES);
    return false;
  }
  uint16_t tileIndex;
  grk_read(&headerData, &tileIndex);
  currTileIndex_ = tileIndex;

  grk_read(&headerData, &currTilePartInfo_.tilePartLength_);
  if(!cp_.hasTLM() && !tilesToDecompress_.isSlated(tileIndex))
  {
    return currTilePartInfo_.tilePartLength_
               ? stream_->skip((int64_t)(currTilePartInfo_.tilePartLength_ - sotMarkerSegmentLen))
               : true;
  }

  auto processor = getTileProcessor(tileIndex);
  if(!processor->readSOT(this->stream_, headerData, headerSize, currTilePartInfo_, false))
    return false;
  currTileProcessor_ = processor;
  return true;
}

bool CodeStreamDecompress::readCBD(uint8_t* headerData, uint16_t headerSize)
{
  assert(headerData != nullptr);
  if(headerSize < 2 || (headerSize - 2) != headerImage_->numcomps)
  {
    grklog.error("Error reading CBD marker");
    return false;
  }
  uint16_t numComps;
  grk_read(&headerData, &numComps);
  if(numComps != headerImage_->numcomps)
  {
    grklog.error("Error reading CBD marker");
    return false;
  }

  for(uint16_t i = 0; i < headerImage_->numcomps; ++i)
  {
    /* Component bit depth */
    uint8_t depth;
    grk_read(&headerData, &depth);
    auto comp = headerImage_->comps + i;
    comp->sgnd = depth >> 7;
    auto prec = (uint8_t)((depth & 0x7f) + 1U);
    if(prec > GRK_MAX_SUPPORTED_IMAGE_PRECISION)
    {
      grklog.error("CBD marker: precision %d for component %d is greater than maximum "
                   "supported precision %d",
                   prec, i, GRK_MAX_SUPPORTED_IMAGE_PRECISION);
      return false;
    }
    comp->prec = prec;
  }

  return true;
}

bool CodeStreamDecompress::readTLM(uint8_t* headerData, uint16_t headerSize)
{
  if(!cp_.tlmMarkers_)
    cp_.tlmMarkers_ = std::make_unique<TLMMarker>(cp_.t_grid_width_ * cp_.t_grid_height_);
  std::span<uint8_t> header(headerData, headerSize);
  auto rc = cp_.tlmMarkers_->read(header, headerSize);

  // disable
  if(rc && (cp_.codingParams_.dec_.disableRandomAccessFlags_ & GRK_RANDOM_ACCESS_TLM) != 0)
    cp_.tlmMarkers_->invalidate();

  return rc;
}

bool CodeStreamDecompress::readUNK(void)
{
  uint16_t size_unk = MARKER_BYTES;
  uint16_t unknownId = markerParser_.currId();
  while(true)
  {
    // keep reading potential markers until we either find the next known marker, or
    // we reach the end of the stream
    try
    {
      if(!markerParser_.readId(true))
      {
        grklog.error("Unable to read unknown marker 0x%02x.", unknownId);
        return false;
      }
    }
    catch(const t1_t2::InvalidMarkerException&)
    {
      size_unk += MARKER_BYTES;
      continue;
    }
    markerCache_->add(unknownId, stream_->tell() - MARKER_BYTES - size_unk, size_unk);
    // check if we need to process another unknown marker
    if(!markerParser_.currentProcessor())
    {
      size_unk = MARKER_BYTES;
      unknownId = markerParser_.currId();
      continue;
    }
    // the next marker is known and located correctly
    break;
  }

  return true;
}

bool CodeStreamDecompress::readSOC()
{
  uint8_t data[MARKER_BYTES];
  uint16_t marker;
  if(stream_->read(data, nullptr, MARKER_BYTES) != MARKER_BYTES)
    return false;

  grk_read(data, &marker);
  if(marker != SOC)
    return false;

  // subtract already-read SOC marker length when caching header
  markerCache_->add(SOC, stream_->tell() - MARKER_BYTES, MARKER_BYTES);

  return true;
}

bool CodeStreamDecompress::readCAP(uint8_t* headerData, uint16_t headerSize)
{
  if(headerSize < sizeof(cp_.pcap_))
  {
    grklog.error("Error with SIZ marker size");
    return false;
  }

  uint32_t tmp;
  grk_read(&headerData, &tmp); /* Pcap */
  if(tmp & 0xFFFDFFFF)
  {
    grklog.error("Pcap in CAP marker has unsupported options.");
    return false;
  }
  if((tmp & 0x00020000) == 0)
  {
    grklog.error("Pcap in CAP marker should have its 15th MSB set. ");
    return false;
  }
  cp_.pcap_ = tmp;
  if(cp_.pcap_)
    isHT_ = true;
  uint32_t count = grk_population_count(cp_.pcap_);
  uint32_t expectedSize = (uint32_t)sizeof(cp_.pcap_) + 2U * count;
  if(headerSize != expectedSize)
  {
    grklog.error("CAP marker size %u != expected size %u", headerSize, expectedSize);
    return false;
  }
  for(uint32_t i = 0; i < count; ++i)
    grk_read(&headerData, cp_.ccap_ + i);

  return true;
}

bool CodeStreamDecompress::readSIZ(uint8_t* headerData, uint16_t headerSize)
{
  SIZMarker siz;
  bool rc = siz.read(this, headerData, headerSize);
  if(rc)
  {
    uint16_t numTilesToDecompress = (uint16_t)(cp_.t_grid_height_ * cp_.t_grid_width_);
    headerImage_->has_multiple_tiles = numTilesToDecompress > 1;
  }

  return rc;
}

} // namespace grk
