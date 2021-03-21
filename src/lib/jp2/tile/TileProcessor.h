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
 *
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#pragma once
#include "grk_includes.h"

namespace grk {

struct TileComponent;

/*
 * Tile structure.
 *
 * Tile bounds are relative to origin, and are equal to the unreduced
 * tile dimensions, while the component dimensions are reduced
 * if there is a resolution reduction.
 *
 */
struct Tile : public grkRectU32 {
	Tile();
	~Tile();

	uint32_t 		numcomps;
	TileComponent 	*comps;
	double distotile;
	double distolayer[100];
	uint64_t numIteratedPackets;
	uint64_t numDecompressedPackets;
};

struct PacketTracker{
	PacketTracker();
	~PacketTracker();
	void init(uint32_t numcomps, uint32_t numres, uint64_t numprec, uint32_t numlayers);
	void clear(void);
	void packet_encoded(uint32_t comps, uint32_t res, uint64_t prec, uint32_t layer);
	bool is_packet_encoded(uint32_t comps, uint32_t res, uint64_t prec, uint32_t layer);
private:
	uint8_t *bits;

	uint32_t m_numcomps;
	uint32_t m_numres;
	uint64_t m_numprec;
	uint32_t m_numlayers;

	uint64_t get_buffer_len(uint32_t numcomps, uint32_t numres, uint64_t numprec, uint32_t numlayers);
	uint64_t index(uint32_t comps, uint32_t res, uint64_t prec, uint32_t layer);
};

/**
 Tile processor for decompression and compression
 */

struct TileProcessor {
	explicit TileProcessor(CodeStream *codeStream,
							BufferedStream *stream,
							bool isCompressor,
							bool isWholeTileDecompress) ;
	~TileProcessor();
	 bool init(void);
	 bool allocWindowBuffers(const GrkImage *output_image);
	 void deallocBuffers();
	 bool pre_write_tile(void);
	bool compressTilePart(uint32_t *tile_bytes_written);
	bool preCompressFirstTilePart(void);
	bool do_compress(void);
	bool decompressT1(void);
	bool decompressT2(SparseBuffer *src_buf);
	bool decompressT2T1(TileCodingParams *tcp,
							GrkImage *outputImage,
							bool multiTile,
							bool doPost);
	bool ingestUncompressedData(uint8_t *p_src, uint64_t src_length);
	bool needsRateControl();
	void ingestImage();
	bool prepareSodDecompress(CodeStreamDecompress *codeStream);
	/** index of tile being currently compressed/decompressed */
	uint16_t m_tile_index;
	/** Compressing Only
	 *  true for first POC tile part, otherwise false*/
	bool m_first_poc_tile_part;
	/** Compressing Only
	 *  index of tile part being currently coding.
	 *  m_tile_part_index holds the total number of tile parts encoded thus far
	 *  while the compressor is compressing the current tile part.*/
	uint8_t m_tile_part_index;
	// Decompressing Only
	uint32_t tile_part_data_length;
	/** Compressing Only
	 * Total number of tile parts of the tile*/
	uint8_t totnum_tp;
	/** Compressing Only
	 *  Current packet iterator number */
	uint32_t pino;
	/** info on image tile */
	Tile *tile;
	/** image header */
	GrkImage *headerImage;
	grk_plugin_tile *current_plugin_tile;
    // true if whole tile will be decoded; false if tile window will be decoded
    bool   wholeTileDecompress;
	PacketLengthMarkers *plt_markers;
	/** Coding parameters */
	CodingParams *m_cp;
	// Compressing only - track which packets have been already written
	// to the code stream
	PacketTracker m_packetTracker;
	BufferedStream *m_stream;
	bool m_corrupt_packet;
	void generateImage(GrkImage* src_image, Tile *src_tile);
	GrkImage* getImage(void);
private:
	/** position of the tile part flag in progression order*/
	uint32_t tp_pos;
	// coding/decoding parameters for this tile
	TileCodingParams *m_tcp;
	 bool isWholeTileDecompress( uint32_t compno);
	 bool needsMctDecompress(uint32_t compno);
	 bool mctDecompress();
	 bool dcLevelShiftDecompress();
	 bool dcLevelShiftCompress();
	 bool mct_encode();
	 bool dwt_encode();
	 void t1_encode();
	 bool t2_encode(uint32_t *packet_bytes_written);
	 bool rateAllocate(void);
	 bool layerNeedsRateControl(uint32_t layno);
	 bool makeSingleLosslessLayer();
	 void makeLayerFinal(uint32_t layno);
	 bool pcrdBisectSimple(uint32_t *p_data_written);
	 void makeLayerSimple(uint32_t layno, double thresh,bool final);
	 bool pcrdBisectFeasible(uint32_t *p_data_written);
	 void makeLayerFeasible(uint32_t layno, uint16_t thresh,bool final);
	 bool truncated;
	 GrkImage *m_image;
	 bool m_isCompressor;
};

}
