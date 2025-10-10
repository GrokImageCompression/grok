/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

#include "grk_includes.h"
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
