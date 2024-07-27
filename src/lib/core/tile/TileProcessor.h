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
 *
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#pragma once
#include "grk_includes.h"
#include <queue>
#include <mutex>

namespace grk
{
/*
 * Tile structure.
 *
 * Tile bounds are in canvas coordinates, and are equal to the
 * full, non-windowed, unreduced tile dimensions,
 * while the component dimensions are reduced
 * if there is a resolution reduction.
 *
 */
struct Tile : public grk_rect32
{
   Tile();
   explicit Tile(uint16_t numcomps);
   virtual ~Tile();
   uint16_t numcomps_;
   TileComponent* comps;
   double distortion;
   double layerDistoration[maxCompressLayersGRK];
};

struct PacketTracker
{
   PacketTracker();
   ~PacketTracker();
   void init(uint32_t numcomps, uint32_t numres, uint64_t numprec, uint32_t numlayers);
   void clear(void);
   void packet_encoded(uint32_t comps, uint32_t res, uint64_t prec, uint32_t layer);
   bool is_packet_encoded(uint32_t comps, uint32_t res, uint64_t prec, uint32_t layer);

 private:
   uint8_t* bits;

   uint32_t numcomps_;
   uint32_t numres_;
   uint64_t numprec_;
   uint32_t numlayers_;

   uint64_t get_buffer_len(uint32_t numcomps, uint32_t numres, uint64_t numprec,
						   uint32_t numlayers);
   uint64_t index(uint32_t comps, uint32_t res, uint64_t prec, uint32_t layer);
};

/**
 Tile processor for decompression and compression
 */

class mct;

struct TileProcessor
{
   explicit TileProcessor(uint16_t index, CodeStream* codeStream, BufferedStream* stream,
						  bool isCompressor);
   ~TileProcessor();
   bool init(void);
   bool createWindowBuffers(const GrkImage* outputImage);
   void deallocBuffers();
   bool preCompressTile(void);
   bool canWritePocMarker(void);
   bool writeTilePartT2(uint32_t* tileBytesWritten);
   bool doCompress(void);
   bool decompressT2T1(GrkImage* outputImage);
   bool ingestUncompressedData(uint8_t* p_src, uint64_t src_length);
   bool needsRateControl();
   void ingestImage();
   bool cacheTilePartPackets(CodeStreamDecompress* codeStream);
   void generateImage(GrkImage* src_image, Tile* src_tile);
   GrkImage* getImage(void);
   void release(GRK_TILE_CACHE_STRATEGY strategy);
   void setCorruptPacket(void);
   PacketTracker* getPacketTracker(void);
   grk_rect32 getUnreducedTileWindow(void);
   TileCodingParams* getTileCodingParams(void);
   uint8_t getMaxNumDecompressResolutions(void);
   BufferedStream* getStream(void);
   uint32_t getPreCalculatedTileLen(void);
   bool canPreCalculateTileLen(void);
   uint16_t getIndex(void) const;
   void incrementIndex(void);
   Tile* getTile(void);
   Scheduler* getScheduler(void);
   bool isCompressor(void);

   /** Compression Only
	*  true for first POC tile part, otherwise false*/
   bool first_poc_tile_part_;
   /** Compressing Only
	*  index of tile part being currently coding.
	*  tilePartCounter_ holds the total number of tile parts encoded thus far
	*  while the compressor is compressing the current tile part.*/
   uint8_t tilePartCounter_;
   /** Compression Only
	*  Current packet iterator number */
   uint32_t pino;
   GrkImage* headerImage;
   grk_plugin_tile* current_plugin_tile;
   CodingParams* cp_;
   PLCache packetLengthCache;
   uint64_t getTilePartDataLength(void);
   bool subtractMarkerSegmentLength(uint16_t markerLen);
   bool setTilePartDataLength(uint16_t tilePart, uint32_t tilePartLength,
							  bool lastTilePartInCodeStream);
   uint64_t getNumProcessedPackets(void);
   void incNumProcessedPackets(void);
   void incNumProcessedPackets(uint64_t numPackets);
   uint64_t getNumDecompressedPackets(void);
   void incNumDecompressedPackets(void);

 private:
   bool isWholeTileDecompress(uint16_t compno);
   bool needsMctDecompress(uint16_t compno);
   bool needsMctDecompress(void);
   bool mctDecompress(FlowComponent* flow);
   bool dcLevelShiftCompress();
   bool mct_encode();
   bool dwt_encode();
   void t1_encode();
   bool encodeT2(uint32_t* packet_bytes_written);
   bool rateAllocate(uint32_t* allPacketBytes, bool disableRateControl);
   bool layerNeedsRateControl(uint32_t layno);
   bool makeSingleLosslessLayer();
   void makeLayerFinal(uint32_t layno);
   bool pcrdBisectSimple(uint32_t* p_data_written, bool disableRateControl);
   void makeLayerSimple(uint32_t layno, double thresh, bool finalAttempt);

   Tile* tile;
   Scheduler* scheduler_;
   uint64_t numProcessedPackets;
   std::atomic<uint64_t> numDecompressedPackets;
   // Decompressing Only
   uint64_t tilePartDataLength;
   /** index of tile being currently compressed/decompressed */
   uint16_t tileIndex_;
   // Compressing only - track which packets have already been written
   // to the code stream
   PacketTracker packetTracker_;
   BufferedStream* stream_;
   bool corrupt_packet_;
   /** position of the tile part flag in progression order*/
   uint32_t newTilePartProgressionPosition;
   // coding/decoding parameters for this tile
   TileCodingParams* tcp_;
   bool truncated;
   GrkImage* image_;
   bool isCompressor_;
   grk_rect32 unreducedImageWindow;
   uint32_t preCalculatedTileLen;
   mct* mct_;
};

} // namespace grk
