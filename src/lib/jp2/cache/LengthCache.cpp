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

namespace grk {

// bytes available in PLT marker to store packet lengths
// (4 bytes are reserved for (marker + marker length), and 1 byte for index)
const uint32_t available_packet_len_bytes_per_plt = USHRT_MAX - 1 - 4;

// minimum number of packet lengths that can be stored in a full
// length PLT marker
// (5 is maximum number of bytes for a single packet length)
const uint32_t min_packets_per_full_plt = available_packet_len_bytes_per_plt / 5;

// TLM(2) + Ltlm(2) + Ztlm(1) + Stlm(1)
const uint32_t tlm_marker_start_bytes = 6;

MarkerInfo::MarkerInfo() : MarkerInfo(0,0,0){
}
MarkerInfo::MarkerInfo(uint16_t _id,
						uint64_t _pos,
						uint32_t _len) : id(_id),
										pos(_pos),
										len(_len){
}
void MarkerInfo::dump(FILE *out_stream){
	fprintf(out_stream, "\t\t type=%#x, pos=%" PRIu64", len=%d\n",id,	pos,len);
}
TilePartInfo::TilePartInfo(uint64_t start, uint64_t endHeader, uint64_t end) : startPosition(start),
																				endHeaderPosition(endHeader),
																				endPosition(end){
}
TilePartInfo::TilePartInfo(void) : TilePartInfo(0,0,0) {
}
void TilePartInfo::dump(FILE *out_stream, uint8_t tilePart){
	std::stringstream ss;
	ss << "\t\t\t tile-part[" << tilePart << "]:"
			<< " star_pos="
			<< startPosition
			<< "," << " endHeaderPosition="
			<< endHeaderPosition
			<< "," << " endPosition="
			<< endPosition
			<< std::endl;
	fprintf(out_stream, "%s", ss.str().c_str());
}
TileInfo::TileInfo(void) : tileno(0),
				numTileParts(0),
				allocatedTileParts(0),
				currentTilePart(0),
				tilePartInfo(nullptr),
				markerInfo(nullptr),
				numMarkers(0),
				allocatedMarkers(0){
	allocatedMarkers = 100;
	numMarkers = 0;
	markerInfo =(MarkerInfo*) grkCalloc(allocatedMarkers,sizeof(MarkerInfo));
}
TileInfo::~TileInfo(void){
	grkFree(tilePartInfo);
	grkFree(markerInfo);
}
bool TileInfo::checkResize(void){
	if (numMarkers + 1 > allocatedMarkers) {
		auto oldMax = allocatedMarkers;
		allocatedMarkers += 100U;
		auto new_marker = (MarkerInfo*) grkRealloc(markerInfo,
													allocatedMarkers* sizeof(MarkerInfo));
		if (!new_marker) {
			grkFree(markerInfo);
			markerInfo = nullptr;
			allocatedMarkers = 0;
			numMarkers = 0;
			GRK_ERROR("Not enough memory to add TLM marker");
			return false;
		}
		markerInfo = new_marker;
		for (uint32_t i = oldMax; i < allocatedMarkers; ++i)
			markerInfo[i] = MarkerInfo();
	}

	return true;
}
bool TileInfo::hasTilePartInfo(void){
	return tilePartInfo != nullptr;
}
bool TileInfo::update(uint16_t tileNumber,uint8_t currentTilePart, uint8_t numTileParts){
	tileno = tileNumber;
	if (numTileParts != 0) {
		allocatedTileParts =	numTileParts;
		if (!tilePartInfo) {
			tilePartInfo =
					(TilePartInfo*) grkCalloc(numTileParts,sizeof(TilePartInfo));
			if (!tilePartInfo) {
				GRK_ERROR("Not enough memory to read SOT marker. "
						"Tile index allocation failed");
				return false;
			}
		} else {
			auto newTilePartIndex = (TilePartInfo*) grkRealloc(
					tilePartInfo,
					numTileParts * sizeof(TilePartInfo));
			if (!newTilePartIndex) {
				grkFree(tilePartInfo);
				tilePartInfo =
						nullptr;
				GRK_ERROR("Not enough memory to read SOT marker. "
						"Tile index allocation failed");
				return false;
			}
			tilePartInfo =
					newTilePartIndex;
		}
	} else {
		if (!tilePartInfo) {
			allocatedTileParts = 10;
			tilePartInfo =
					(TilePartInfo*) grkCalloc(
							allocatedTileParts,
							sizeof(TilePartInfo));
			if (!tilePartInfo) {
				allocatedTileParts =	0;
				GRK_ERROR("Not enough memory to read SOT marker. "
						"Tile index allocation failed");
				return false;
			}
		}

		if (currentTilePart
				>= allocatedTileParts) {
			TilePartInfo *newTilePartIndex;
			allocatedTileParts =	currentTilePart + 1U;
			newTilePartIndex =
					(TilePartInfo*) grkRealloc(tilePartInfo,
							allocatedTileParts
									* sizeof(TilePartInfo));
			if (!newTilePartIndex) {
				grkFree(tilePartInfo);
				tilePartInfo =
						nullptr;
				allocatedTileParts =	0;
				GRK_ERROR("Not enough memory to read SOT marker. Tile index allocation failed");
				return false;
			}
			tilePartInfo = newTilePartIndex;
		}
	}

	return true;
}
TilePartInfo* TileInfo::getTilePartInfo(uint8_t tilePart){
	if (!tilePartInfo)
		return nullptr;
	return &tilePartInfo[tilePart];
}
void TileInfo::dump(FILE *out_stream, uint16_t tileNum){
	fprintf(out_stream, "\t\t nb of tile-part in tile [%u]=%u\n",
			tileNum, numTileParts);
	if (hasTilePartInfo()) {
		for (uint8_t tilePart = 0; tilePart < numTileParts;tilePart++) {
			auto tilePartInfo = getTilePartInfo(tilePart);
			tilePartInfo->dump(out_stream, tilePart);
		}
	}
	if (markerInfo) {
		for (uint32_t markerNum = 0;markerNum < numMarkers;markerNum++) {
			markerInfo[markerNum].dump(out_stream);
		}
	}
}
CodeStreamInfo::CodeStreamInfo(): mainHeaderStart(0),
								mainHeaderEnd(0),
								numTiles(0),
								tileInfo(nullptr){
}
CodeStreamInfo::~CodeStreamInfo(){
	for (auto &m : marker)
		delete m;
	delete[] tileInfo;
}
bool CodeStreamInfo::allocTileInfo(uint16_t ntiles){
	if (tileInfo)
		return true;
	numTiles = ntiles;
	tileInfo = new TileInfo[numTiles];
	return true;
}
bool CodeStreamInfo::updateTileInfo(uint16_t tileNumber, uint8_t currentTilePart, uint8_t numTileParts){
	assert(tileInfo != nullptr);
	return tileInfo[tileNumber].update(tileNumber, currentTilePart, numTileParts);
}
TileInfo* CodeStreamInfo::getTileInfo(uint16_t tileNumber){
	assert(tileNumber < numTiles);
	return tileInfo + tileNumber;
}
bool CodeStreamInfo::hasTileInfo(void){
	return tileInfo != nullptr;
}
void CodeStreamInfo::dump(FILE *out_stream) {
	fprintf(out_stream, "Codestream index from main header: {\n");
	std::stringstream ss;
	ss << "\t Main header start position=" << mainHeaderStart
			<< std::endl << "\t Main header end position="
			<< mainHeaderEnd << std::endl;
	fprintf(out_stream, "%s", ss.str().c_str());
	fprintf(out_stream, "\t Marker list: {\n");
	for (auto &m : marker)
		m->dump(out_stream);
	fprintf(out_stream, "\t }\n");
	if (tileInfo) {
		uint8_t totalTileParts = 0;
		for (uint16_t i = 0; i < numTiles; i++)
			totalTileParts += getTileInfo(i)->numTileParts;
		if (totalTileParts) {
			fprintf(out_stream, "\t Tile index: {\n");
			for (uint16_t i = 0; i < numTiles; i++) {
				auto tileInfo = getTileInfo(i);
				tileInfo->dump(out_stream, i);
			}
			fprintf(out_stream, "\t }\n");
		}
	}
	fprintf(out_stream, "}\n");
}
void CodeStreamInfo::pushMarker(uint16_t id,uint64_t pos,uint32_t len){
	marker.push_back(new MarkerInfo(id,pos,len));
}
uint64_t CodeStreamInfo::getMainHeaderStart(void){
	return mainHeaderStart;
}
void CodeStreamInfo::setMainHeaderStart(uint64_t start){
	this->mainHeaderStart = start;
}
uint64_t CodeStreamInfo::getMainHeaderEnd(void){
	return mainHeaderEnd;
}
void CodeStreamInfo::setMainHeaderEnd(uint64_t end){
	this->mainHeaderEnd = end;
}
TileLengthMarkers::TileLengthMarkers() :
		m_markers(new TL_MAP()),
		m_markerIndex(0),
		m_markerTilePartIndex(0),
		m_curr_vec(nullptr),
		m_stream(nullptr),
		m_tlm_start_stream_position(0) {
}
TileLengthMarkers::TileLengthMarkers(BufferedStream *stream): TileLengthMarkers() {
	m_stream = stream;
}
TileLengthMarkers::~TileLengthMarkers() {
	if (m_markers) {
		for (auto it = m_markers->begin(); it != m_markers->end(); it++) {
			delete it->second;
		}
		delete m_markers;
	}
}
bool TileLengthMarkers::read(uint8_t *p_header_data, uint16_t header_size){
	if (header_size < tlm_marker_start_bytes) {
		GRK_ERROR("Error reading TLM marker");
		return false;
	}
	uint8_t i_TLM, L;
	uint32_t L_iT, L_LTP;

	// correct for length of marker
	header_size = (uint16_t) (header_size - 2);
	// read TLM marker segment index
	i_TLM = *p_header_data++;
	// read and parse L parameter, which indicates number of bytes used to represent
	// remaining parameters
	L = *p_header_data++;
	// 0x70 ==  1110000
	if ((L & ~0x70) != 0) {
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
	uint32_t bytes_per_tile_part_length = L_LTP ? 4U : 2U;
	/*
	* 0 <= L_iT <= 2
	*
	* 0 => no tile part indices
	* 1 => 1 byte tile part indices
	* 2 => 2 byte tile part indices
	*/
	L_iT = ((L >> 4) & 0x3);
	uint32_t quotient = bytes_per_tile_part_length + L_iT;
	if (header_size % quotient != 0) {
		GRK_ERROR("Error reading TLM marker");
		return false;
	}
	// note: each tile can have max 255 tile parts, but
	// the whole image with multiple tiles can have more than
	// 255
	size_t num_tp = (uint8_t) (header_size / quotient);

	uint32_t Ttlm_i = 0, Ptlm_i = 0;
	for (size_t i = 0; i < num_tp; ++i) {
		// read (global) tile part index
		if (L_iT) {
			grk_read<uint32_t>(p_header_data, &Ttlm_i, L_iT);
			p_header_data += L_iT;
		}
		// read tile part length
		grk_read<uint32_t>(p_header_data, &Ptlm_i, bytes_per_tile_part_length);
		auto info =	L_iT ? TilePartLengthInfo((uint16_t) Ttlm_i, Ptlm_i) : TilePartLengthInfo(Ptlm_i);
		push(i_TLM, info);
		p_header_data += bytes_per_tile_part_length;
	}

	return true;
}
void TileLengthMarkers::push(uint8_t i_TLM, TilePartLengthInfo info) {
	auto pair = m_markers->find(i_TLM);

	if (pair != m_markers->end()) {
		pair->second->push_back(info);
	} else {
		auto vec = new TL_INFO_VEC();
		vec->push_back(info);
		m_markers->operator[](i_TLM) = vec;
	}
}
void TileLengthMarkers::getInit(void){
	m_markerIndex = 0;
	m_markerTilePartIndex = 0;
	m_curr_vec = nullptr;
	if (m_markers) {
		auto pair = m_markers->find(0);
		if (pair != m_markers->end())
			m_curr_vec = pair->second;
	}
}
TilePartLengthInfo TileLengthMarkers::getNext(void){
	if (!m_markers)
		return 0;
	if (m_curr_vec) {
		if (m_markerTilePartIndex == m_curr_vec->size()) {
			m_markerIndex++;
			if (m_markerIndex < m_markers->size()) {
				m_curr_vec = m_markers->operator[](m_markerIndex);
				m_markerTilePartIndex = 0;
			} else {
				m_curr_vec = nullptr;
			}
		}
		if (m_curr_vec)
			return m_curr_vec->operator[](m_markerTilePartIndex++);
	}
	return 0;
}
bool TileLengthMarkers::skipTo(uint16_t skipTileIndex, BufferedStream *stream,uint64_t firstSotPos){
	assert(stream);
	getInit();
	auto tl = getNext();
	uint16_t tileNumber = 0;
	uint64_t skip = 0;
	while (tileNumber != skipTileIndex){
		if (tl.length == 0){
			GRK_ERROR("corrupt TLM marker");
			return false;
		}
		skip += tl.length;
		tl = getNext();
		tileNumber = (uint16_t)(tl.has_tile_number ? tl.tileNumber : tileNumber+1U);
	}

	return stream->seek(firstSotPos + skip);
}
bool TileLengthMarkers::writeBegin(uint16_t totalTileParts) {
	uint32_t tlm_size = tlm_marker_start_bytes 	+ tlm_len_per_tile_part * totalTileParts;

	m_tlm_start_stream_position = m_stream->tell();

	/* TLM */
	if (!m_stream->write_short(J2K_MS_TLM))
		return false;

	/* Ltlm */
	if (!m_stream->write_short((uint16_t) (tlm_size - 2)))
		return false;

	/* Ztlm=0*/
	if (!m_stream->write_byte(0))
		return false;

	/* Stlm ST=1(8bits-255 tiles max),SP=1(Ptlm=32bits) */
	if (!m_stream->write_byte(0x50))
		return false;

	/* make room for tile part lengths */
	return m_stream->skip(tlm_len_per_tile_part	* totalTileParts);
}
void TileLengthMarkers::writeUpdate(uint16_t tileIndex, uint32_t tile_part_size) {
	assert(tileIndex <= 255);
    push(m_markerIndex,	TilePartLengthInfo((uint8_t)tileIndex,	tile_part_size));
}
bool TileLengthMarkers::writeEnd(void) {
    uint64_t tlm_position = m_tlm_start_stream_position+ tlm_marker_start_bytes;
	uint64_t current_position = m_stream->tell();

	if (!m_stream->seek(tlm_position))
		return false;

	for (auto it = m_markers->begin(); it != m_markers->end(); it++) {
		auto lengths = it->second;
		for (auto info = lengths->begin(); info != lengths->end(); ++info ){
			if (info->has_tile_number) {
				assert(info->tileNumber <= 255);
				m_stream->write_byte((uint8_t)info->tileNumber);
			}
			m_stream->write_int(info->length);
		}
	}

	return m_stream->seek(current_position);
}
bool TileLengthMarkers::addTileMarkerInfo(uint16_t tileno,
									CodeStreamInfo *codestreamInfo,
									uint16_t id,
									uint64_t pos,
									uint32_t len) {
	assert(codestreamInfo != nullptr);
	assert(codestreamInfo->hasTileInfo());
	auto currTileInfo = codestreamInfo->getTileInfo(tileno);
	if (id == J2K_MS_SOT) {
		uint8_t currTilePart = (uint8_t)currTileInfo->currentTilePart;
		auto tilePartInfo = currTileInfo->getTilePartInfo(currTilePart);
		if (tilePartInfo)
			tilePartInfo->startPosition =	pos;
	}

	codestreamInfo->pushMarker(id, pos, len);

	return true;
}

PacketLengthMarkers::PacketLengthMarkers() :
		m_markers(new PL_MAP()),
		m_markerIndex(0),
		m_curr_vec(nullptr),
		m_packetIndex(0),
		m_packet_len(0),
		m_marker_bytes_written(0),
		m_total_bytes_written(0),
		m_marker_len_cache(0),
		m_stream(nullptr)
{ }
PacketLengthMarkers::PacketLengthMarkers(BufferedStream *strm) : PacketLengthMarkers()
{
	m_stream = strm;
	writeInit();
}
PacketLengthMarkers::~PacketLengthMarkers() {
	if (m_markers) {
		for (auto it = m_markers->begin(); it != m_markers->end(); it++)
			delete it->second;
		delete m_markers;
	}
}
void PacketLengthMarkers::writeInit(void) {
	readInitIndex(0);
	m_total_bytes_written = 0;
	m_marker_bytes_written = 0;
	m_marker_len_cache = 0;
}
void PacketLengthMarkers::writeNext(uint32_t len) {
	assert(len);
	m_curr_vec->push_back(len);
}
void PacketLengthMarkers::writeIncrement(uint32_t bytes) {
	m_marker_bytes_written += bytes;
	m_total_bytes_written += bytes;
}
void PacketLengthMarkers::writeMarkerLength(){
	// write marker length
	if (m_marker_len_cache){
		uint64_t current_pos = m_stream->tell();
		m_stream->seek(m_marker_len_cache);
		//do not include 2 bytes for marker itself
		m_stream->write_short((uint16_t) (m_marker_bytes_written - 2));
		m_stream->seek(current_pos);
		m_marker_len_cache = 0;
		m_marker_bytes_written = 0;
	}
	assert(m_marker_bytes_written == 0);
}
// check if we need to start a new marker
void PacketLengthMarkers::writeMarkerHeader() {
	// 5 bytes worst-case to store a packet length
	if (m_total_bytes_written == 0
			|| (m_marker_bytes_written >= available_packet_len_bytes_per_plt - 5)) {

		// complete current marker
		writeMarkerLength();

		// begin new marker
		m_stream->write_short(J2K_MS_PLT);
		writeIncrement(2);

		// cache location of marker length and skip over
		m_marker_len_cache = m_stream->tell();
		m_stream->skip(2);
		writeIncrement(2);
	}
}
uint32_t PacketLengthMarkers::write() {
	writeMarkerHeader();
	for (auto map_iter = m_markers->begin(); map_iter != m_markers->end();
			++map_iter) {

		// write index
		m_stream->write_byte(map_iter->first);
		writeIncrement(1);

		// write marker lengths
		for (auto val_iter = map_iter->second->begin();
				val_iter != map_iter->second->end(); ++val_iter) {
			//check if we need to start a new PLT marker segment
			writeMarkerHeader();

			// write from MSB down to LSB
			uint32_t val = *val_iter;
			uint32_t numbits = floorlog2<uint32_t>(val) + 1;
			uint32_t numbytes = (numbits + 6) / 7;
			assert(numbytes <= 5);

			// write period
			uint8_t temp[5];
			int32_t counter = (int32_t)(numbytes - 1);
			temp[counter--] = (val & 0x7F);
			val = (uint32_t) (val >> 7);

			//write commas (backwards from LSB to MSB)
			while (val) {
				uint8_t b = (uint8_t) ((val & 0x7F) | 0x80);
				temp[counter--] = b;
				val = (uint32_t) (val >> 7);
			}
			assert(counter == -1);
			uint32_t written = (uint32_t)m_stream->write_bytes(temp, numbytes);
			assert(written == numbytes);
			(void)written;
			writeIncrement(numbytes);
		}
	}
	// write final marker length
	writeMarkerLength();
	return m_total_bytes_written;
}

bool PacketLengthMarkers::readPLM(uint8_t *p_header_data, uint16_t header_size){
	if (header_size < 1) {
		GRK_ERROR("PLM marker segment too short");
		return false;
	}
	// Zplm
	uint8_t Zplm = *p_header_data++;
	--header_size;
	readInitIndex(Zplm);
	while (header_size > 0) {
		// Nplm
		uint8_t Nplm = *p_header_data++;
		if (header_size < (1 + Nplm)) {
			GRK_ERROR("Malformed PLM marker segment");
			return false;
		}
		for (uint32_t i = 0; i < Nplm; ++i) {
			uint8_t tmp = *p_header_data;
			++p_header_data;
			readNext(tmp);
		}
		header_size = (uint16_t)(header_size - (1 + Nplm));
		if (m_packet_len != 0) {
			GRK_ERROR("Malformed PLM marker segment");
			return false;
		}
	}
	return true;
}
bool PacketLengthMarkers::readPLT(uint8_t *p_header_data, uint16_t header_size){
	if (header_size < 1) {
		GRK_ERROR("PLT marker segment too short");
		return false;
	}

	/* Zplt */
	uint8_t Zpl = *p_header_data++;
	--header_size;
	readInitIndex(Zpl);
	for (uint32_t i = 0; i < header_size; ++i) {
		/* Iplt_ij */
		uint8_t tmp = *p_header_data++;
		readNext(tmp);
	}
	if (m_packet_len != 0) {
		GRK_ERROR("Malformed PLT marker segment");
		return false;
	}
	return true;
}
void PacketLengthMarkers::readInitIndex(uint8_t index) {
	m_markerIndex = index;
	m_packet_len = 0;
	auto pair = m_markers->find(m_markerIndex);
	if (pair != m_markers->end()) {
		m_curr_vec = pair->second;
	} else {
		m_curr_vec = new PL_INFO_VEC();
		m_markers->operator[](m_markerIndex) = m_curr_vec;
	}
}
void PacketLengthMarkers::readNext(uint8_t Iplm) {
	/* take only the lower seven bits */
	m_packet_len |= (Iplm & 0x7f);
	if (Iplm & 0x80) {
		m_packet_len <<= 7;
	} else {
		assert(m_curr_vec);
		m_curr_vec->push_back(m_packet_len);
		//GRK_INFO("Packet length: (%u, %u)", Zpl, packet_len);
		m_packet_len = 0;
	}
}
void PacketLengthMarkers::getInit(void) {
	m_packetIndex = 0;
	m_markerIndex = 0;
	m_curr_vec = nullptr;
	if (m_markers) {
		auto pair = m_markers->find(0);
		if (pair != m_markers->end()) {
			m_curr_vec = pair->second;
		}
	}
}
// note: packet length must be at least 1, so 0 indicates
// no packet length available
uint32_t PacketLengthMarkers::getNext(void) {
	if (!m_markers)
		return 0;
	if (m_curr_vec) {
		if (m_packetIndex == m_curr_vec->size()) {
			m_markerIndex++;
			if (m_markerIndex < m_markers->size()) {
				m_curr_vec = m_markers->operator[](m_markerIndex);
				m_packetIndex = 0;
			} else {
				m_curr_vec = nullptr;
			}
		}
		if (m_curr_vec)
			return m_curr_vec->operator[](m_packetIndex++);
	}
	return 0;
}

PacketInfo::PacketInfo(void) : PacketInfo(0,0,0){
}
PacketInfo::PacketInfo(uint64_t off,
						uint32_t hdrlen,
						uint32_t datalen) : offset(off),
											headerLength(hdrlen),
											dataLength(datalen)
{}

PacketInfoCache::~PacketInfoCache(){
	for (auto &p : packetInfo)
		delete p;
}

}
