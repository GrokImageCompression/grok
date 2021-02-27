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
 */

#pragma once

#include "grk_includes.h"
#include <map>

namespace grk {

class ISparseBuffer;
struct grk_tile;
struct TileComponent;

enum eSplitOrientation{
	SPLIT_L,
	SPLIT_H,
	SPLIT_NUM_ORIENTATIONS
};


enum eBandOrientation{
	BAND_ORIENT_LL,
	BAND_ORIENT_HL,
	BAND_ORIENT_LH,
	BAND_ORIENT_HH,
	BAND_NUM_ORIENTATIONS
};


//////////////////////////////////////////////
// Band Indices

// LL band index when resolution == 0
const uint32_t BAND_RES_ZERO_INDEX_LL = 0;

// band indices when resolution > 0
enum eBandIndex{
	BAND_INDEX_HL,
	BAND_INDEX_LH,
	BAND_INDEX_HH,
	BAND_NUM_INDICES
};
/////////////////////////////////////////////


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

// compressing/decoding pass
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
	grk_buf compressedStream;
	uint32_t numbps;
	uint32_t numlenbits;
	uint32_t numPassesInPacket; 	/* number of passes encoded in current packet */
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
	uint8_t *paddedCompressedStream;
	Layer *layers;
	CodePass *passes;
	uint32_t numPassesInPreviousPackets; /* number of passes in previous packets */
	uint32_t numPassesTotal; 			/* total number of passes in all layers */
	uint32_t *contextStream;
};

//decompressor code block
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
	Segment *segs; 					/* information on segments */
	uint32_t numSegments; 			/* number of segment in block*/
	uint32_t numSegmentsAllocated; 	// number of segments allocated for segs array

};

const size_t kChunkSize = 1024;
template <typename T, typename P> class ChunkedArray{
public:
	ChunkedArray(P *blockInitializer, uint64_t maxChunkSize) : m_blockInitializer(blockInitializer),
																m_chunkSize(std::min<uint64_t>(maxChunkSize, kChunkSize)),
																m_currChunk(nullptr),
																m_currChunkIndex(0)
	{
	}
	~ChunkedArray(void){
		for (auto &ch : chunks){
			for (size_t i = 0; i < m_chunkSize; ++i)
				delete ch.second[i];
			delete[] ch.second;
		}
	}
	T* get(uint64_t index){
		uint64_t chunkIndex = index / m_chunkSize;
		uint64_t itemIndex =  index % m_chunkSize;

		if (m_currChunk == nullptr || chunkIndex != m_currChunkIndex){
			m_currChunkIndex = chunkIndex;
			auto iter = chunks.find(chunkIndex);
			if (iter != chunks.end()){
				m_currChunk =  iter->second;
			} else {
				m_currChunk = new T*[m_chunkSize];
				memset(m_currChunk, 0, m_chunkSize * sizeof(T*));
				chunks[chunkIndex] = m_currChunk;
			}
		}

		auto item = m_currChunk[itemIndex];
		if (!item){
			item = new T();
			m_blockInitializer->initBlock(item, index);
			m_currChunk[itemIndex] = item;
		}
		return item;
	}
private:
	std::map<uint64_t, T**> chunks;
	P *m_blockInitializer;
	uint64_t m_chunkSize;
	T** m_currChunk;
	uint64_t m_currChunkIndex;
};


struct PrecinctImpl {
	PrecinctImpl(bool isCompressor, grk_rect_u32 *bounds,grk_pt cblkExpn);
	~PrecinctImpl();
	void initTagTrees();
	void deleteTagTrees();
	bool initBlocks(grk_rect_u32 *bounds);
	template<typename T> bool initBlock(T* block, uint64_t cblkno);
	ChunkedArray<CompressCodeblock, PrecinctImpl> *enc;
	ChunkedArray<DecompressCodeblock, PrecinctImpl> *dec;
	TagTree *incltree; /* inclusion tree */
	TagTree *imsbtree; /* IMSB tree */
	grk_rect_u32 m_cblk_grid;
	grk_rect_u32 m_bounds;
	grk_pt m_cblk_expn;
	bool m_isCompressor;
};


// precinct
struct Precinct : public grk_rect_u32 {
	Precinct(const grk_rect_u32 &bounds, bool isCompressor, grk_pt cblkExpn);
	~Precinct(void);
	void initTagTrees(void);
	void deleteTagTrees(void);
	uint32_t getCblkGridwidth(void);
	uint32_t getCblkGridHeight(void);
	uint64_t getNumCblks(void);
	CompressCodeblock* getCompressedBlockPtr(uint64_t cblkno);
	DecompressCodeblock* getDecompressedBlockPtr(uint64_t cblkno);
	TagTree* getInclTree(void);
	TagTree* getImsbTree(void);
	uint32_t getNominalBlockSize(void);
	grk_pt getCblkExpn(void);
	grk_rect_u32 getCblkGrid(void);

	uint64_t precinctIndex;
private:
	PrecinctImpl *impl;
	grk_pt m_cblk_expn;
	PrecinctImpl* getImpl(void);
};



// band
struct Subband : public grk_rect_u32 {
	Subband();
	Subband(const Subband &rhs);
	Subband& operator= (const Subband &rhs);
	bool isEmpty() ;
	void print();
	Precinct* getPrecinct(uint64_t precinctIndex);
	Precinct* createPrecinct(bool isCompressor,
						uint64_t precinctIndex,
						grk_pt precinctStart,
						grk_pt precinctExpn,
						uint32_t pw,
						grk_pt cblkExpn);

	eBandOrientation orientation;
	std::vector<Precinct*> precincts;
	// maps global precinct index to vector index
	std::map<uint64_t, uint64_t> precinctMap;
	uint64_t numPrecincts;
	uint32_t numbps;
	float stepsize;
};

// resolution
struct Resolution : public grk_rect_u32 {
	Resolution();
	void print();
	bool init(bool isCompressor,
				TileComponentCodingParams *tccp,
				uint8_t resno,
				grk_plugin_tile *current_plugin_tile);

	bool initialized;
	Subband band[BAND_NUM_INDICES]; // unreduced tile component bands
									// (in canvas coords, but shifted to tile origin)
	uint32_t numBandWindows;  // 1 or 3
	uint32_t pw, ph; 	/* dimensions of precinct grid */
	grk_pt cblkExpn;
	grk_pt precinctStart;
	grk_pt precinctExpn;
	grk_plugin_tile *current_plugin_tile;

};

struct BlockExec : public IOpenable {

	BlockExec();
	TileComponent *tilec;
	uint8_t bandIndex;
	eBandOrientation bandOrientation;
	float stepsize;
	uint32_t cblk_sty;
	uint8_t qmfbid;
	/* code block offset in tile coordinates*/
	uint32_t x;
	uint32_t y;
	// missing bit planes for all blocks in band
	uint8_t k_msbs;
	bool isOpen;
};


struct TileComponent;

struct DecompressBlockExec : public BlockExec {
	DecompressBlockExec();
	bool open(T1Interface *t1);
	void close(void);

	DecompressCodeblock *cblk;
	uint8_t resno;
	uint32_t roishift;

};

struct CompressBlockExec : public BlockExec{
	CompressBlockExec();
	bool open(T1Interface *t1);
	void close(void);

	CompressCodeblock *cblk;
	grk_tile *tile;
	bool doRateControl;
	double distortion;
	int32_t *tiledp;
	uint16_t compno;
	uint8_t resno;
	uint64_t precinctIndex;
	uint64_t cblkno;
	float inv_step_ht;
	const double *mct_norms;
#ifdef DEBUG_LOSSLESS_T1
	int32_t* unencodedData;
#endif
	uint16_t mct_numcomps;

};


}
