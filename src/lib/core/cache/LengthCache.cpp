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
 */

#include "grk_includes.h"

namespace grk
{
// TLM(2) + Ltlm(2) + Ztlm(1) + Stlm(1)
const uint32_t tlm_marker_start_bytes = 6;

MarkerInfo::MarkerInfo() : MarkerInfo(0, 0, 0) {}
MarkerInfo::MarkerInfo(uint16_t _id, uint64_t _pos, uint32_t _len) : id(_id), pos(_pos), len(_len)
{}
void MarkerInfo::dump(FILE* outputFileStream)
{
   fprintf(outputFileStream, "\t\t type=%#x, pos=%" PRIu64 ", len=%u\n", id, pos, len);
}
TilePartInfo::TilePartInfo(uint64_t start, uint64_t endHeader, uint64_t end)
	: startPosition(start), endHeaderPosition(endHeader), endPosition(end)
{}
TilePartInfo::TilePartInfo(void) : startPosition(0), endHeaderPosition(0), endPosition(0) {}
void TilePartInfo::dump(FILE* outputFileStream, uint8_t tilePart)
{
   std::stringstream ss;
   ss << "\t\t\t tile-part[" << tilePart << "]:"
	  << " star_pos=" << startPosition << ","
	  << " endHeaderPosition=" << endHeaderPosition << ","
	  << " endPosition=" << endPosition << std::endl;
   fprintf(outputFileStream, "%s", ss.str().c_str());
}
TileInfo::TileInfo(void)
	: tileno(0), numTileParts(0), allocatedTileParts(0), currentTilePart(0), tilePartInfo(nullptr),
	  markerInfo(nullptr), numMarkers(0), allocatedMarkers(0)
{
   allocatedMarkers = 100;
   numMarkers = 0;
   markerInfo = (MarkerInfo*)grk_calloc(allocatedMarkers, sizeof(MarkerInfo));
}
TileInfo::~TileInfo(void)
{
   delete[] tilePartInfo;
   grk_free(markerInfo);
}
bool TileInfo::checkResize(void)
{
   if(numMarkers + 1 > allocatedMarkers)
   {
	  auto oldMax = allocatedMarkers;
	  allocatedMarkers += 100U;
	  auto new_marker = (MarkerInfo*)grk_realloc(markerInfo, allocatedMarkers * sizeof(MarkerInfo));
	  if(!new_marker)
	  {
		 grk_free(markerInfo);
		 markerInfo = nullptr;
		 allocatedMarkers = 0;
		 numMarkers = 0;
		 Logger::logger_.error("Not enough memory to add TLM marker");
		 return false;
	  }
	  markerInfo = new_marker;
	  for(uint32_t i = oldMax; i < allocatedMarkers; ++i)
		 markerInfo[i] = MarkerInfo();
   }

   return true;
}
bool TileInfo::hasTilePartInfo(void)
{
   return tilePartInfo != nullptr;
}
bool TileInfo::update(uint16_t tileIndex, uint8_t currentTilePart, uint8_t numTileParts)
{
   tileno = tileIndex;
   if(!tilePartInfo)
   {
	  allocatedTileParts = numTileParts ? numTileParts : 10;
	  tilePartInfo = new TilePartInfo[allocatedTileParts];
   }
   else
   {
	  if(currentTilePart >= allocatedTileParts)
	  {
		 auto temp = new TilePartInfo[allocatedTileParts * 2];
		 for(uint8_t i = 0; i < allocatedTileParts; ++i)
			temp[i] = tilePartInfo[i];
		 delete[] tilePartInfo;
		 tilePartInfo = temp;
		 allocatedTileParts *= 2;
	  }
   }
   tilePartInfo[currentTilePart] = TilePartInfo(tileIndex, currentTilePart, numTileParts);

   return true;
}
TilePartInfo* TileInfo::getTilePartInfo(uint8_t tilePart)
{
   if(!tilePartInfo)
	  return nullptr;
   return tilePartInfo + tilePart;
}
void TileInfo::dump(FILE* outputFileStream, uint16_t tileNum)
{
   fprintf(outputFileStream, "\t\t nb of tile-part in tile [%u]=%u\n", tileNum, numTileParts);
   if(hasTilePartInfo())
   {
	  for(uint8_t tilePart = 0; tilePart < numTileParts; tilePart++)
		 getTilePartInfo(tilePart)->dump(outputFileStream, tilePart);
   }
   if(markerInfo)
   {
	  for(uint32_t markerNum = 0; markerNum < numMarkers; markerNum++)
		 markerInfo[markerNum].dump(outputFileStream);
   }
}
CodeStreamInfo::CodeStreamInfo(BufferedStream* str)
	: mainHeaderStart(0), mainHeaderEnd(0), tileInfo(nullptr), numTiles(0), stream(str)
{}
CodeStreamInfo::~CodeStreamInfo()
{
   for(const auto& m : marker)
	  delete m;
   delete[] tileInfo;
}
bool CodeStreamInfo::allocTileInfo(uint16_t ntiles)
{
   if(tileInfo)
	  return true;
   numTiles = ntiles;
   tileInfo = new TileInfo[numTiles];
   return true;
}
bool CodeStreamInfo::updateTileInfo(uint16_t tileIndex, uint8_t currentTilePart,
									uint8_t numTileParts)
{
   assert(tileInfo != nullptr);
   return tileInfo[tileIndex].update(tileIndex, currentTilePart, numTileParts);
}
TileInfo* CodeStreamInfo::getTileInfo(uint16_t tileIndex)
{
   if(!tileInfo || tileIndex >= numTiles)
	  return nullptr;

   return tileInfo + tileIndex;
}
void CodeStreamInfo::dump(FILE* outputFileStream)
{
   fprintf(outputFileStream, "Codestream index from main header: {\n");
   std::stringstream ss;
   ss << "\t Main header start position=" << mainHeaderStart << std::endl
	  << "\t Main header end position=" << mainHeaderEnd << std::endl;
   fprintf(outputFileStream, "%s", ss.str().c_str());
   fprintf(outputFileStream, "\t Marker list: {\n");
   for(auto& m : marker)
	  m->dump(outputFileStream);
   fprintf(outputFileStream, "\t }\n");
   if(tileInfo)
   {
	  uint8_t numTilePartsTotal = 0;
	  for(uint16_t i = 0; i < numTiles; i++)
		 numTilePartsTotal += getTileInfo(i)->numTileParts;
	  if(numTilePartsTotal)
	  {
		 fprintf(outputFileStream, "\t Tile index: {\n");
		 for(uint16_t i = 0; i < numTiles; i++)
		 {
			auto tileInfo = getTileInfo(i);
			tileInfo->dump(outputFileStream, i);
		 }
		 fprintf(outputFileStream, "\t }\n");
	  }
   }
   fprintf(outputFileStream, "}\n");
}
void CodeStreamInfo::pushMarker(uint16_t id, uint64_t pos, uint32_t len)
{
   marker.push_back(new MarkerInfo(id, pos, len));
}
uint64_t CodeStreamInfo::getMainHeaderStart(void)
{
   return mainHeaderStart;
}
void CodeStreamInfo::setMainHeaderStart(uint64_t start)
{
   this->mainHeaderStart = start;
}
uint64_t CodeStreamInfo::getMainHeaderEnd(void)
{
   return mainHeaderEnd;
}
void CodeStreamInfo::setMainHeaderEnd(uint64_t end)
{
   mainHeaderEnd = end;
}
bool CodeStreamInfo::seekFirstTilePart(uint16_t tileIndex)
{
   // no need to seek if we haven't parsed any tiles yet
   bool hasVeryFirstTilePartInfo = tileInfo && (tileInfo + 0)->hasTilePartInfo();
   if(!hasVeryFirstTilePartInfo)
	  return true;

   auto tileInfoForTile = getTileInfo(tileIndex);
   assert(tileInfoForTile && tileInfoForTile->numTileParts);
   // move just past SOT marker of first tile part for this tile
   if(!(stream->seek(tileInfoForTile->getTilePartInfo(0)->startPosition + MARKER_BYTES)))
   {
	  Logger::logger_.error("Error in seek");
	  return false;
   }

   return true;
}
TilePartLengthInfo::TilePartLengthInfo() : TilePartLengthInfo(0, 0) {}
TilePartLengthInfo::TilePartLengthInfo(uint16_t tileno, uint32_t len)
	: tileIndex_(tileno), length_(len)
{}
TileLengthMarkers::TileLengthMarkers(uint16_t numSignalledTiles)
	: markers_(new TL_MAP()), markerIt_(markers_->end()), markerTilePartIndex_(0),
	  curr_vec_(nullptr), stream_(nullptr), streamStart(0), valid_(true), hasTileIndices_(false),
	  tileCount_(0), numSignalledTiles_(numSignalledTiles)
{}
TileLengthMarkers::TileLengthMarkers(BufferedStream* stream) : TileLengthMarkers(USHRT_MAX)
{
   stream_ = stream;
}
TileLengthMarkers::~TileLengthMarkers()
{
   if(markers_)
   {
	  for(auto it = markers_->begin(); it != markers_->end(); it++)
		 delete it->second;
	  delete markers_;
   }
}

// second validation level should be to compare TLM tile length against length
// signaled in SOT marker - they should be equal. We don't do this for performance
// reasons.
bool TileLengthMarkers::valid(void)
{
   return valid_;
}
void TileLengthMarkers::invalidate(void)
{
   valid_ = false;
}

bool TileLengthMarkers::read(uint8_t* headerData, uint16_t header_size)
{
   assert(markers_);
   if(header_size < tlm_marker_start_bytes)
   {
	  Logger::logger_.error("TLM: error reading marker");
	  return false;
   }
   // read TLM marker segment index
   uint8_t i_TLM = *headerData++;
   header_size = (uint16_t)(header_size - 1);
   markerIt_ = markers_->find(i_TLM);
   if(markerIt_ != markers_->end())
   {
	  if(valid_)
	  {
		 Logger::logger_.warn("TLM: each marker index must be unique. Disabling TLM");
		 valid_ = false;
	  }
   }

   // read and parse L parameter, which indicates number of bytes used to represent
   // remaining parameters
   uint8_t L = *headerData++;
   header_size = (uint16_t)(header_size - 1);
   // 0x70 ==  1110000
   if((L & ~0x70) != 0)
   {
	  Logger::logger_.error("TLM: illegal L value");
	  return false;
   }
   /*
	* 0 <= L_LTP <= 1
	*
	* 0 => 16 bit tile part lengths
	* 1 => 32 bit tile part lengths
	*/
   uint32_t L_LTP = (L >> 6) & 0x1;
   uint32_t bytesPerTilePartLength = L_LTP ? 4U : 2U;
   /*
	* 0 <= L_iT <= 2
	*
	* 0 => no tile indices
	* 1 => 1 byte tile indices
	* 2 => 2 byte tile indices
	*/
   uint32_t L_iT = ((L >> 4) & 0x3);

   // sanity check on tile indices
   if(markers_->empty())
   {
	  hasTileIndices_ = L_iT != 0;
   }
   else if((hasTileIndices_ && L_iT == 0) || (!hasTileIndices_ && L_iT != 0))
   {
	  if(valid_)
	  {
		 Logger::logger_.warn(
			 "TLM: Cannot mix markers with and without tile part indices. Disabling TLM");
		 valid_ = false;
	  }
   }

   uint32_t quotient = bytesPerTilePartLength + L_iT;
   if(header_size % quotient != 0)
   {
	  Logger::logger_.error("TLM: error reading marker");
	  return false;
   }
   // note: each tile can have max 255 tile parts, but
   // the whole image with multiple tiles can have max 65535 tile parts
   size_t numTileParts = (uint8_t)(header_size / quotient);

   uint32_t Ttlm_i = 0, Ptlm_i = 0;
   for(size_t i = 0; i < numTileParts; ++i)
   {
	  // read (global) tile index
	  if(L_iT)
	  {
		 grk_read<uint32_t>(headerData, &Ttlm_i, L_iT);
		 headerData += L_iT;
	  }
	  // read tile part length
	  grk_read<uint32_t>(headerData, &Ptlm_i, bytesPerTilePartLength);
	  if(Ptlm_i < 14)
	  {
		 if(valid_)
		 {
			Logger::logger_.warn("TLM: tile part length %u is less than 14. Disabling TLM", Ptlm_i);
			valid_ = false;
		 }
	  }
	  auto info = hasTileIndices_ ? TilePartLengthInfo((uint16_t)Ttlm_i, Ptlm_i)
								  : TilePartLengthInfo(tileCount_++, Ptlm_i);
	  push(i_TLM, info);
	  headerData += bytesPerTilePartLength;
   }

   return true;
}
void TileLengthMarkers::push(uint8_t i_TLM, TilePartLengthInfo info)
{
   markerIt_ = markers_->find(i_TLM);
   if(markerIt_ != markers_->end())
   {
	  markerIt_->second->push_back(info);
   }
   else
   {
	  auto vec = new TL_INFO_VEC();
	  vec->push_back(info);
	  markers_->operator[](i_TLM) = vec;
	  markerIt_ = markers_->find(i_TLM);
   }
}
void TileLengthMarkers::rewind(void)
{
   markerTilePartIndex_ = 0;
   curr_vec_ = nullptr;
   if(markers_)
   {
	  markerIt_ = markers_->begin();
	  if(markerIt_ != markers_->end())
		 curr_vec_ = markerIt_->second;
   }
}
TilePartLengthInfo* TileLengthMarkers::next(void)
{
   return next(false);
}
/**
 * Query next TLM entry
 *
 * @param peek if false, then move to next TLM entry.
 * Otherwise, stay at current TLM entry
 *
 */
TilePartLengthInfo* TileLengthMarkers::next(bool peek)
{
   assert(markers_);
   if(!valid_)
   {
	  Logger::logger_.warn("Attempt to get next marker from invalid TLM marker");
	  return nullptr;
   }
   if(curr_vec_)
   {
	  if(markerTilePartIndex_ == curr_vec_->size())
	  {
		 markerIt_++;
		 if(markerIt_ != markers_->end())
		 {
			curr_vec_ = markerIt_->second;
			markerTilePartIndex_ = 0;
		 }
		 else
		 {
			curr_vec_ = nullptr;
		 }
	  }
	  if(curr_vec_)
	  {
		 auto rc = &curr_vec_->operator[](markerTilePartIndex_);
		 if(rc->tileIndex_ >= numSignalledTiles_)
		 {
			Logger::logger_.error(
				"TLM entry tile index %d must be less than signalled number of tiles %d",
				rc->tileIndex_, numSignalledTiles_);
			throw CorruptTLMException();
		 }
		 if(!peek)
			markerTilePartIndex_++;
		 return rc;
	  }
   }
   return nullptr;
}
/**
 * Seek to next scheduled tile part.
 *
 * return false if TLM marker is corrupt, otherwise false
 */
void TileLengthMarkers::seek(TileSet* tilesToDecompress, CodingParams* cp, BufferedStream* stream)
{
   assert(stream);
   // peek at tile part
   auto tilePart = next(true);
   uint64_t skip = 0;
   auto currentPosition = stream->tell();
   while(tilePart && !tilesToDecompress->isScheduled(tilePart->tileIndex_))
   {
	  if(tilePart->length_ == 0)
	  {
		 stream->seek(currentPosition);
		 Logger::logger_.error("corrupt TLM marker");
		 throw CorruptTLMException();
	  }
	  skip += tilePart->length_;
	  // Logger::logger_.info("Skipped tile part from tile %u",tilePart->tileIndex_);
	  auto tcp = cp->tcps + tilePart->tileIndex_;
	  // increment tile part counter (unable to validate with SOT marker)
	  tcp->tilePartCounter_++;
	  // increment TLM
	  next(false);
	  // peek at next tile part
	  tilePart = next(true);
   }
   if(skip && !stream->seek(stream->tell() + skip))
	  throw CorruptTLMException();
}

bool TileLengthMarkers::writeBegin(uint16_t numTilePartsTotal)
{
   streamStart = stream_->tell();

   /* TLM */
   if(!stream_->writeShort(J2K_MS_TLM))
	  return false;

   /* Ltlm */
   uint32_t tlm_size = tlm_marker_start_bytes + tlmMarkerBytesPerTilePart * numTilePartsTotal;
   if(!stream_->writeShort((uint16_t)(tlm_size - MARKER_BYTES)))
	  return false;

   /* Ztlm=0*/
   if(!stream_->writeByte(0))
	  return false;

   /* Stlm ST=1(8bits-255 tiles max),SP=1(Ptlm=32bits) */
   if(!stream_->writeByte(0x60))
	  return false;

   /* make room for tile part lengths */
   return stream_->skip(tlmMarkerBytesPerTilePart * numTilePartsTotal);
}
void TileLengthMarkers::push(uint16_t tileIndex, uint32_t tile_part_size)
{
   push((uint8_t)markerIt_->first, TilePartLengthInfo(tileIndex, tile_part_size));
}
bool TileLengthMarkers::writeEnd(void)
{
   uint64_t current_position = stream_->tell();
   if(!stream_->seek(streamStart + tlm_marker_start_bytes))
	  return false;
   for(auto it = markers_->begin(); it != markers_->end(); it++)
   {
	  auto lengths = it->second;
	  for(auto info = lengths->begin(); info != lengths->end(); ++info)
	  {
		 stream_->writeShort(info->tileIndex_);
		 stream_->writeInt(info->length_);
	  }
   }

   return stream_->seek(current_position);
}
bool TileLengthMarkers::addTileMarkerInfo(uint16_t tileno, CodeStreamInfo* codestreamInfo,
										  uint16_t id, uint64_t pos, uint32_t len)
{
   assert(codestreamInfo);
   if(id == J2K_MS_SOT)
   {
	  auto currTileInfo = codestreamInfo->getTileInfo(tileno);
	  auto tilePartInfo = currTileInfo->getTilePartInfo(currTileInfo->currentTilePart);
	  if(tilePartInfo)
		 tilePartInfo->startPosition = pos;
   }
   codestreamInfo->pushMarker(id, pos, len);

   return true;
}

PacketInfo::PacketInfo(void) : packetLength(0) {}
PacketInfoCache::~PacketInfoCache()
{
   for(const auto& p : packetInfo)
	  delete p;
}

} // namespace grk
