/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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
	fprintf(outputFileStream, "\t\t type=%#x, pos=%" PRIu64 ", len=%d\n", id, pos, len);
}
TilePartInfo::TilePartInfo(uint64_t start, uint64_t endHeader, uint64_t end)
	: startPosition(start), endHeaderPosition(endHeader), endPosition(end)
{}
TilePartInfo::TilePartInfo(void) : TilePartInfo(0, 0, 0) {}
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
	markerInfo = (MarkerInfo*)grkCalloc(allocatedMarkers, sizeof(MarkerInfo));
}
TileInfo::~TileInfo(void)
{
	grkFree(tilePartInfo);
	grkFree(markerInfo);
}
bool TileInfo::checkResize(void)
{
	if(numMarkers + 1 > allocatedMarkers)
	{
		auto oldMax = allocatedMarkers;
		allocatedMarkers += 100U;
		auto new_marker =
			(MarkerInfo*)grkRealloc(markerInfo, allocatedMarkers * sizeof(MarkerInfo));
		if(!new_marker)
		{
			grkFree(markerInfo);
			markerInfo = nullptr;
			allocatedMarkers = 0;
			numMarkers = 0;
			GRK_ERROR("Not enough memory to add TLM marker");
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
	if(numTileParts != 0)
	{
		allocatedTileParts = numTileParts;
		if(!tilePartInfo)
		{
			tilePartInfo = (TilePartInfo*)grkCalloc(numTileParts, sizeof(TilePartInfo));
			if(!tilePartInfo)
			{
				GRK_ERROR("Not enough memory to read SOT marker. "
						  "Tile index allocation failed");
				return false;
			}
		}
		else
		{
			auto newTilePartIndex =
				(TilePartInfo*)grkRealloc(tilePartInfo, numTileParts * sizeof(TilePartInfo));
			if(!newTilePartIndex)
			{
				grkFree(tilePartInfo);
				tilePartInfo = nullptr;
				GRK_ERROR("Not enough memory to read SOT marker. "
						  "Tile index allocation failed");
				return false;
			}
			tilePartInfo = newTilePartIndex;
		}
	}
	else
	{
		if(!tilePartInfo)
		{
			allocatedTileParts = 10;
			tilePartInfo = (TilePartInfo*)grkCalloc(allocatedTileParts, sizeof(TilePartInfo));
			if(!tilePartInfo)
			{
				allocatedTileParts = 0;
				GRK_ERROR("Not enough memory to read SOT marker. "
						  "Tile index allocation failed");
				return false;
			}
		}

		if(currentTilePart >= allocatedTileParts)
		{
			TilePartInfo* newTilePartIndex;
			allocatedTileParts = currentTilePart + 1U;
			newTilePartIndex =
				(TilePartInfo*)grkRealloc(tilePartInfo, allocatedTileParts * sizeof(TilePartInfo));
			if(!newTilePartIndex)
			{
				grkFree(tilePartInfo);
				tilePartInfo = nullptr;
				allocatedTileParts = 0;
				GRK_ERROR("Not enough memory to read SOT marker. Tile index allocation failed");
				return false;
			}
			tilePartInfo = newTilePartIndex;
		}
	}

	return true;
}
TilePartInfo* TileInfo::getTilePartInfo(uint8_t tilePart)
{
	if(!tilePartInfo)
		return nullptr;
	return &tilePartInfo[tilePart];
}
void TileInfo::dump(FILE* outputFileStream, uint16_t tileNum)
{
	fprintf(outputFileStream, "\t\t nb of tile-part in tile [%u]=%u\n", tileNum, numTileParts);
	if(hasTilePartInfo())
	{
		for(uint8_t tilePart = 0; tilePart < numTileParts; tilePart++)
		{
			auto tilePartInfo = getTilePartInfo(tilePart);
			tilePartInfo->dump(outputFileStream, tilePart);
		}
	}
	if(markerInfo)
	{
		for(uint32_t markerNum = 0; markerNum < numMarkers; markerNum++)
		{
			markerInfo[markerNum].dump(outputFileStream);
		}
	}
}
CodeStreamInfo::CodeStreamInfo(BufferedStream* str)
	: mainHeaderStart(0), mainHeaderEnd(0), numTiles(0), tileInfo(nullptr), stream(str)
{}
CodeStreamInfo::~CodeStreamInfo()
{
	for(auto& m : marker)
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
	assert(tileIndex < numTiles);
	assert(tileInfo);
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
	this->mainHeaderEnd = end;
}
bool CodeStreamInfo::skipToTile(uint16_t tileIndex, uint64_t lastSotReadPosition)
{
	if(tileInfo && tileInfo->hasTilePartInfo())
	{
		auto tileInfoForTile = getTileInfo(tileIndex);
		if(!tileInfoForTile->numTileParts)
		{
			/* the index for this tile has not been built,
			 *  so move to the last SOT read */
			if(!(stream->seek(lastSotReadPosition + 2)))
			{
				GRK_ERROR("Problem with seek function");
				return false;
			}
		}
		else
		{
			if(!(stream->seek(tileInfoForTile->getTilePartInfo(0)->startPosition + 2)))
			{
				GRK_ERROR("Problem with seek function");
				return false;
			}
		}
	}

	return true;
}
TileLengthMarkers::TileLengthMarkers()
	: m_markers(new TL_MAP()), m_markerIndex(0), m_markerTilePartIndex(0), m_curr_vec(nullptr),
	  m_stream(nullptr), streamStart(0)
{}
TileLengthMarkers::TileLengthMarkers(BufferedStream* stream) : TileLengthMarkers()
{
	m_stream = stream;
}
TileLengthMarkers::~TileLengthMarkers()
{
	if(m_markers)
	{
		for(auto it = m_markers->begin(); it != m_markers->end(); it++)
		{
			delete it->second;
		}
		delete m_markers;
	}
}
bool TileLengthMarkers::read(uint8_t* p_header_data, uint16_t header_size)
{
	if(header_size < tlm_marker_start_bytes)
	{
		GRK_ERROR("Error reading TLM marker");
		return false;
	}
	uint8_t i_TLM, L;
	uint32_t L_iT, L_LTP;

	// correct for length of marker
	header_size = (uint16_t)(header_size - 2);
	// read TLM marker segment index
	i_TLM = *p_header_data++;
	// read and parse L parameter, which indicates number of bytes used to represent
	// remaining parameters
	L = *p_header_data++;
	// 0x70 ==  1110000
	if((L & ~0x70) != 0)
	{
		GRK_ERROR("Illegal L value in TLM marker");
		return false;
	}
	/*
	 * 0 <= L_LTP <= 1
	 *
	 * 0 => 16 bit tile part lengths
	 * 1 => 32 bit tile part lengths
	 */
	L_LTP = (L >> 6) & 0x1;
	uint32_t bytesPerTilePartLength = L_LTP ? 4U : 2U;
	/*
	 * 0 <= L_iT <= 2
	 *
	 * 0 => no tile part indices
	 * 1 => 1 byte tile part indices
	 * 2 => 2 byte tile part indices
	 */
	L_iT = ((L >> 4) & 0x3);
	uint32_t quotient = bytesPerTilePartLength + L_iT;
	if(header_size % quotient != 0)
	{
		GRK_ERROR("Error reading TLM marker");
		return false;
	}
	// note: each tile can have max 255 tile parts, but
	// the whole image with multiple tiles can have max 65535 tile parts
	size_t num_tp = (uint8_t)(header_size / quotient);

	uint32_t Ttlm_i = 0, Ptlm_i = 0;
	for(size_t i = 0; i < num_tp; ++i)
	{
		// read (global) tile part index
		if(L_iT)
		{
			grk_read<uint32_t>(p_header_data, &Ttlm_i, L_iT);
			p_header_data += L_iT;
		}
		// read tile part length
		grk_read<uint32_t>(p_header_data, &Ptlm_i, bytesPerTilePartLength);
		auto info =
			L_iT ? TilePartLengthInfo((uint16_t)Ttlm_i, Ptlm_i) : TilePartLengthInfo(Ptlm_i);
		push(i_TLM, info);
		p_header_data += bytesPerTilePartLength;
	}

	return true;
}
void TileLengthMarkers::push(uint8_t i_TLM, TilePartLengthInfo info)
{
	auto pair = m_markers->find(i_TLM);

	if(pair != m_markers->end())
	{
		pair->second->push_back(info);
	}
	else
	{
		auto vec = new TL_INFO_VEC();
		vec->push_back(info);
		m_markers->operator[](i_TLM) = vec;
	}
}
void TileLengthMarkers::rewind(void)
{
	m_markerIndex = 0;
	m_markerTilePartIndex = 0;
	m_curr_vec = nullptr;
	if(m_markers)
	{
		auto pair = m_markers->find(0);
		if(pair != m_markers->end())
			m_curr_vec = pair->second;
	}
}
TilePartLengthInfo TileLengthMarkers::getNext(void)
{
	if(!m_markers)
		return 0;
	if(m_curr_vec)
	{
		if(m_markerTilePartIndex == m_curr_vec->size())
		{
			m_markerIndex++;
			if(m_markerIndex < m_markers->size())
			{
				m_curr_vec = m_markers->operator[](m_markerIndex);
				m_markerTilePartIndex = 0;
			}
			else
			{
				m_curr_vec = nullptr;
			}
		}
		if(m_curr_vec)
			return m_curr_vec->operator[](m_markerTilePartIndex++);
	}
	return 0;
}
bool TileLengthMarkers::skipTo(uint16_t skipTileIndex, BufferedStream* stream, uint64_t firstSotPos)
{
	assert(stream);
	rewind();
	auto tl = getNext();
	uint16_t tileIndex = 0;
	uint64_t skip = 0;
	while(tileIndex != skipTileIndex)
	{
		if(tl.length == 0)
		{
			GRK_ERROR("corrupt TLM marker");
			return false;
		}
		skip += tl.length;
		tl = getNext();
		tileIndex = (uint16_t)(tl.hasTileIndex ? tl.tileIndex : tileIndex + 1U);
	}

	return stream->seek(firstSotPos + skip);
}
bool TileLengthMarkers::writeBegin(uint16_t numTilePartsTotal)
{
	uint32_t tlm_size = tlm_marker_start_bytes + tlmMarkerBytesPerTilePart * numTilePartsTotal;

	streamStart = m_stream->tell();

	/* TLM */
	if(!m_stream->write_short(J2K_MS_TLM))
		return false;

	/* Ltlm */
	if(!m_stream->write_short((uint16_t)(tlm_size - 2)))
		return false;

	/* Ztlm=0*/
	if(!m_stream->write_byte(0))
		return false;

	/* Stlm ST=1(8bits-255 tiles max),SP=1(Ptlm=32bits) */
	if(!m_stream->write_byte(0x60))
		return false;

	/* make room for tile part lengths */
	return m_stream->skip(tlmMarkerBytesPerTilePart * numTilePartsTotal);
}
void TileLengthMarkers::writeUpdate(uint16_t tileIndex, uint32_t tile_part_size)
{
	push(m_markerIndex, TilePartLengthInfo(tileIndex, tile_part_size));
}
bool TileLengthMarkers::writeEnd(void)
{
	uint64_t tlm_position = streamStart + tlm_marker_start_bytes;
	uint64_t current_position = m_stream->tell();

	if(!m_stream->seek(tlm_position))
		return false;

	for(auto it = m_markers->begin(); it != m_markers->end(); it++)
	{
		auto lengths = it->second;
		for(auto info = lengths->begin(); info != lengths->end(); ++info)
		{
			if(info->hasTileIndex)
				m_stream->write_short(info->tileIndex);
			m_stream->write_int(info->length);
		}
	}

	return m_stream->seek(current_position);
}
bool TileLengthMarkers::addTileMarkerInfo(uint16_t tileno, CodeStreamInfo* codestreamInfo,
										  uint16_t id, uint64_t pos, uint32_t len)
{
	assert(codestreamInfo);
	auto currTileInfo = codestreamInfo->getTileInfo(tileno);
	if(id == J2K_MS_SOT)
	{
		uint8_t currTilePart = (uint8_t)currTileInfo->currentTilePart;
		auto tilePartInfo = currTileInfo->getTilePartInfo(currTilePart);
		if(tilePartInfo)
			tilePartInfo->startPosition = pos;
	}

	codestreamInfo->pushMarker(id, pos, len);

	return true;
}

PacketInfo::PacketInfo(void) : headerLength(0), packetLength(0), parsedData(false) {}
PacketInfoCache::~PacketInfoCache()
{
	for(auto& p : packetInfo)
		delete p;
}
uint32_t PacketInfo::getPacketDataLength(void)
{
	return packetLength - headerLength;
}

} // namespace grk
