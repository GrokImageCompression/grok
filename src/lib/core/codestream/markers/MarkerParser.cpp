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
#include "grk_exceptions.h"
#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "grk_includes.h"
#include "IStream.h"
#include "StreamIO.h"
#include "GrkImageMeta.h"
#include "GrkImage.h"
#include "MarkerParser.h"
#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct ITileProcessor;
}
#include "CodeStream.h"
#include "BitIO.h"

namespace grk
{
IMarkerProcessor::IMarkerProcessor(uint16_t ID) : id(ID) {}
MarkerProcessor::MarkerProcessor(uint16_t id, MARKER_CALLBACK c)
    : IMarkerProcessor(id), callback_(c)
{}
bool MarkerProcessor::process(uint8_t* headerData, uint16_t headerSize) const
{
  if(callback_)
    return callback_(headerData, headerSize);
  return false;
}
MarkerParser::~MarkerParser()
{
  for(const auto& val : processors_)
    delete val.second;
  if(ownsStream_)
    delete stream_;
}
void MarkerParser::setStream(IStream* stream, bool ownsStream)
{
  if(ownsStream_)
    delete stream_;
  stream_ = stream;
  ownsStream_ = ownsStream;
  scratch_.setStream(stream);
}

IStream* MarkerParser::getStream(void)
{
  return stream_;
}

bool MarkerParser::readShort(IStream* stream, uint16_t* val)
{
  uint8_t temp[sizeof(uint16_t)];
  if(stream->read(temp, nullptr, sizeof(uint16_t)) != sizeof(uint16_t))
    return false;

  grk_read(temp, val);

  return true;
}
void MarkerParser::synch(uint16_t markerId)
{
  currMarkerId_ = markerId;
}
uint16_t MarkerParser::currId(void)
{
  return currMarkerId_;
}
void MarkerParser::setSOT(void)
{
  currMarkerId_ = SOT;
}

bool MarkerParser::readSOTorEOC(void)
{
  if(!readId(false))
    return false;

  if(currMarkerId_ != SOT && currMarkerId_ != EOC)
    grklog.warn("Expected SOT or EOC marker - read %s marker instead.",
                markerString(currMarkerId_).c_str());

  return true;
}

bool MarkerParser::endOfCodeStream(void)
{
  return foundEOC_ || stream_->numBytesLeft() == 0;
}

bool MarkerParser::readSOTafterSOD(void)
{
  // if there is no data left, then simply return true
  if(stream_->numBytesLeft() == 0)
    return true;

  // if EOC marker has not been read yet, then try to read the next marker
  // (should be EOC or SOT)
  if(!foundEOC_)
  {
    try
    {
      if(!readId(false))
      {
        grklog.warn("readSOTafterSOD: Not enough data to read another marker.\n"
                    "Tile may be truncated.");
        return true;
      }
    }
    catch([[maybe_unused]] const t1_t2::InvalidMarkerException& ume)
    {
      grklog.warn("readSOTafterSOD: expected EOC or SOT "
                  "but found invalid marker 0x%.4x",
                  currId());
      throw DecodeUnknownMarkerAtEndOfTileException();
    }

    switch(currId())
    {
      // we found the EOC marker - set state accordingly and return true;
      // we can ignore all data after EOC
      case EOC:
        foundEOC_ = true;
        break;
      // start of another tile
      case SOT:
        break;
      default:
        grklog.warn("findNextTile: expected EOC or SOT "
                    "but found marker 0x%.4x.\nIgnoring %u bytes "
                    "remaining in the stream.",
                    currId(), stream_->numBytesLeft() + 2);
        throw DecodeUnknownMarkerAtEndOfTileException();
    }
  }

  return true;
}
bool MarkerParser::process(const IMarkerProcessor* processor, uint16_t markerBodyLength)
{
  return scratch_.process(processor, markerBodyLength);
}

std::pair<bool, uint16_t> MarkerParser::processMarker(void)
{
  // marker body length == marker segment length - MARKER_LENGTH_BYTES - MARKER_BYTES
  uint16_t markerBodyLength = 0;
  if(!readShort(stream_, &markerBodyLength))
  {
    return {false, markerBodyLength};
  }
  else if(markerBodyLength < MARKER_LENGTH_BYTES)
  {
    grklog.error("Marker length %u for marker 0x%x is less than marker length bytes (2)",
                 markerBodyLength, currMarkerId_);
    return {false, markerBodyLength};
  }
  else if(markerBodyLength == MARKER_LENGTH_BYTES)
  {
    grklog.error("Zero-size marker in header.");
    return {false, markerBodyLength};
  }
  auto processor = currentProcessor();
  if(!processor)
  {
    grklog.error("Unknown marker 0x%x encountered", currMarkerId_);
    return {false, markerBodyLength};
  }
  bool rc = process(processor, markerBodyLength - MARKER_LENGTH_BYTES);
  return {rc, markerBodyLength};
}

bool MarkerParser::readId(bool suppressWarning)
{
  if(!readShort(stream_, &currMarkerId_))
    return false;

  /* Check if the current marker ID is valid */
  if(currMarkerId_ < 0xff00)
  {
    if(!suppressWarning)
      grklog.warn("marker ID 0x%.4x does not match JPEG 2000 marker format 0xffxx", currMarkerId_);
    throw t1_t2::InvalidMarkerException(currMarkerId_);
  }

  return true;
}
void MarkerParser::add(const uint16_t id, IMarkerProcessor* processor)
{
  if(!processors_.contains(id))
    processors_[id] = processor; // Add only if it doesn't exist
}

void MarkerParser::add(
    const std::initializer_list<std::pair<const uint16_t, IMarkerProcessor*>>& new_markers)
{
  for(const auto& entry : new_markers)
  {
    auto it = processors_.find(entry.first);
    if(it != processors_.end())
    {
      grklog.warn("Marker %d already exists. Overwriting.", entry.first);
      delete it->second; // If you need to clean up the old handler
    }
    processors_[entry.first] = entry.second; // Add or overwrite
  }
}
void MarkerParser::clearProcessors(void)
{
  for(const auto& val : processors_)
    delete val.second;
  processors_.clear();
}

IMarkerProcessor* MarkerParser::currentProcessor(void)
{
  auto iter = processors_.find(currMarkerId_);
  if(iter != processors_.end())
    return iter->second;
  else
  {
    grklog.warn("Unknown marker 0x%02x detected.", currMarkerId_);
    return nullptr;
  }
}

bool MarkerParser::checkForIllegalTilePart(void)
{
  try
  {
    processMarker();
  }
  catch(const CorruptSOTMarkerException& csme)
  {
    return true;
  }
  return false;
}

std::string MarkerParser::markerString(uint16_t marker)
{
  switch(marker)
  {
    case SOC:
      return "SOC";
    case SOT:
      return "SOT";
    case SOD:
      return "SOD";
    case EOC:
      return "EOC";
    case CAP:
      return "CAP";
    case SIZ:
      return "SIZ";
    case COD:
      return "COD";
    case COC:
      return "COC";
    case RGN:
      return "RGN";
    case QCD:
      return "QCD";
    case QCC:
      return "QCC";
    case POC:
      return "POC";
    case TLM:
      return "TLM";
    case PLM:
      return "PLM";
    case PLT:
      return "PLT";
    case PPM:
      return "PPM";
    case PPT:
      return "PPT";
    case SOP:
      return "SOP";
    case EPH:
      return "EPH";
    case CRG:
      return "CRG";
    case COM:
      return "COM";
    case CBD:
      return "CBD";
    case MCC:
      return "MCC";
    case MCT:
      return "MCT";
    case MCO:
      return "MCO";
    case UNK:
    default:
      return "Unknown";
  }
}

MarkerScratch::MarkerScratch(void)
    : buff_(new uint8_t[default_header_size]), len_(default_header_size)
{}
MarkerScratch::~MarkerScratch(void)
{
  delete[] buff_;
}
bool MarkerScratch::process(const IMarkerProcessor* processor, uint16_t markerSize)
{
  if(markerSize > len_)
  {
    if(markerSize > stream_->numBytesLeft())
    {
      grklog.error("Marker size inconsistent with stream length");
      return false;
    }
    delete[] buff_;
    buff_ = new uint8_t[2 * markerSize];
    len_ = (uint16_t)(2U * markerSize);
  }
  if(stream_->read(buff_, nullptr, markerSize) != markerSize)
  {
    grklog.error("Stream too short");
    return false;
  }

  return processor->process(buff_, markerSize);
}

void MarkerScratch::setStream(IStream* stream)
{
  stream_ = stream;
}

} // namespace grk
