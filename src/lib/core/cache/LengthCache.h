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

#pragma once
#include <vector>
#include <map>

namespace grk
{
struct MarkerInfo
{
   MarkerInfo(uint16_t _id, uint64_t _pos, uint32_t _len);
   MarkerInfo();
   void dump(FILE* outputFileStream);
   uint16_t id;
   /** position in code stream */
   uint64_t pos;
   /** length (marker id included) */
   uint32_t len;
};
struct TilePartInfo
{
   TilePartInfo(uint64_t start, uint64_t endHeader, uint64_t end);
   TilePartInfo(void);
   void dump(FILE* outputFileStream, uint8_t tilePart);
   /** start position of tile part*/
   uint64_t startPosition;
   /** end position of tile part header */
   uint64_t endHeaderPosition;
   /** end position of tile part */
   uint64_t endPosition;
};
struct TileInfo
{
   TileInfo(void);
   ~TileInfo(void);
   bool checkResize(void);
   bool hasTilePartInfo(void);
   bool update(uint16_t tileIndex, uint8_t currentTilePart, uint8_t numTileParts);
   TilePartInfo* getTilePartInfo(uint8_t tilePart);
   void dump(FILE* outputFileStream, uint16_t tileNum);
   uint16_t tileno;
   uint8_t numTileParts;
   uint8_t allocatedTileParts;
   uint8_t currentTilePart;

 private:
   // TilePartInfo array
   TilePartInfo* tilePartInfo;

   // MarkerInfo array
   MarkerInfo* markerInfo;
   uint32_t numMarkers;
   uint32_t allocatedMarkers;
};
struct CodeStreamInfo
{
   CodeStreamInfo(BufferedStream* str);
   virtual ~CodeStreamInfo();
   bool allocTileInfo(uint16_t numTiles);
   bool updateTileInfo(uint16_t tileIndex, uint8_t currentTilePart, uint8_t numTileParts);
   TileInfo* getTileInfo(uint16_t tileIndex);
   void dump(FILE* outputFileStream);
   void pushMarker(uint16_t id, uint64_t pos, uint32_t len);
   uint64_t getMainHeaderStart(void);
   void setMainHeaderStart(uint64_t start);
   uint64_t getMainHeaderEnd(void);
   void setMainHeaderEnd(uint64_t end);
   bool seekFirstTilePart(uint16_t tileIndex);

 private:
   /** main header start position (SOC position) */
   uint64_t mainHeaderStart;
   /** main header end position (first SOT position) */
   uint64_t mainHeaderEnd;
   std::vector<MarkerInfo*> marker;
   // TileInfo array
   TileInfo* tileInfo;
   uint16_t numTiles;
   BufferedStream* stream;
};
struct TilePartLengthInfo
{
   explicit TilePartLengthInfo();
   TilePartLengthInfo(uint16_t tileno, uint32_t len);
   uint16_t tileIndex_;
   uint32_t length_;
};

typedef std::vector<TilePartLengthInfo> TL_INFO_VEC;
typedef std::map<uint16_t, TL_INFO_VEC*> TL_MAP;

struct TileLengthMarkers
{
   explicit TileLengthMarkers(uint16_t numSignalledTiles);
   explicit TileLengthMarkers(BufferedStream* stream);
   ~TileLengthMarkers();

   bool read(uint8_t* headerData, uint16_t header_size);
   void rewind(void);
   TilePartLengthInfo* next(void);
   TilePartLengthInfo* next(bool peek);
   void invalidate(void);
   bool valid(void);
   void seek(TileSet* tilesToDecompress, CodingParams* cp, BufferedStream* stream);
   bool writeBegin(uint16_t numTilePartsTotal);
   void push(uint16_t tileIndex, uint32_t tile_part_size);
   bool writeEnd(void);
   /**
	Add tile header marker information
	@param tileno       tile index number
	@param codeStreamInfo   Codestream information structure
	@param type         marker type
	@param pos          byte offset of marker segment
	@param len          length of marker segment
	*/
   static bool addTileMarkerInfo(uint16_t tileno, CodeStreamInfo* codeStreamInfo, uint16_t type,
								 uint64_t pos, uint32_t len);

 private:
   void push(uint8_t i_TLM, TilePartLengthInfo curr_vec);
   TL_MAP* markers_;
   TL_MAP::iterator markerIt_;
   uint16_t markerTilePartIndex_;
   TL_INFO_VEC* curr_vec_;
   BufferedStream* stream_;
   uint64_t streamStart;
   bool valid_;
   bool hasTileIndices_;
   // used to track tile index when there are no tile indices
   // stored in markers
   uint16_t tileCount_;
   uint16_t numSignalledTiles_;
};

struct PacketInfo
{
   PacketInfo(void);
   uint32_t getPacketDataLength(void);
   uint32_t packetLength;
};

struct PacketInfoCache
{
   PacketInfoCache();
   ~PacketInfoCache();

   std::vector<PacketInfo*> packetInfo;
};

} // namespace grk
