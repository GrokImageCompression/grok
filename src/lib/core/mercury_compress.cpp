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

#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(GRK_MERCURY_BUILD)
#include "mercury_capi.h"

namespace grk
{
namespace
{
  const uint16_t cinemaComponents = 3;
  const uint8_t cinemaPrecision = 12;
  const uint8_t cinema2kResolutions = 6;
  const uint8_t cinema4kResolutions = 7;

  bool mercuryEncodeRequested()
  {
    const char* value = std::getenv("GRK_MERCURY_ENCODE");
    return value && std::strcmp(value, "1") == 0;
  }

  // checked after Profile::setCinemaParams has forced the rest of the cinema setup
  bool parametersMatchMercury(const grk_cparameters* parameters)
  {
    bool fourK = parameters->rsiz == GRK_PROFILE_CINEMA_4K;
    if(!fourK && parameters->rsiz != GRK_PROFILE_CINEMA_2K)
      return false;
    uint8_t resolutions = fourK ? cinema4kResolutions : cinema2kResolutions;
    return parameters->numresolution == resolutions && parameters->cod_format != GRK_FMT_MJ2 &&
           parameters->numlayers == 1 && !parameters->allocation_by_quality &&
           parameters->layer_rate[0] > 0.0 && parameters->mct == 1 && !parameters->mct_data &&
           !parameters->qfactor && !parameters->quant_step_shift &&
           parameters->csty == CP_CSTY_PRT && !parameters->write_plt &&
           !parameters->progressive_rate_control &&
           parameters->rate_control_algorithm == GRK_RATE_CONTROL_PCRD_OPT &&
           parameters->tx0 == 0 && parameters->ty0 == 0;
  }

  bool imageMatchesMercury(const GrkImage* image)
  {
    if(image->numcomps != cinemaComponents || image->x0 != 0 || image->y0 != 0)
      return false;
    auto first = image->comps;
    for(uint16_t c = 0; c < image->numcomps; ++c)
    {
      auto comp = image->comps + c;
      if(comp->prec != cinemaPrecision || comp->sgnd || comp->dx != 1 || comp->dy != 1 ||
         comp->w != first->w || comp->h != first->h || comp->stride != first->stride ||
         comp->data_type != GRK_INT_32 || !comp->data)
        return false;
    }
    return true;
  }
} // namespace

void CodeStreamCompress::selectMercuryEncode(const grk_cparameters* parameters)
{
  if(!mercuryEncodeRequested() || !parametersMatchMercury(parameters) ||
     !imageMatchesMercury(headerImage_))
    return;
  if(cp_.numComments_ != 1 || cp_.isBinaryComment_[0])
    return;
  mercuryEncode_ = MercuryEncodeSettings{parameters->layer_rate[0], parameters->max_cs_size};
}

uint64_t CodeStreamCompress::compressWithMercury(void)
{
  auto image = headerImage_;
  const int32_t* planes[cinemaComponents];
  for(uint16_t c = 0; c < cinemaComponents; ++c)
    planes[c] = (const int32_t*)image->comps[c].data;
  // rounding in the byte budget can pass max_cs_size by a few bytes
  uint64_t capacity =
      mercuryEncode_->maxCodestreamSize + (headerEndPosition_ - headerStartPosition_);
  std::vector<uint8_t> codestream(capacity);
  uint64_t length = 0;
  uint16_t threshold = 0;
  const auto& enc = cp_.codingParams_.enc_;
  int32_t rc =
      mercury_encode_dci(planes, image->comps[0].stride, image->comps[0].w, image->comps[0].h,
                         cp_.rsiz_ == GRK_PROFILE_CINEMA_4K, 0, mercuryEncode_->maxCodestreamSize,
                         mercuryEncode_->layerRate, enc.maxComponentRate_,
                         enc.rateControlSlopeHint_, enc.rateControlTolerance_, false, enc.writeTlm_,
                         (uint32_t)TFSingleton::num_threads(), (const uint8_t*)cp_.comment_[0],
                         cp_.commentLength_[0], codestream.data(), capacity, &length, &threshold);
  if(rc != MERCURY_OK)
  {
    grklog.error("mercury failed to compress the image, error %d", rc);
    return 0;
  }
  if(!stream_->seek(headerStartPosition_) ||
     stream_->writeBytes(codestream.data(), length) != length || !stream_->flush())
    return 0;
  recordSlopeThreshold(threshold);
  grklog.info("mercury compressed %ux%u", image->comps[0].w, image->comps[0].h);
  return stream_->tell();
}

} // namespace grk

#else // GRK_MERCURY_BUILD: no mercury encoder without cargo at configure time

namespace grk
{
void CodeStreamCompress::selectMercuryEncode(const grk_cparameters*) {}
uint64_t CodeStreamCompress::compressWithMercury(void)
{
  return 0;
}
} // namespace grk

#endif // GRK_MERCURY_BUILD
