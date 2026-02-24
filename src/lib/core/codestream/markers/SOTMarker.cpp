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
#include "TileFutureManager.h"
#include "FlowComponent.h"
#include "IStream.h"
#include "FetchCommon.h"
#include "TPFetchSeq.h"
#include "GrkImageMeta.h"
#include "GrkImage.h"
#include "MarkerParser.h"
#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct TileProcessor;
struct TileProcessorCompress;
} // namespace grk

#include "CodeStream.h"
#include "PacketLengthCache.h"
#include "ICoder.h"
#include "CoderPool.h"
#include "ICoder.h"
#include "CodeblockCompress.h"

#include "CodecScheduler.h"
#include "TileProcessor.h"
#include "SOTMarker.h"
#include "TileProcessorCompress.h"

namespace grk
{
SOTMarker::SOTMarker(void) : psot_location_(0) {}
bool SOTMarker::write_psot(IStream* stream, uint32_t tilePartLength)
{
  if(psot_location_)
  {
    auto currentLocation = stream->tell();
    stream->seek(psot_location_);
    if(!stream->write(tilePartLength))
      return false;
    stream->seek(currentLocation);
  }

  return true;
}

bool SOTMarker::write(TileProcessorCompress* compressor, uint32_t tilePartLength)
{
  auto stream = compressor->getStream();
  /* SOT */
  if(!stream->write(SOT))
    return false;

  /* Lsot */
  if(!stream->write((uint16_t)10))
    return false;
  /* Isot */
  if(!stream->write((uint16_t)compressor->getIndex()))
    return false;

  /* Psot  */
  if(tilePartLength)
  {
    if(!stream->write(tilePartLength))
      return false;
  }
  else
  {
    psot_location_ = stream->tell();
    if(!stream->skip(4))
      return false;
  }

  /* TPsot */
  if(!stream->write8u(compressor->getTilePartCounter()))
    return false;

  /* TNsot */
  if(!stream->write8u(compressor->getTCP()->signalledNumTileParts_))
    return false;

  return true;
}

} /* namespace grk */
