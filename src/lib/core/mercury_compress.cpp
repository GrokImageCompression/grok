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

#include "TFSingleton.h"

#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "GrkMatrix.h"
#include "Logger.h"
#include "buffer.h"
#include "GrkObjectWrapper.h"
#include "TileFutureManager.h"
#include "FlowComponent.h"
#include "IStream.h"
#include "StreamIO.h"
#include "FetchCommon.h"
#include "TPFetchSeq.h"
#include "GrkImage.h"
#include "ICompressor.h"
#include "IDecompressor.h"
#include "Profile.h"
#include "MarkerParser.h"
#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct ITileProcessor;
}
#include "CodeStream.h"
#include "PacketIter.h"
#include "PacketLengthCache.h"
#include "TLMMarker.h"

#include "ICoder.h"
#include "CoderPool.h"
#include "CodeblockCompress.h"
#include "MinHeap.h"
#include "PacketManager.h"
#include "mct.h"
#include "ITileProcessor.h"
#include "ITileProcessorCompress.h"
#include "SOTMarker.h"
#include "CodeStreamCompress.h"
#include "XYZTransform.h"
#include "plugin_accelerate.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(GRK_MERCURY_BUILD)
#include "mercury_capi.h"
#include "mercury_header.h"

namespace grk
{
namespace
{
  const uint16_t cinemaComponents = 3;
  const uint8_t cinemaPrecision = 12;
  const uint8_t maxXyzSourcePrecision = 16;

  bool mercuryEncodeDisabled()
  {
    const char* value = std::getenv("GRK_MERCURY_ENCODE");
    return value && std::strcmp(value, "0") == 0;
  }

  bool parametersMatchMercury(const grk_cparameters* parameters)
  {
    return parameters->cod_format != GRK_FMT_MJ2 && parameters->numlayers == 1 &&
           !parameters->allocation_by_quality && parameters->layer_rate[0] > 0.0 &&
           parameters->mct != 2 && !parameters->mct_data && !parameters->qfactor &&
           !parameters->quant_step_shift && !(parameters->csty & (CP_CSTY_SOP | CP_CSTY_EPH)) &&
           !parameters->write_plt && !parameters->progressive_rate_control &&
           parameters->rate_control_algorithm == GRK_RATE_CONTROL_PCRD_OPT &&
           !(parameters->rsiz & GRK_PROFILE_PART2) && parameters->tx0 == 0 && parameters->ty0 == 0;
  }

  bool progressionsAreCprl(TileCodingParams* tcp)
  {
    if(!tcp->hasPoc())
      return tcp->prg_ == GRK_CPRL;
    for(uint32_t i = 0; i < tcp->getNumProgressions(); ++i)
      if(tcp->progressionOrderChange_[i].specified_compression_poc_prog != GRK_CPRL)
        return false;
    return true;
  }

  bool codingMatchesMercury(const CodingParams& cp, TileCodingParams* tcp, uint16_t numcomps)
  {
    if(cp.t_grid_width_ * cp.t_grid_height_ != 1 || (tcp->mct_ && numcomps != 3))
      return false;
    const auto& enc = cp.codingParams_.enc_;
    if(enc.enableTilePartGeneration_ &&
       (enc.newTilePartProgressionDivider_ != 'C' || !progressionsAreCprl(tcp)))
      return false;
    const auto& first = tcp->tccps_[0];
    for(uint16_t c = 0; c < numcomps; ++c)
    {
      const auto& tccp = tcp->tccps_[c];
      bool sameStyle = tccp.numresolutions_ == first.numresolutions_ &&
                       tccp.cblkw_expn_ == first.cblkw_expn_ &&
                       tccp.cblkh_expn_ == first.cblkh_expn_ && tccp.csty_ == first.csty_ &&
                       std::equal(tccp.precWidthExp_, tccp.precWidthExp_ + tccp.numresolutions_,
                                  first.precWidthExp_) &&
                       std::equal(tccp.precHeightExp_, tccp.precHeightExp_ + tccp.numresolutions_,
                                  first.precHeightExp_);
      if(!sameStyle || tccp.qmfbid_ != 0 || tccp.cblkStyle_ != 0 || tccp.roishift_ != 0 ||
         tccp.numresolutions_ < 2)
        return false;
    }
    return true;
  }

  // precision is left to the callers
  bool imagePlanesMatchMercury(const GrkImage* image)
  {
    if(image->x0 != 0 || image->y0 != 0)
      return false;
    auto first = image->comps;
    for(uint16_t c = 0; c < image->numcomps; ++c)
    {
      auto comp = image->comps + c;
      if(comp->prec != first->prec || comp->sgnd || comp->dx != 1 || comp->dy != 1 ||
         comp->w != first->w || comp->h != first->h || comp->stride != first->stride ||
         comp->data_type != GRK_INT_32 || !comp->data)
        return false;
    }
    return true;
  }

  struct MercuryTilePartSink
  {
    IStream* stream;
    TLMMarker* tlm;
    uint32_t parts = 0;
    bool written = true;
  };

  void writeMercuryTilePart(void* context, const uint8_t* bytes, uint64_t length)
  {
    auto sink = (MercuryTilePartSink*)context;
    ++sink->parts;
    sink->written = sink->written && sink->stream->writeBytes(bytes, length) == length;
    if(sink->tlm)
      sink->tlm->add(0, (uint32_t)length);
  }
} // namespace

bool CodeStreamCompress::deferXyzToMercury(const grk_cparameters* parameters, GrkImage* image)
{
  bool cinema =
      parameters->rsiz == GRK_PROFILE_CINEMA_2K || parameters->rsiz == GRK_PROFILE_CINEMA_4K;
  if(mercuryEncodeDisabled() || pluginAcceleratesCompress(parameters) || !cinema ||
     image->numcomps != cinemaComponents || !imagePlanesMatchMercury(image) ||
     image->comps[0].prec > maxXyzSourcePrecision)
    return false;
  mercuryXyzSourcePrecision_ = image->comps[0].prec;
  for(uint16_t c = 0; c < image->numcomps; ++c)
    image->comps[c].prec = cinemaPrecision;
  image->color_space = GRK_CLRSPC_CUSTOM_CIE;
  return true;
}

bool CodeStreamCompress::selectMercuryEncode(const grk_cparameters* parameters)
{
  mercuryEncode_ = !mercuryEncodeDisabled() && parametersMatchMercury(parameters) &&
                   codingMatchesMercury(cp_, cp_.tcps_.get(0), headerImage_->numcomps) &&
                   imagePlanesMatchMercury(headerImage_);
  if(mercuryEncode_ || !mercuryXyzSourcePrecision_)
    return true;
  for(uint16_t c = 0; c < headerImage_->numcomps; ++c)
    headerImage_->comps[c].prec = *mercuryXyzSourcePrecision_;
  mercuryXyzSourcePrecision_.reset();
  if(applyXYZTransform(headerImage_, cinemaPrecision))
    return true;
  grklog.error("Failed to apply XYZ colour transform");
  return false;
}

uint64_t CodeStreamCompress::compressWithMercury(void)
{
  auto image = headerImage_;
  auto tcp = cp_.tcps_.get(0);
  MercuryCodingHeader coding;
  fillMercuryCodingHeader(coding, cp_, *tcp, *image, *image);
  const auto& enc = cp_.codingParams_.enc_;
  MercuryEncodeParams params = {};
  // classic's rate control rounds the layer budget up
  params.max_layer_length = (uint32_t)std::ceil(tcp->rates_[0]);
  params.max_comp_size = enc.maxComponentRate_;
  params.slope_hint = enc.rateControlSlopeHint_;
  params.rate_tolerance = enc.rateControlTolerance_;
  params.split_by_component = enc.enableTilePartGeneration_;
  params.xyz_source_precision = mercuryXyzSourcePrecision_.value_or(0);
  params.threads = (uint32_t)TFSingleton::num_threads();
  std::vector<const int32_t*> planes(image->numcomps);
  for(uint16_t c = 0; c < image->numcomps; ++c)
    planes[c] = (const int32_t*)image->comps[c].data;
  MercuryTilePartSink sink{stream_, cp_.tlmMarkers_.get()};
  uint16_t threshold = 0;
  int32_t rc = mercury_encode_tile(&coding.header, &params, planes.data(), image->comps[0].stride,
                                   writeMercuryTilePart, &sink, &threshold);
  if(rc != MERCURY_OK)
  {
    grklog.error("mercury failed to compress the image, error %d", rc);
    return 0;
  }
  if(!sink.written)
    return 0;
  if(sink.parts != tcp->signalledNumTileParts_)
  {
    grklog.error("mercury wrote %u tile-parts, the header signals %u", sink.parts,
                 tcp->signalledNumTileParts_);
    return 0;
  }
  recordSlopeThreshold(threshold);
  if(!end())
    return 0;
  grklog.info("mercury compressed %ux%u", image->comps[0].w, image->comps[0].h);
  return stream_->tell();
}

} // namespace grk

#else // GRK_MERCURY_BUILD: no mercury encoder without cargo at configure time

namespace grk
{
bool CodeStreamCompress::deferXyzToMercury(const grk_cparameters*, GrkImage*)
{
  return false;
}
bool CodeStreamCompress::selectMercuryEncode(const grk_cparameters*)
{
  return true;
}
uint64_t CodeStreamCompress::compressWithMercury(void)
{
  return 0;
}
} // namespace grk

#endif // GRK_MERCURY_BUILD
