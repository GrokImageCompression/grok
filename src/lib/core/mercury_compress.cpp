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
#include "WaveletInt16Rules.h"
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
           !parameters->allocation_by_quality &&
           (parameters->layer_rate[0] > 0.0 || !parameters->max_comp_size) &&
           parameters->mct != 2 && !parameters->mct_data && !parameters->qfactor &&
           !parameters->quant_step_shift && !(parameters->csty & (CP_CSTY_SOP | CP_CSTY_EPH)) &&
           !parameters->progressive_rate_control && !(parameters->rsiz & GRK_PROFILE_PART2) &&
           parameters->tx0 == 0 && parameters->ty0 == 0;
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
    if(tcp->mct_ && numcomps != 3)
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
                       tccp.qmfbid_ == first.qmfbid_ &&
                       std::equal(tccp.precWidthExp_, tccp.precWidthExp_ + tccp.numresolutions_,
                                  first.precWidthExp_) &&
                       std::equal(tccp.precHeightExp_, tccp.precHeightExp_ + tccp.numresolutions_,
                                  first.precHeightExp_);
      // mercury's COD check rejects precincts one sample wide or high above resolution 0
      auto oneSamplePrecincts =
          std::count(tccp.precWidthExp_ + 1, tccp.precWidthExp_ + tccp.numresolutions_, 0) +
          std::count(tccp.precHeightExp_ + 1, tccp.precHeightExp_ + tccp.numresolutions_, 0);
      if(!sameStyle || tccp.cblkStyle_ != 0 || tccp.roishift_ != 0 || tccp.numresolutions_ < 2 ||
         oneSamplePrecincts > 0)
        return false;
    }
    return true;
  }

  bool tilesMatchMercury(CodingParams& cp, uint16_t numcomps)
  {
    uint32_t numTiles = (uint32_t)cp.t_grid_width_ * cp.t_grid_height_;
    // mercury takes every pass where classic's tiled rate control fails the encode
    if(numTiles > 1 && cp.codingParams_.enc_.failOnRateControlOvershoot_)
      return false;
    // mercury writes no tile-part POC markers
    auto first = cp.tcps_.get(0);
    for(uint32_t tile = 1; tile < numTiles; ++tile)
    {
      if(!cp.tcps_.get((uint16_t)tile)->sameProgressions(first, numcomps))
        return false;
    }
    return true;
  }

  // precision is left to the callers
  bool planeGeometryMatchesMercury(const GrkImage* image)
  {
    if(image->x0 != 0 || image->y0 != 0)
      return false;
    auto first = image->comps;
    for(uint16_t c = 0; c < image->numcomps; ++c)
    {
      auto comp = image->comps + c;
      if(comp->prec != first->prec || comp->sgnd || comp->dx != 1 || comp->dy != 1 ||
         comp->w != first->w || comp->h != first->h || comp->data_type != first->data_type)
        return false;
    }
    return first->data_type == GRK_INT_32 || first->data_type == GRK_INT_16;
  }

  bool int16PlanesMatchMercury(const GrkImage* image, TileCodingParams* tcp)
  {
    // classic codes int16 planes with the int16 9/7 only up to forward97FitsInt16
    return image->comps[0].data_type != GRK_INT_16 || tcp->tccps_[0].qmfbid_ == 1 ||
           forward97FitsInt16(image->comps[0].prec);
  }

  bool imagePlanesMatchMercury(const GrkImage* image)
  {
    auto first = image->comps;
    for(uint16_t c = 0; c < image->numcomps; ++c)
    {
      if(image->comps[c].stride != first->stride || !image->comps[c].data)
        return false;
    }
    return planeGeometryMatchesMercury(image);
  }

  void startMercuryTilePart(void* context, uint16_t tile, uint64_t length)
  {
    auto sink = (MercuryTilePartSink*)context;
    ++sink->parts;
    if(sink->tlm)
      sink->tlm->add(tile, (uint32_t)length);
  }

  void writeMercuryBytes(void* context, const uint8_t* bytes, uint64_t length)
  {
    auto sink = (MercuryTilePartSink*)context;
    sink->written = sink->written && sink->stream->writeBytes(bytes, length) == length;
  }

  void submitMercuryTask(void* context, mercury_host_run_fn run, void* pass)
  {
    static_cast<tf::Executor*>(context)->silent_async([run, pass] { run(pass); });
  }
} // namespace

bool CodeStreamCompress::deferXyzToMercury(const grk_cparameters* parameters, GrkImage* image)
{
  bool cinema =
      parameters->rsiz == GRK_PROFILE_CINEMA_2K || parameters->rsiz == GRK_PROFILE_CINEMA_4K;
  if(mercuryEncodeDisabled() || pluginAcceleratesCompress(parameters) || !cinema ||
     image->numcomps != cinemaComponents ||
     !(stripInput_ ? planeGeometryMatchesMercury(image) : imagePlanesMatchMercury(image)) ||
     image->comps[0].data_type != GRK_INT_32 || image->comps[0].prec > maxXyzSourcePrecision)
    return false;
  mercuryXyzSourcePrecision_ = image->comps[0].prec;
  for(uint16_t c = 0; c < image->numcomps; ++c)
    image->comps[c].prec = cinemaPrecision;
  image->color_space = GRK_CLRSPC_CUSTOM_CIE;
  return true;
}

bool CodeStreamCompress::selectMercuryEncode(const grk_cparameters* parameters)
{
  uint32_t numTiles = (uint32_t)cp_.t_grid_width_ * cp_.t_grid_height_;
  mercuryEncode_ =
      !mercuryEncodeDisabled() && parametersMatchMercury(parameters) &&
      codingMatchesMercury(cp_, cp_.tcps_.get(0), headerImage_->numcomps) &&
      (numTiles == 1 || parameters->layer_rate[0] == 0.0 || usesImageWideRateControl(numTiles)) &&
      tilesMatchMercury(cp_, headerImage_->numcomps) &&
      int16PlanesMatchMercury(headerImage_, cp_.tcps_.get(0)) &&
      (stripInput_ ? planeGeometryMatchesMercury(headerImage_)
                   : imagePlanesMatchMercury(headerImage_));
  if(mercuryEncode_ || !mercuryXyzSourcePrecision_)
    return true;
  auto xyzSourcePrecision = *mercuryXyzSourcePrecision_;
  mercuryXyzSourcePrecision_.reset();
  classicXyzPerStrip_ = stripInput_;
  if(stripInput_)
    return true;
  for(uint16_t c = 0; c < headerImage_->numcomps; ++c)
    headerImage_->comps[c].prec = xyzSourcePrecision;
  if(applyXYZTransform(headerImage_, cinemaPrecision))
    return true;
  grklog.error("Failed to apply XYZ colour transform");
  return false;
}

void CodeStreamCompress::MercuryEncoderFree::operator()(MercuryEncoder* encoder) const
{
  mercury_encoder_free(encoder);
}

bool CodeStreamCompress::createMercuryEncoder(void)
{
  auto image = headerImage_;
  auto tcp = cp_.tcps_.get(0);
  MercuryCodingHeader coding;
  fillMercuryCodingHeader(coding, cp_, *tcp, *image, *image);
  const auto& enc = cp_.codingParams_.enc_;
  MercuryEncodeParams params = {};
  uint32_t numTiles = (uint32_t)cp_.t_grid_width_ * cp_.t_grid_height_;
  // classic's rate control rounds each tile's layer budget up
  for(uint32_t tile = 0; tile < numTiles; ++tile)
  {
    double rate = cp_.tcps_.get((uint16_t)tile)->rates_[0];
    if(rate > 0.0)
      params.max_layer_length += (uint64_t)std::ceil(rate);
  }
  params.max_comp_size = enc.maxComponentRate_;
  params.slope_hint = enc.rateControlSlopeHint_;
  params.rate_tolerance = enc.rateControlTolerance_;
  params.split_by_component = enc.enableTilePartGeneration_;
  params.write_plt = enc.writePlt_;
  params.xyz_source_precision = mercuryXyzSourcePrecision_.value_or(0);
  if(!localExecutor_)
    mercuryGlobalExecutor_ = TFSingleton::acquire();
  params.threads = (uint32_t)(localExecutor_ ? localNumThreads_ : TFSingleton::num_threads());
  params.submit = submitMercuryTask;
  params.submit_ctx = localExecutor_ ? localExecutor_.get() : mercuryGlobalExecutor_.get();
  const uint8_t levels = (uint8_t)(tcp->tccps_[0].numresolutions_ - 1);
  std::vector<int32_t> levelFactors(levels);
  std::vector<double> bandScales((size_t)levels * t1::BAND_NUM_ORIENTATIONS);
  params.int16_planes = image->comps[0].data_type == GRK_INT_16;
  if(params.int16_planes && tcp->tccps_[0].qmfbid_ == 0)
  {
    for(uint8_t level = 0; level < levels; ++level)
    {
      levelFactors[level] = forward97Int16LevelFactor(level);
      for(uint8_t orientation = 0; orientation < t1::BAND_NUM_ORIENTATIONS; ++orientation)
        bandScales[(size_t)level * t1::BAND_NUM_ORIENTATIONS + orientation] =
            forward97Int16BandScale(level, orientation);
    }
    params.int16_fraction_bits = wavelet97Int16FractionBits(image->comps[0].prec);
    params.int16_level_factors = levelFactors.data();
    params.int16_band_scales = bandScales.data();
  }
  mercuryTilePartSink_ = {stream_, cp_.tlmMarkers_.get()};
  MercuryEncoder* encoder = nullptr;
  int32_t rc = mercury_encoder_new(&coding.header, &params, startMercuryTilePart, writeMercuryBytes,
                                   &mercuryTilePartSink_, &encoder);
  if(rc != MERCURY_OK)
  {
    grklog.error("mercury failed to start the encode, error %d", rc);
    return false;
  }
  mercuryEncoder_.reset(encoder);
  return true;
}

bool CodeStreamCompress::pushMercuryRows(const void* const* planes, uint32_t stride, uint32_t rows)
{
  int32_t rc = mercury_encoder_push_rows(mercuryEncoder_.get(), planes, stride, rows);
  if(rc == MERCURY_OK)
    return true;
  grklog.error("mercury failed to compress %u rows, error %d", rows, rc);
  mercuryEncoder_.reset();
  return false;
}

uint64_t CodeStreamCompress::compressWithMercury(void)
{
  auto image = headerImage_;
  if(!stripInput_)
  {
    std::vector<const void*> planes(image->numcomps);
    for(uint16_t c = 0; c < image->numcomps; ++c)
      planes[c] = image->comps[c].data;
    if(!createMercuryEncoder() ||
       !pushMercuryRows(planes.data(), image->comps[0].stride, image->comps[0].h))
      return 0;
  }
  uint16_t threshold = 0;
  int32_t rc = mercury_encoder_finish(mercuryEncoder_.release(), &threshold);
  if(rc != MERCURY_OK)
  {
    grklog.error("mercury failed to compress the image, error %d", rc);
    return 0;
  }
  if(!mercuryTilePartSink_.written)
    return 0;
  uint32_t numTiles = (uint32_t)cp_.t_grid_width_ * cp_.t_grid_height_;
  uint32_t signalledParts = 0;
  for(uint32_t tile = 0; tile < numTiles; ++tile)
    signalledParts += cp_.tcps_.get((uint16_t)tile)->signalledNumTileParts_;
  if(mercuryTilePartSink_.parts != signalledParts)
  {
    grklog.error("mercury wrote %u tile-parts, the header signals %u", mercuryTilePartSink_.parts,
                 signalledParts);
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
void CodeStreamCompress::MercuryEncoderFree::operator()(MercuryEncoder*) const {}
bool CodeStreamCompress::createMercuryEncoder(void)
{
  return false;
}
bool CodeStreamCompress::pushMercuryRows(const void* const*, uint32_t, uint32_t)
{
  return false;
}
uint64_t CodeStreamCompress::compressWithMercury(void)
{
  return 0;
}
} // namespace grk

#endif // GRK_MERCURY_BUILD
