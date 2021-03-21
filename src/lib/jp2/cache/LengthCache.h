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

#pragma once
#include <vector>
#include <map>

namespace grk {

struct MarkerInfo {
	MarkerInfo(uint16_t _id,uint64_t _pos, uint32_t _len);
	MarkerInfo();
	void dump(FILE *out_stream);
	/** marker id */
	uint16_t id;
	/** position in code stream */
	uint64_t pos;
	/** length (marker id included) */
	uint32_t len;
};
struct TilePartInfo {
	TilePartInfo(uint64_t start, uint64_t endHeader, uint64_t end);
	TilePartInfo(void);
	void dump(FILE *out_stream, uint8_t tilePart);
	/** start position of tile part*/
	uint64_t startPosition;
	/** end position of tile part header */
	uint64_t endHeaderPosition;
	/** end position of tile part */
	uint64_t endPosition;
};
struct TileInfo {
	TileInfo(void);
	~TileInfo(void);
	bool checkResize(void);
	bool hasTilePartInfo(void);
	bool update(uint16_t tileIndex,uint8_t currentTilePart, uint8_t numTileParts);
	TilePartInfo* getTilePartInfo(uint8_t tilePart);
	void dump(FILE *out_stream, uint16_t tileNum);
	/** tile index */
	uint16_t tileno;
	/** number of tile parts */
	uint8_t numTileParts;
	/** current nb of tile part (allocated)*/
	uint8_t allocatedTileParts;
	/** current tile-part index */
	uint8_t currentTilePart;
private:
	/** tile part index */
	TilePartInfo *tilePartInfo;
	/** array of markers */
	MarkerInfo *markerInfo;
	/** number of markers */
	uint32_t numMarkers;
	/** actual size of markers array */
	uint32_t allocatedMarkers;
};
struct CodeStreamInfo {
	CodeStreamInfo();
	virtual ~CodeStreamInfo();
	bool allocTileInfo(uint16_t numTiles);
	bool updateTileInfo(uint16_t tileIndex, uint8_t currentTilePart, uint8_t numTileParts);
	TileInfo* getTileInfo(uint16_t tileIndex);
	bool hasTileInfo(void);
	void dump(FILE *out_stream);
	void pushMarker(uint16_t id,uint64_t pos,uint32_t len);
	uint64_t getMainHeaderStart(void);
	void setMainHeaderStart(uint64_t start);
	uint64_t getMainHeaderEnd(void);
	void setMainHeaderEnd(uint64_t end);
private:
	/** main header start position (SOC position) */
	uint64_t mainHeaderStart;
	/** main header end position (first SOT position) */
	uint64_t mainHeaderEnd;
	/** list of markers */
	std::vector<MarkerInfo*> marker;
	uint16_t numTiles;
	TileInfo *tileInfo;
};
struct TilePartLengthInfo {
	TilePartLengthInfo() :	hasTileIndex(false),
			tileIndex(0),
			length(0) {
	}
	TilePartLengthInfo(uint32_t len) :	hasTileIndex(false),
								tileIndex(0),
								length(len) {
	}
	TilePartLengthInfo(uint16_t tileno, uint32_t len) : hasTileIndex(true),
												tileIndex(tileno),
												length(len) {
	}
	bool hasTileIndex;
	uint16_t tileIndex;
	uint32_t length;
};
typedef std::vector<TilePartLengthInfo> TL_INFO_VEC;
// map of (TLM marker id) => (tile part length vector)
typedef std::map<uint8_t, TL_INFO_VEC*> TL_MAP;

struct TileLengthMarkers {
	TileLengthMarkers();
	TileLengthMarkers(BufferedStream *stream);
	~TileLengthMarkers();

	bool read(uint8_t *p_header_data, uint16_t header_size);
	void getInit(void);
	TilePartLengthInfo getNext(void);
	bool skipTo(uint16_t skipTileIndex, BufferedStream *stream, uint64_t firstSotPos);

	bool writeBegin(uint16_t numTilePartsTotal);
	void writeUpdate(uint16_t tileIndex, uint32_t tile_part_size);
	bool writeEnd(void);
	/**
	 Add tile header marker information
	 @param tileno       tile index number
	 @param codeStreamInfo   Codestream information structure
	 @param type         marker type
	 @param pos          byte offset of marker segment
	 @param len          length of marker segment
	 */
	static bool addTileMarkerInfo(uint16_t tileno,
							CodeStreamInfo *codeStreamInfo,
							uint16_t type,
							uint64_t pos,
							uint32_t len);
private:
	void push(uint8_t i_TLM, TilePartLengthInfo curr_vec);
	TL_MAP *m_markers;
	uint8_t m_markerIndex;
	uint8_t m_markerTilePartIndex;
	TL_INFO_VEC *m_curr_vec;
	BufferedStream *m_stream;
	uint64_t m_tlm_start_stream_position;
};

typedef std::vector<uint32_t> PL_INFO_VEC;

// map of (PLT/PLM marker id) => (packet length vector)
typedef std::map<uint8_t, PL_INFO_VEC*> PL_MAP;

struct PacketLengthMarkers {
	PacketLengthMarkers(void);
	PacketLengthMarkers(BufferedStream *strm);
	~PacketLengthMarkers(void);

	// decompressor  packet lengths
	bool readPLT(uint8_t *p_header_data, uint16_t header_size);
	bool readPLM(uint8_t *p_header_data, uint16_t header_size);
	void getInit(void);
	uint32_t getNext(void);

	// compressor packet lengths
	void writeInit(void);
	void writeNext(uint32_t len);
	uint32_t write();
private:
	void readInitIndex(uint8_t index);
	void readNext(uint8_t Iplm);
	void writeMarkerHeader(void);
	void writeMarkerLength();
	void writeIncrement(uint32_t bytes);

	PL_MAP *m_markers;
	uint8_t m_markerIndex;
	PL_INFO_VEC *m_curr_vec;
	size_t m_packetIndex;
	uint32_t m_packet_len;
	uint32_t m_marker_bytes_written;
	uint32_t m_total_bytes_written;
	uint64_t m_marker_len_cache;
	BufferedStream *m_stream;
};

struct PacketInfo{
	PacketInfo(void);
	PacketInfo(uint64_t off, uint32_t hdrlen, uint32_t datalen);
private:
	uint64_t offset;
	uint32_t headerLength;
	uint32_t dataLength;
};

struct PacketInfoCache {
	PacketInfoCache();
	~PacketInfoCache();

	std::vector<PacketInfo*> packetInfo;
};

}
