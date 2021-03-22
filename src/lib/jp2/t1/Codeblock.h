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

#include "grk_includes.h"
#include <vector>
namespace grk {

// code segment (code block can be encoded into multiple segments)
struct Segment {
	Segment() {
		clear();
	}
	void clear() {
		numpasses = 0;
		len = 0;
		maxpasses = 0;
		numPassesInPacket = 0;
		numBytesInPacket = 0;
	}
	uint32_t numpasses;		   	// number of passes in segment
	uint32_t len;               // total length of segment
	uint32_t maxpasses;			// maximum number of passes in segment
	uint32_t numPassesInPacket;	// number of passes contributed by current packet
	uint32_t numBytesInPacket;  // number of bytes contributed by current packet
};

// compressing/decoding pass
struct CodePass {
	CodePass() :rate(0),
			distortiondec(0),
			len(0),
			term(0),
			slope(0) {
	}
	uint32_t rate;
	double distortiondec;
	uint32_t len;
	uint8_t term;
	uint16_t slope;  //ln(slope) in 8.8 fixed point
};
//quality layer
struct Layer {
	Layer() :numpasses(0),
			len(0),
			disto(0),
			data(nullptr) {
	}
	uint32_t numpasses; // Number of passes in the layer
	uint32_t len; 		// number of bytes in layer
	double disto; 		// layer distortion decrease
	uint8_t *data; 		// compressed layer data
};

// note: block lives in canvas coordinates
struct Codeblock : public grkBuffer2d<int32_t, AllocatorAligned>, public ICacheable {
	Codeblock():
			numbps(0),
			numlenbits(0),
			numPassesInPacket(0)
			#ifdef DEBUG_LOSSLESS_T2
			,included(false)
			#endif
	{}
	virtual ~Codeblock(){
		compressedStream.dealloc();
	}
	Codeblock(const Codeblock &rhs): grkBuffer2d(rhs),
									numbps(rhs.numbps),
									numlenbits(rhs.numlenbits),
									numPassesInPacket(rhs.numPassesInPacket)
									#ifdef DEBUG_LOSSLESS_T2
									,included(0)
									#endif
	{
		compressedStream = rhs.compressedStream;
	}
	Codeblock& operator=(const Codeblock& rhs){
		if (this != &rhs) { // self-assignment check expected
			x0 = rhs.x0;
			y0 = rhs.y0;
			x1 = rhs.x1;
			y1 = rhs.y1;
			compressedStream = rhs.compressedStream;
			numbps = rhs.numbps;
			numlenbits = rhs.numlenbits;
			numPassesInPacket = rhs.numPassesInPacket;
			#ifdef DEBUG_LOSSLESS_T2
			included = rhs.included;
			packet_length_info = rhs.packet_length_info;
			#endif
		}
		return *this;
	}
	void setRect(grkRectU32 r){
		(*(grkRectU32*)this) = r;
	}
    grkBufferU8 compressedStream;
	uint32_t numbps;
	uint32_t numlenbits;
	uint32_t numPassesInPacket; 	/* number of passes encoded in current packet */
protected:
#ifdef DEBUG_LOSSLESS_T2
	uint32_t included;
	std::vector<PacketLengthInfo> packet_length_info;
#endif
};

struct CompressCodeblock : public Codeblock {
	CompressCodeblock() :
					paddedCompressedStream(nullptr),
					layers(	nullptr),
					passes(nullptr),
					numPassesInPreviousPackets(0),
					numPassesTotal(0),
					contextStream(nullptr)
	{}
	virtual ~CompressCodeblock() {
		compressedStream.dealloc();
		grkFree(layers);
		grkFree(passes);
	}
	bool init() {
		if (!layers) {
			layers = (Layer*) grkCalloc(100, sizeof(Layer));
			if (!layers)
				return false;
		}
		if (!passes) {
			passes = (CodePass*) grkCalloc(100, sizeof(CodePass));
			if (!passes)
				return false;
		}
		return true;
	}
	/**
	 * Allocates data memory for an compressing code block.
	 * We actually allocate 2 more bytes than specified, and then offset data by +2.
	 * This is done so that we can safely initialize the MQ coder pointer to data-1,
	 * without risk of accessing uninitialized memory.
	 */
	bool allocData(size_t nominalBlockSize) {
		uint32_t desired_data_size = (uint32_t) (nominalBlockSize * sizeof(uint32_t));
		// we add two fake zero bytes at beginning of buffer, so that mq coder
		//can be initialized to data[-1] == actualData[1], and still point
		//to a valid memory location
		auto buf =  new uint8_t[desired_data_size + grk_cblk_enc_compressed_data_pad_left];
		buf[0] = 0;
		buf[1] = 0;

		paddedCompressedStream = buf + grk_cblk_enc_compressed_data_pad_left;
		compressedStream.buf = buf;
		compressedStream.len = desired_data_size;
		compressedStream.owns_data = true;

		return true;
	}
	uint8_t *paddedCompressedStream;
	Layer *layers;
	CodePass *passes;
	uint32_t numPassesInPreviousPackets; /* number of passes in previous packets */
	uint32_t numPassesTotal; 			/* total number of passes in all layers */
	uint32_t *contextStream;
};

struct DecompressCodeblock: public Codeblock {


	DecompressCodeblock() : 	segs(nullptr),
													numSegments(0),
													#ifdef DEBUG_LOSSLESS_T2
													included(0),
													#endif
													numSegmentsAllocated(0)
	{}
	virtual ~DecompressCodeblock(){
		cleanUpSegBuffers();
		delete[] segs;
	}
	Segment* getSegment(uint32_t segmentIndex){
		if (!segs) {
			numSegmentsAllocated = 1;
			segs = new Segment[numSegmentsAllocated];
			numSegmentsAllocated = 1;
		} else if (segmentIndex >= numSegmentsAllocated){
			auto new_segs = new Segment[2*numSegmentsAllocated];
			for (uint32_t i = 0; i < numSegmentsAllocated; ++i)
				new_segs[i] = segs[i];
			numSegmentsAllocated *= 2;;
			delete[] segs;
			segs = new_segs;
		}

		return segs + segmentIndex;
	}
	bool init(){
		return true;
	}
	uint32_t  getNumSegments(void){
		return numSegments;
	}
	Segment* getCurrentSegment(void){
		return numSegments ? getSegment(numSegments-1) : nullptr;
	}
	Segment* nextSegment(void){
		numSegments++;
		return getCurrentSegment();
	}
	void cleanUpSegBuffers(){
		for (auto& b : seg_buffers)
			delete b;
		seg_buffers.clear();
		numSegments = 0;
	}
	size_t getSegBuffersLen(){
		return std::accumulate(seg_buffers.begin(), seg_buffers.end(), (size_t)0, [](const size_t s, grkBufferU8 *a){
		   return (s + a->len);
		});
	}
	bool copyToContiguousBuffer(uint8_t *buffer) {
		if (!buffer)
			return false;
		size_t offset = 0;
		for (auto& buf : seg_buffers){
			if (buf->len){
				memcpy(buffer + offset, buf->buf, buf->len);
				offset += buf->len;
			}
		}
		return true;
	}
	std::vector<grkBufferU8*> seg_buffers;
private:
	Segment *segs; 					/* information on segments */
	uint32_t numSegments; 			/* number of segment in block*/
	uint32_t numSegmentsAllocated; 	// number of segments allocated for segs array
};

}
