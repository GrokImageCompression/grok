/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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
#include "testing.h"
#include <vector>

namespace grk {

// code segment (code block can be encoded into multiple segments)
struct grk_seg {
	grk_seg();

	void clear();
	uint32_t dataindex;		      // segment data offset in contiguous memory block
	uint32_t numpasses;		    	// number of passes in segment
	uint32_t len;               // total length of segment
	uint32_t maxpasses;			  	// maximum number of passes in segment
	uint32_t numPassesInPacket;	  // number of passes contributed by current packet
	uint32_t numBytesInPacket;      // number of bytes contributed by current packet
};

struct grk_packet_length_info {
	grk_packet_length_info(uint32_t mylength, uint32_t bits);
	grk_packet_length_info();
	bool operator==(const grk_packet_length_info &rhs) const;
	uint32_t len;
	uint32_t len_bits;
};

// encoding/decoding pass
struct grk_pass {
	grk_pass();
	uint32_t rate;
	double distortiondec;
	uint32_t len;
	uint8_t term;
	uint16_t slope;  //ln(slope) in 8.8 fixed point
};

//quality layer
struct grk_layer {
	grk_layer();
	uint32_t numpasses; /* Number of passes in the layer */
	uint32_t len; /* len of information */
	double disto; /* add for index (Cfr. Marcela) */
	uint8_t *data; /* data buffer (points to code block data) */
};

struct grk_cblk {
    grk_cblk(const grk_cblk &rhs);
    grk_cblk();
    grk_cblk& operator=(const grk_cblk& other);
    virtual ~grk_cblk(){}
    virtual void clear();
    uint32_t width();
    uint32_t height();
    uint32_t area();
	uint32_t x0, y0, x1, y1; /* dimension of the code block : left upper corner (x0, y0) right low corner (x1,y1) */
	uint8_t *compressedData; /* data buffer*/
	uint32_t compressedDataSize; /* size of allocated data buffer */
	bool owns_data;	// true if code block manages data buffer, otherwise false
	uint32_t numbps;
	uint32_t numlenbits;
	uint32_t numPassesInPacket; /* number of passes encoded in current packet */
#ifdef DEBUG_LOSSLESS_T2
	uint32_t included;
	std::vector<grk_packet_length_info> packet_length_info;
#endif
};

// encoder code block
struct grk_cblk_enc : public grk_cblk {
	grk_cblk_enc();
	~grk_cblk_enc();
	grk_cblk_enc(const grk_cblk_enc &rhs);
	grk_cblk_enc& operator=(const grk_cblk_enc& other);
	void clear() override;
	bool alloc();
	bool alloc_data(size_t nominalBlockSize);
	void cleanup();
	uint8_t *paddedCompressedData; /* data buffer*/
	grk_layer *layers;
	grk_pass *passes;
	uint32_t numPassesInPreviousPackets; /* number of passes in previous packetss */
	uint32_t numPassesTotal; /* total number of passes in all layers */
	uint32_t *contextStream;
};

//decoder code block
struct grk_cblk_dec: public grk_cblk {
	grk_cblk_dec();
	~grk_cblk_dec();
	grk_cblk_dec(const grk_cblk_dec &rhs);
	grk_cblk_dec& operator=(const grk_cblk_dec& other);
	void clear() override;
	void init();
	bool alloc();
	void cleanup();
	void cleanup_seg_buffers();
	size_t getSegBuffersLen();
	bool copy_to_contiguous_buffer(uint8_t *buffer);
	std::vector<grk_buf*> seg_buffers;
	grk_seg *segs; /* information on segments */
	uint32_t numSegments; /* number of segment in block*/
	uint32_t numSegmentsAllocated; // number of segments allocated for segs array

};

// precinct
struct grk_precinct : public grk_rect_u32 {
	grk_precinct();
	void initTagTrees();
	void deleteTagTrees();

	uint32_t cw, ch; /* number of precinct in width and height */
	grk_cblk_enc *enc;
	grk_cblk_dec *dec;
	uint64_t num_code_blocks;
	TagTree *incltree; /* inclusion tree */
	TagTree *imsbtree; /* IMSB tree */
};

// band
struct grk_band : public grk_rect_u32 {
	grk_band();
	grk_band(const grk_band &rhs);

	grk_band& operator= (const grk_band &rhs);


	bool isEmpty() ;
	// 0 for first band of lowest resolution, otherwise equal to 1,2 or 3
	uint8_t bandno;
	grk_precinct *precincts; /* precinct information */
	uint64_t numPrecincts;
	size_t numAllocatedPrecincts;
	uint32_t numbps;
	float stepsize;
	// inverse step size in 13 bit fixed point
	uint32_t inv_step;

};

// resolution
struct grk_resolution : public grk_rect_u32 {
	grk_resolution();

	/* dimension of the resolution level in tile coordinates */
	uint32_t pw, ph;
	uint32_t numbands; /* number sub-band for the resolution level */
	grk_band bands[3]; /* subband information */

    /* dimension of the resolution limited to window of interest.
     *  Only valid if tcd->whole_tile_decoding is set */
	grk_rect_u32 win_bounds;
};

struct TileComponent;

// tile
struct grk_tile : public grk_rect_u32 {
	uint32_t numcomps; /* number of components in tile */
	TileComponent *comps; /* Components information */
	uint64_t numpix;
	double distotile;
	double distolayer[100];
	uint64_t packno; /* packet number */
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
 Tile coder/decoder
 */
struct TileProcessor {
	explicit TileProcessor(CodeStream *codeStream, BufferedStream *stream) ;
	~TileProcessor();

	/**
	 * Allocates memory for decoding a specific tile.
	 *
	 * @param	output_image 	output image - stores the decompress region of interest
	 * @param	isEncoder	 	true if tile will be encoded, otherwise false
	 *
	 * @return	true if the remaining data is sufficient.
	 */
	 bool init_tile(grk_image *output_image, bool isEncoder);

	 bool pre_write_tile(void);

	/**
	 * Compress a tile from a raw image into stream.
	 * @param	tile_bytes_written	number of bytes written to stream
	 * @return  true if the coding is successful.
	 */
	bool compress_tile_part(uint32_t *tile_bytes_written);

	bool pre_compress_first_tile_part(void);

	/**
	 * Compress a tile from a raw image into stream.
	 * @return  true if the coding is successful.
	 */
	bool do_encode(void);


	/**
	 T1 Decode a tile from a buffer
	 @return true if successful
	 */
	bool decompress_tile_t1(void);

	/**
	 T2 Decode a tile from a buffer
	 @param src_buf Source buffer
	 @return true if successful
	 */
	bool decompress_tile_t2(ChunkBuffer *src_buf);

	/**
	 * Copies tile data from the given memory block onto the system.
	 */
	bool copy_uncompressed_data_to_tile(uint8_t *p_src, uint64_t src_length);

	bool needs_rate_control();

	bool copy_decompressed_tile_to_output_image(grk_image *p_output_image);

	void copy_image_to_tile();

	bool prepare_sod_decoding(CodeStream *codeStream);

	/** index of tile being currently coded/decoded */
	uint16_t m_tile_index;

	/** Encoding Only
	 *  tile part index, regardless of poc.
	 *  for each new poc, tp is reset to 0*/
	uint8_t m_poc_tile_part_index;

	/** Encoding Only
	 *  index of tile part being currently coding, taking into account POC.
	 *  m_tile_part_index holds the total number of tile parts encoded thus far
	 *  while the compressor is encoding the current tile part.*/
	uint8_t m_tile_part_index;

	// Decoding Only
	uint32_t tile_part_data_length;

	/** Encoding Only
	 * Total number of tile parts of the tile*/
	uint8_t totnum_tp;

	/** Encoding Only
	 *  Current packet iterator number */
	uint32_t pino;

	/** info on image tile */
	grk_tile *tile;
	/** image header */
	grk_image *image;
	grk_plugin_tile *current_plugin_tile;

    /** Only valid for decoding. Whether the whole tile is decoded, or just the region in win_x0/win_y0/win_x1/win_y1 */
    bool   whole_tile_decoding;

	PacketLengthMarkers *plt_markers;

	/** Coding parameters */
	CodingParams *m_cp;

	// Encoding only - track which packets have been arleady written
	// to the code stream
	PacketTracker m_packetTracker;

	uint32_t* m_resno_decoded_per_component;
	BufferedStream *m_stream;
private:

	/** position of the tile part flag in progression order*/
	uint32_t tp_pos;

	/** coding/decoding parameters common to all tiles */
	TileCodingParams *m_tcp;

	 bool t2_decode(ChunkBuffer *src_buf,	uint64_t *p_data_read);

	 bool is_whole_tilecomp_decoding( uint32_t compno);

	 bool need_mct_decode(uint32_t compno);

	 bool mct_decode();

	 bool dc_level_shift_decode();

	 bool dc_level_shift_encode();

	 bool mct_encode();

	 bool dwt_encode();

	 void t1_encode();

	 bool t2_encode(uint32_t *packet_bytes_written);

	 bool rate_allocate(void);

	 bool layer_needs_rate_control(uint32_t layno);

	 bool make_single_lossless_layer();

	 void makelayer_final(uint32_t layno);

	 bool pcrd_bisect_simple(uint32_t *p_data_written);

	 void make_layer_simple(uint32_t layno, double thresh,
			bool final);

	 bool pcrd_bisect_feasible(uint32_t *p_data_written);

	 void makelayer_feasible(uint32_t layno, uint16_t thresh,
			bool final);
public:
	 bool m_corrupt_packet;

};

}
