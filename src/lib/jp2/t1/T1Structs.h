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
 */

#pragma once

#include "grk_includes.h"

namespace grk {

// code segment (code block can be encoded into multiple segments)
struct Segment {
	Segment();

	void clear();
	uint32_t dataindex;		      // segment data offset in contiguous memory block
	uint32_t numpasses;		    	// number of passes in segment
	uint32_t len;               // total length of segment
	uint32_t maxpasses;			  	// maximum number of passes in segment
	uint32_t numPassesInPacket;	  // number of passes contributed by current packet
	uint32_t numBytesInPacket;      // number of bytes contributed by current packet
};

struct PacketLengthInfo {
	PacketLengthInfo(uint32_t mylength, uint32_t bits);
	PacketLengthInfo();
	bool operator==(const PacketLengthInfo &rhs) const;
	uint32_t len;
	uint32_t len_bits;
};

// encoding/decoding pass
struct CodePass {
	CodePass();
	uint32_t rate;
	double distortiondec;
	uint32_t len;
	uint8_t term;
	uint16_t slope;  //ln(slope) in 8.8 fixed point
};

//quality layer
struct Layer {
	Layer();
	uint32_t numpasses; /* Number of passes in the layer */
	uint32_t len; /* len of information */
	double disto; /* add for index (Cfr. Marcela) */
	uint8_t *data; /* data buffer (points to code block data) */
};

struct Codeblock : public grk_rect_u32 {
    Codeblock(const Codeblock &rhs);
    Codeblock();
    Codeblock& operator=(const Codeblock& other);
    virtual ~Codeblock(){}
    virtual void clear();
	uint8_t *compressedData; /* data buffer*/
	uint32_t compressedDataSize; /* size of allocated data buffer */
	bool owns_data;	// true if code block manages data buffer, otherwise false
	uint32_t numbps;
	uint32_t numlenbits;
	uint32_t numPassesInPacket; /* number of passes encoded in current packet */
#ifdef DEBUG_LOSSLESS_T2
	uint32_t included;
	std::vector<PacketLengthInfo> packet_length_info;
#endif
};

// compressor code block
struct CompressCodeblock : public Codeblock {
	CompressCodeblock();
	~CompressCodeblock();
	CompressCodeblock(const CompressCodeblock &rhs);
	CompressCodeblock& operator=(const CompressCodeblock& other);
	void clear() override;
	bool alloc();
	bool alloc_data(size_t nominalBlockSize);
	void cleanup();
	uint8_t *paddedCompressedData; /* data buffer*/
	Layer *layers;
	CodePass *passes;
	uint32_t numPassesInPreviousPackets; /* number of passes in previous packetss */
	uint32_t numPassesTotal; /* total number of passes in all layers */
	uint32_t *contextStream;
};

//decoder code block
struct DecompressCodeblock: public Codeblock {
	DecompressCodeblock();
	~DecompressCodeblock();
	DecompressCodeblock(const DecompressCodeblock &rhs);
	DecompressCodeblock& operator=(const DecompressCodeblock& other);
	void clear() override;
	void init();
	bool alloc();
	void cleanup();
	void cleanup_seg_buffers();
	size_t getSegBuffersLen();
	bool copy_to_contiguous_buffer(uint8_t *buffer);
	std::vector<grk_buf*> seg_buffers;
	Segment *segs; /* information on segments */
	uint32_t numSegments; /* number of segment in block*/
	uint32_t numSegmentsAllocated; // number of segments allocated for segs array

};

// precinct
struct Precinct : public grk_rect_u32 {
	Precinct();
	void initTagTrees();
	void deleteTagTrees();

	uint32_t cw, ch; /* number of precinct in width and height */
	CompressCodeblock *enc;
	DecompressCodeblock *dec;
	uint64_t num_code_blocks;
	TagTree *incltree; /* inclusion tree */
	TagTree *imsbtree; /* IMSB tree */
};

// band
struct Subband : public grk_rect_u32 {
	Subband();
	Subband(const Subband &rhs);

	Subband& operator= (const Subband &rhs);


	bool isEmpty() ;
	// 0 for first band of lowest resolution, otherwise equal to 1,2 or 3
	uint8_t bandno;
	Precinct *precincts; /* precinct information */
	uint64_t numPrecincts;
	size_t numAllocatedPrecincts;
	uint32_t numbps;
	float stepsize;
	// inverse step size in 13 bit fixed point
	uint32_t inv_step;

};

// resolution
struct Resolution : public grk_rect_u32 {
	Resolution();

	/* precinct dimensions */
	uint32_t pw, ph;
	uint32_t numbands; /* number sub-band for the resolution level */
	Subband bands[3]; /* subband information */

    /* dimension of the resolution limited to window of interest.
     *  Only valid if tcd->whole_tile_decoding is set */
	grk_rect_u32 win_bounds;
};

}
