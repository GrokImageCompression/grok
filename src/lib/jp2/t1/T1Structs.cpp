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

#include "grk_includes.h"

namespace grk {


Segment::Segment() {
	clear();
}
void Segment::clear() {
	dataindex = 0;
	numpasses = 0;
	len = 0;
	maxpasses = 0;
	numPassesInPacket = 0;
	numBytesInPacket = 0;
}

PacketLengthInfo::PacketLengthInfo(uint32_t mylength, uint32_t bits) :
		len(mylength), len_bits(bits) {
}
PacketLengthInfo::PacketLengthInfo() :
		len(0), len_bits(0) {
}
bool PacketLengthInfo::operator==(const PacketLengthInfo &rhs) const {
	return (rhs.len == len && rhs.len_bits == len_bits);
}

CodePass::CodePass() :
		rate(0), distortiondec(0), len(0), term(0), slope(0) {
}
Layer::Layer() :
		numpasses(0), len(0), disto(0), data(nullptr) {
}

Precinct::Precinct() :
		cw(0), ch(0),
		enc(nullptr), dec(nullptr),
		num_code_blocks(0),
		incltree(nullptr), imsbtree(nullptr) {
}

Codeblock::Codeblock():
		compressedData(nullptr),
		compressedDataSize(0),
		owns_data(false),
		numbps(0),
		numlenbits(0),
		numPassesInPacket(0)
#ifdef DEBUG_LOSSLESS_T2
		,included(false),
#endif
{
}

Codeblock::Codeblock(const Codeblock &rhs): grk_rect_u32(rhs),
		compressedData(rhs.compressedData), compressedDataSize(rhs.compressedDataSize), owns_data(rhs.owns_data),
		numbps(rhs.numbps), numlenbits(rhs.numlenbits), numPassesInPacket(rhs.numPassesInPacket)
#ifdef DEBUG_LOSSLESS_T2
	,included(0)
#endif
{
}
Codeblock& Codeblock::operator=(const Codeblock& rhs){
	if (this != &rhs) { // self-assignment check expected
		x0 = rhs.x0;
		y0 = rhs.y0;
		x1 = rhs.x1;
		y1 = rhs.y1;
		compressedData = rhs.compressedData;
		compressedDataSize = rhs.compressedDataSize;
		owns_data = rhs.owns_data;
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
void Codeblock::clear(){
	compressedData = nullptr;
	owns_data = false;
}
CompressCodeblock::CompressCodeblock() :
				paddedCompressedData(nullptr),
				layers(	nullptr),
				passes(nullptr),
				numPassesInPreviousPackets(0),
				numPassesTotal(0),
				contextStream(nullptr)
{
}
CompressCodeblock::CompressCodeblock(const CompressCodeblock &rhs) :
						Codeblock(rhs),
						paddedCompressedData(rhs.paddedCompressedData),
						layers(	rhs.layers),
						passes(rhs.passes),
						numPassesInPreviousPackets(rhs.numPassesInPreviousPackets),
						numPassesTotal(rhs.numPassesTotal),
						contextStream(rhs.contextStream)
{}

CompressCodeblock& CompressCodeblock::operator=(const CompressCodeblock& rhs){
	if (this != &rhs) { // self-assignment check expected
		Codeblock::operator = (rhs);
		paddedCompressedData = rhs.paddedCompressedData;
		layers = rhs.layers;
		passes = rhs.passes;
		numPassesInPreviousPackets = rhs.numPassesInPreviousPackets;
		numPassesTotal = rhs.numPassesTotal;
		contextStream = rhs.contextStream;
#ifdef DEBUG_LOSSLESS_T2
		packet_length_info = rhs.packet_length_info;
#endif

	}
	return *this;
}

CompressCodeblock::~CompressCodeblock() {
	cleanup();
}
void CompressCodeblock::clear(){
	Codeblock::clear();
	layers = nullptr;
	passes = nullptr;
	contextStream = nullptr;
#ifdef DEBUG_LOSSLESS_T2
	packet_length_info.clear();
#endif
}
bool CompressCodeblock::alloc() {
	if (!layers) {
		/* no memset since data */
		layers = (Layer*) grk_calloc(100, sizeof(Layer));
		if (!layers)
			return false;
	}
	if (!passes) {
		passes = (CodePass*) grk_calloc(100, sizeof(CodePass));
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
bool CompressCodeblock::alloc_data(size_t nominalBlockSize) {
	uint32_t desired_data_size = (uint32_t) (nominalBlockSize * sizeof(uint32_t));
	if (desired_data_size > compressedDataSize) {
		if (owns_data)
			delete[] compressedData;

		// we add two fake zero bytes at beginning of buffer, so that mq coder
		//can be initialized to data[-1] == actualData[1], and still point
		//to a valid memory location
		compressedData = new uint8_t[desired_data_size + grk_cblk_enc_compressed_data_pad_left];
		compressedData[0] = 0;
		compressedData[1] = 0;

		paddedCompressedData = compressedData + grk_cblk_enc_compressed_data_pad_left;
		compressedDataSize = desired_data_size;
		owns_data = true;
	}
	return true;
}

void CompressCodeblock::cleanup() {
	if (owns_data) {
		delete[] compressedData;
		compressedData = nullptr;
		owns_data = false;
	}
	paddedCompressedData = nullptr;
	grk_free(layers);
	layers = nullptr;
	grk_free(passes);
	passes = nullptr;
}

DecompressCodeblock::DecompressCodeblock() {
	init();
}

DecompressCodeblock::~DecompressCodeblock(){
    cleanup();
}

void DecompressCodeblock::clear(){
	Codeblock::clear();
	segs = nullptr;
	cleanup_seg_buffers();
}

DecompressCodeblock::DecompressCodeblock(const DecompressCodeblock &rhs) :
				Codeblock(rhs),
				segs(rhs.segs), numSegments(rhs.numSegments),
				numSegmentsAllocated(rhs.numSegmentsAllocated)
{}

DecompressCodeblock& DecompressCodeblock::operator=(const DecompressCodeblock& rhs){
	if (this != &rhs) { // self-assignment check expected
		*this = DecompressCodeblock(rhs);
	}
	return *this;
}

bool DecompressCodeblock::alloc() {
	if (!segs) {
		segs = new Segment[default_numbers_segments];
		/*fprintf(stderr, "Allocate %u elements of code_block->data\n", default_numbers_segments * sizeof(Segment));*/

		numSegmentsAllocated = default_numbers_segments;

		/*fprintf(stderr, "Allocate 8192 elements of code_block->data\n");*/
		/*fprintf(stderr, "numSegmentsAllocated of code_block->data = %u\n", p_code_block->numSegmentsAllocated);*/

	} else {
		/* sanitize */
		auto l_segs = segs;
		uint32_t l_current_max_segs = numSegmentsAllocated;

		/* Note: since seg_buffers simply holds references to another data buffer,
		 we do not need to copy it to the sanitized block  */
		cleanup_seg_buffers();
		init();
		segs = l_segs;
		numSegmentsAllocated = l_current_max_segs;
	}
	return true;
}

void DecompressCodeblock::init() {
	compressedData = nullptr;
	compressedDataSize = 0;
	owns_data = false;
	segs = nullptr;
	x0 = 0;
	y0 = 0;
	x1 = 0;
	y1 = 0;
	numbps = 0;
	numlenbits = 0;
	numPassesInPacket = 0;
	numSegments = 0;
#ifdef DEBUG_LOSSLESS_T2
	included = 0;
#endif
	numSegmentsAllocated = 0;
}

void DecompressCodeblock::cleanup() {
	if (owns_data) {
		delete[] compressedData;
		compressedData = nullptr;
		owns_data = false;
	}
	cleanup_seg_buffers();
	delete[] segs;
	segs = nullptr;
}

void DecompressCodeblock::cleanup_seg_buffers(){

	for (auto& b : seg_buffers)
		delete b;
	seg_buffers.clear();

}

size_t DecompressCodeblock::getSegBuffersLen(){
	return std::accumulate(seg_buffers.begin(), seg_buffers.end(), 0, [](const size_t s, grk_buf *a){
	   return s + a->len;
	});
}

bool DecompressCodeblock::copy_to_contiguous_buffer(uint8_t *buffer) {
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

Subband::Subband() :
				bandno(0),
				precincts(nullptr),
				numPrecincts(0),
				numbps(0),
				stepsize(0),
				inv_step(0) {
}

//note: don't copy precinct array
Subband::Subband(const Subband &rhs) : grk_rect_u32(rhs),
										bandno(rhs.bandno),
										precincts(nullptr),
										numPrecincts(0),
										numbps(rhs.numbps),
										stepsize(rhs.stepsize),
										inv_step(rhs.inv_step)
{
}

Subband& Subband::operator= (const Subband &rhs){
	if (this != &rhs) { // self-assignment check expected
		*this = Subband(rhs);
	}
	return *this;
}


bool Subband::isEmpty() {
	return ((x1 - x0 == 0) || (y1 - y0 == 0));
}

void Precinct::deleteTagTrees() {
	delete incltree;
	incltree = nullptr;
	delete imsbtree;
	imsbtree = nullptr;
}

void Precinct::initTagTrees() {

	// if cw == 0 or ch == 0,
	// then the precinct has no code blocks, therefore
	// no need for inclusion and msb tag trees
	if (cw > 0 && ch > 0) {
		if (!incltree) {
			try {
				incltree = new TagTree(cw, ch);
			} catch (std::exception &e) {
				GRK_WARN("No incltree created.");
			}
		} else {
			if (!incltree->init(cw, ch)) {
				GRK_WARN("Failed to re-initialize incltree.");
				delete incltree;
				incltree = nullptr;
			}
		}

		if (!imsbtree) {
			try {
				imsbtree = new TagTree(cw, ch);
			} catch (std::exception &e) {
				GRK_WARN("No imsbtree created.");
			}
		} else {
			if (!imsbtree->init(cw, ch)) {
				GRK_WARN("Failed to re-initialize imsbtree.");
				delete imsbtree;
				imsbtree = nullptr;
			}
		}
	}
}

Resolution::Resolution() :
		pw(0),
		ph(0),
		numbands(0)
{}


}
