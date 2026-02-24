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

#include <memory>
#include <ranges>

#include "grk_exceptions.h"
#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "grk_includes.h"
#include "StreamIO.h"
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
}
#include "CodeStream.h"
#include "PacketLengthCache.h"
#include "TLMMarker.h"

#include "ICoder.h"
#include "CoderPool.h"
#include "WindowScheduler.h"
#include "TileProcessor.h"
#include "TileCache.h"
#include "TLMFile.h"

namespace grk
{

struct TilePartLengthPOD
{
  uint16_t tileIndex_;
  uint32_t length_;
};

// TLM(2) + Ltlm(2) + Ztlm(1) + Stlm(1)
const uint32_t tlm_marker_start_bytes = 6;

TLMMarker::TLMMarker(uint16_t numSignalledTiles)
    : markerManager_(std::make_unique<TLMMarkerManager>()), numSignalledTiles_(numSignalledTiles)
{
  tilePartsPerTile_.resize(numSignalledTiles_);
}

TLMMarker::TLMMarker(std::string& filePath, uint16_t numSignalledTiles, uint64_t tileStreamStart)
    : TLMMarker(numSignalledTiles)
{
  valid_ = false;
  auto serialized = TLMFile<TilePartLengthPOD>::load(filePath);
  if(serialized)
  {
    for(auto& tplp : serialized.value())
    {
      TilePartLength<uint32_t> tpl(tplp.tileIndex_, tplp.length_);
      add(tpl);
    }
    readComplete(tileStreamStart);
    valid_ = true;
  }
  else
  {
    filePath_ = filePath;
  }
}

TLMMarker::TLMMarker(IStream* stream) : TLMMarker(USHRT_MAX)
{
  stream_ = stream;
}

bool TLMMarker::valid() const noexcept
{
  return valid_;
}

void TLMMarker::invalidate() noexcept
{
  valid_ = false;
}

void TLMMarker::add(TilePartLength<uint32_t> tpl)
{
  // 1. add tile part
  auto& tpSeq = tilePartsPerTile_[tpl.tileIndex_];
  if(!tpSeq)
    tpSeq = std::make_unique<TPSeq>();
  // Note: at this stage, we don't know how many tile parts there are for this tile.
  // This number will eventually be calculated in tpSeq->complete()
  tpSeq->push_back(static_cast<uint8_t>(tpSeq->size()), 0, tilePartStart_, tpl.length_);

  // 2. add tile part to TLM markers
  markerManager_->push_back(tpl);

  // 3 increment start position
  tilePartStart_ += tpl.length_;
}

/**
 * @brief Reads and processes TLM marker from code stream
 * @param headerData Span of header data to read from
 * @param headerSize Size of the header data
 * @return true if successful, false on fatal error
 */
bool TLMMarker::read(std::span<uint8_t> headerData, uint16_t headerSize)
{
  if(headerSize < tlm_marker_start_bytes)
  {
    grklog.error("TLM: error reading marker - insufficient header size");
    return false;
  }

  // read TLM marker segment index
  uint8_t i_TLM = headerData[0];
  headerData = headerData.subspan(1);
  headerSize = static_cast<uint16_t>(headerSize - 1);

  // sanity check on i_TLM
  if(valid_ && !markerManager_->validateMarkerId(i_TLM))
    valid_ = false;

  // read and parse L parameter, which indicates number of bytes used to represent
  // remaining parameters
  uint8_t L = headerData[0];
  headerData = headerData.subspan(1);
  headerSize = static_cast<uint16_t>(headerSize - 1);
  // 0x70 == 1110000
  if((L & ~0x70) != 0)
  {
    grklog.warn("TLM: illegal L value. Disabling TLM");
    valid_ = false;
    return true; // Not a fatal error in original code
  }
  /*
   * 0 <= L_LTP <= 1
   *
   * 0 => 16 bit tile part lengths
   * 1 => 32 bit tile part lengths
   */
  uint8_t L_LTP = (L >> 6) & 0x1;
  uint8_t bytesPerTilePartLength = L_LTP ? 4U : 2U;
  /*
   * 0 <= L_iT <= 2
   *
   * 0 => no tile indices : if this is the case, there must be only
   * one tile part per tile, and all tile streams must appear in order
   * 1 => 1 byte tile indices
   * 2 => 2 byte tile indices
   */
  uint32_t L_iT = ((L >> 4) & 0x3);
  if(L_iT == 3)
  {
    grklog.warn("TLM: illegal L_it value of 3. Disabling TLM");
    valid_ = false;
    return true; // Not a fatal error in original code
  }

  // sanity check on tile indices
  if(markerManager_->empty())
  {
    hasTileIndices_ = (L_iT != 0);
  }
  else if(hasTileIndices_ ^ (L_iT != 0))
  {
    if(valid_)
    {
      grklog.warn("TLM: Cannot mix markers with and without tile part indices. Disabling TLM");
      valid_ = false;
    }
  }

  uint32_t quotient = bytesPerTilePartLength + L_iT;
  if(headerSize % quotient != 0)
  {
    grklog.error("TLM: error reading marker - header size not divisible by quotient");
    return false;
  }

  // note: each tile can have max 255 tile parts, but
  // the whole image with multiple tiles can have max 65535 tile parts
  size_t numTilePartsInMarker = headerSize / quotient;
  assert(numTilePartsInMarker <= USHRT_MAX);

  uint16_t Ttlm_i = 0;
  uint32_t Ptlm_i = 0;
  for(uint16_t i = 0; i < numTilePartsInMarker; ++i)
  {
    // read (global) tile index
    if(L_iT)
    {
      grk_read(headerData.data(), &Ttlm_i, L_iT);
      headerData = headerData.subspan(L_iT);
    }
    // read tile part length
    grk_read(headerData.data(), &Ptlm_i, bytesPerTilePartLength);
    if(Ptlm_i < 14)
    {
      if(valid_)
      {
        grklog.warn("TLM: tile part length %u is less than 14. Disabling TLM", Ptlm_i);
        valid_ = false;
      }
    }
    auto info = TilePartLength<uint32_t>(hasTileIndices_ ? Ttlm_i : tileCount_++, Ptlm_i);
    if(info.tileIndex_ >= numSignalledTiles_)
    {
      grklog.warn(
          "TLM: tile index %d out of bounds - signalled number of tiles equals %d. Disabling TLM",
          info.tileIndex_, numSignalledTiles_);
      valid_ = false;
    }
    if(valid_)
      add(info);

    headerData = headerData.subspan(bytesPerTilePartLength);
  }

  return true;
}

void TLMMarker::readComplete(uint64_t tileStreamStart) noexcept
{
  for(const auto& seq :
      tilePartsPerTile_ | std::views::filter([](const auto& ptr) { return ptr != nullptr; }))
  {
    seq->complete(tileStreamStart);
  }
}

void TLMMarker::rewind() noexcept
{
  if(!valid_)
    return;

  if(!std::ranges::all_of(tilePartsPerTile_,
                          [](const auto& tpSeq) { return tpSeq && !tpSeq->empty(); }))
  {
    grklog.warn("TLM: number of tiles in TLM markers does not match signalled number of "
                "tiles. Disabling TLM");
    valid_ = false;
    return;
  }

  markerManager_->reset();
}

const TPSEQ_VEC& TLMMarker::getTileParts(void) const
{
  return tilePartsPerTile_;
}

uint8_t TLMMarker::getNumTileParts(uint16_t tileIndex) const noexcept
{
  if(!valid_ || tileIndex >= tilePartsPerTile_.size())
    return 0;

  auto& partsPerTile = tilePartsPerTile_[tileIndex];
  return partsPerTile ? static_cast<uint8_t>(partsPerTile->size()) : 0;
}

/**
 * Query next TLM entry
 *
 * @param peek if false, then move to next TLM entry.
 * Otherwise, stay at current TLM entry
 *
 */
TilePartLength<uint32_t>* TLMMarker::next(bool peek)
{
  if(!valid_)
  {
    grklog.warn("Attempt to get next marker from invalid TLM marker");
    return nullptr;
  }

  auto tilePart = markerManager_->next(peek);
  if(tilePart)
  {
    // Check for out-of-bounds access
    if(tilePart->tileIndex_ >= numSignalledTiles_)
    {
      grklog.error("TLM entry tile index %d must be less than signalled number of tiles %d",
                   tilePart->tileIndex_, numSignalledTiles_);
      throw CorruptTLMException();
    }
  }

  return tilePart;
}

/**
 * Seek to next slated tile part.
 *
 */
void TLMMarker::seekNextSlated(TileWindow* tilesToDecompress, TileCache* tileCache, IStream* stream)
{
  assert(stream);

  auto currentPosition = stream->tell();

  // Lambda to restore position if an error occurs
  auto restore = [stream, currentPosition]() { stream->seek(currentPosition); };

  try
  {
    uint64_t skip = 0;
    while(auto tilePart = next(true))
    {
      if(tilePart->length_ == 0)
      {
        restore();
        grklog.error("corrupt TLM marker");
        throw CorruptTLMException();
      }

      // with TLM marker enabled, a tile will be in one of two states:
      // 1. no tile parts for this tile have been parsed
      // or
      // 2. all tile parts in the stream for this tile have been parsed
      if(tilesToDecompress->isSlated(tilePart->tileIndex_))
      {
        auto cacheEntry = tileCache->get(tilePart->tileIndex_);
        if(!cacheEntry || !cacheEntry->processor->allSOTMarkersParsed())
          break;
      }

      skip += tilePart->length_;
      next(false);
    }

    if(skip && !stream->seek(currentPosition + skip))
      throw CorruptTLMException();
  }
  catch(...)
  {
    restore();
    throw;
  }
}
/**
 * @brief Prepares to write TLM marker to code stream
 * @param numTilePartsTotal total number of tile parts in image
 * @return true if successful
 */
bool TLMMarker::writeBegin(uint32_t numTilePartsTotal)
{
  streamStart = stream_->tell();

  /* TLM */
  if(!stream_->write(TLM))
  {
    grklog.error("Failed to write TLM marker");
    return false;
  }

  /* Ltlm */
  uint32_t tlm_size = tlm_marker_start_bytes + tlmMarkerBytesPerTilePart * numTilePartsTotal;
  if(!stream_->write(static_cast<uint16_t>(tlm_size - MARKER_BYTES)))
  {
    grklog.error("Failed to write Ltlm length");
    return false;
  }

  /* Ztlm=0 */
  if(!stream_->write8u(0))
  {
    grklog.error("Failed to write Ztlm value");
    return false;
  }

  /* Stlm ST=1(8bits-255 tiles max), SP=1(Ptlm=32bits) */
  if(!stream_->write8u(0x60))
  {
    grklog.error("Failed to write Stlm value");
    return false;
  }

  /* make room for tile part lengths */
  if(!stream_->skip(tlmMarkerBytesPerTilePart * numTilePartsTotal))
  {
    grklog.error("Failed to skip space for tile part lengths");
    return false;
  }

  return true;
}

void TLMMarker::add(uint16_t tile_index, uint32_t tile_part_size) noexcept
{
  markerManager_->push_back(TilePartLength<uint32_t>(tile_index, tile_part_size));
}

/**
 * @brief Finalizes writing of TLM marker by updating tile part lengths
 * @return true if successful, false on fatal error
 */
bool TLMMarker::writeEnd()
{
  uint64_t current_position = stream_->tell();
  if(!stream_->seek(streamStart + tlm_marker_start_bytes))
  {
    grklog.error("Failed to seek to start of TLM marker data");
    return false;
  }

  markerManager_->reset();
  while(auto tilePart = markerManager_->next(false))
  {
    if(!stream_->write(tilePart->tileIndex_))
    {
      grklog.error("Failed to write tile index");
      return false;
    }
    if(!stream_->write(tilePart->length_))
    {
      grklog.error("Failed to write tile part length");
      return false;
    }
  }

  if(!stream_->seek(current_position))
  {
    grklog.error("Failed to seek back to current position");
    return false;
  }

  return true;
}

} // namespace grk