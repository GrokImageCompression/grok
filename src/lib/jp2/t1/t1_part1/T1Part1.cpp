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
#include "T1Part1.h"
#include "TileProcessor.h"
#include "t1_common.h"
#include "T1.h"
#include "testing.h"
#include <algorithm>
using namespace std;

namespace grk {
namespace t1_part1{

T1Part1::T1Part1(bool isCompressor, uint32_t maxCblkW,	uint32_t maxCblkH) : t1(nullptr){
	t1 = new T1(isCompressor,maxCblkW,	maxCblkH);

}
T1Part1::~T1Part1() {
	delete t1;
}

/**
 Multiply two fixed-point numbers.
 @param  a 13-bit precision fixed point number
 @param  b 11-bit precision fixed point number
 @return a * b in T1_NMSEDEC_FRACBITS-bit precision fixed point
 */
static inline int32_t int_fix_mul_t1(int32_t a, int32_t b) {
#if defined(_MSC_VER) && (_MSC_VER >= 1400) && !defined(__INTEL_COMPILER) && defined(_M_IX86)
	int64_t temp = __emul(a, b);
#else
	int64_t temp = (int64_t) a * (int64_t) b;
#endif
	temp += 1<<(13 + 11 - T1_NMSEDEC_FRACBITS - 1);
	assert((temp >> (13 + 11 - T1_NMSEDEC_FRACBITS)) <= (int64_t)0x7FFFFFFF);
	assert(
			(temp >> (13 + 11 - T1_NMSEDEC_FRACBITS)) >= (-(int64_t)0x7FFFFFFF - (int64_t)1));
	return (int32_t) (temp >> (13 + 11 - T1_NMSEDEC_FRACBITS));
}


void T1Part1::preCompress(CompressBlockExec *block, grk_tile *tile,
		uint32_t &maximum) {
	auto cblk = block->cblk;
	auto w = cblk->width();
	auto h = cblk->height();
	if (!t1->allocate_buffers(w,h))
		return;
	t1->data_stride = w;
	auto tileLineAdvance = (tile->comps + block->compno)->getBuffer()->stride() - w;
	auto tiledp = block->tiledp;
	uint32_t tileIndex = 0;
	uint32_t cblk_index = 0;
	maximum = 0;
	if (block->qmfbid == 1) {
		for (auto j = 0U; j < h; ++j) {
			for (auto i = 0U; i < w; ++i) {
				int32_t temp = (block->tiledp[tileIndex] *= (1<< T1_NMSEDEC_FRACBITS));
				temp = to_smr(temp);
				maximum = max((uint32_t)smr_abs(temp), maximum);
				t1->data[cblk_index] = temp;
				tileIndex++;
				cblk_index++;
			}
			tileIndex += tileLineAdvance;
		}
	} else {
		for (auto j = 0U; j < h; ++j) {
			for (auto i = 0U; i < w; ++i) {
				int32_t temp = int_fix_mul_t1(tiledp[tileIndex], block->inv_step);
				temp = to_smr(temp);
				maximum = max((uint32_t)smr_abs(temp), maximum);
				t1->data[cblk_index] = temp;
				tileIndex++;
				cblk_index++;
			}
			tileIndex += tileLineAdvance;
		}
	}
}
bool T1Part1::compress(CompressBlockExec *block) {
	uint32_t max = 0;
	preCompress(block,block->tile,max);

	auto cblk = block->cblk;
	cblk_enc cblkexp;
	memset(&cblkexp, 0, sizeof(cblk_enc));

	cblkexp.x0 = block->x;
	cblkexp.y0 = block->y;
	cblkexp.x1 = block->x + cblk->width();
	cblkexp.y1 = block->y + cblk->height();
	assert(cblk->width() > 0);
	assert(cblk->height() > 0);

	cblkexp.data = cblk->paddedCompressedStream;

	auto disto = t1->compress_cblk(&cblkexp, max, block->band_orientation,
			block->compno,
			(block->tile->comps + block->compno)->numresolutions - 1 - block->resno,
			block->qmfbid, block->stepsize, block->cblk_sty,
			block->mct_norms, block->mct_numcomps, block->doRateControl);

	cblk->numPassesTotal = cblkexp.numPassesTotal;
	cblk->numbps = cblkexp.numbps;
	for (uint32_t i = 0; i < cblk->numPassesTotal; ++i) {
		auto passexp = cblkexp.passes + i;
		auto passgrk = cblk->passes + i;
		passgrk->distortiondec = passexp->distortiondec;
		passgrk->len = passexp->len;
		passgrk->rate = passexp->rate;
		passgrk->term = passexp->term;
	}

    t1->code_block_enc_deallocate(&cblkexp);
	cblkexp.data = nullptr;

 	block->distortion =  disto;

 	return true;
}

bool T1Part1::decompress(DecompressBlockExec *block) {
	auto cblk = block->cblk;
  	if (cblk->seg_buffers.empty())
		return true;

	size_t total_seg_len = cblk->getSegBuffersLen() + grk_cblk_dec_compressed_data_pad_right;
	if (t1->cblkdatabuffersize < total_seg_len) {
		auto new_block = (uint8_t*) grk_realloc(t1->cblkdatabuffer,
				total_seg_len);
		if (!new_block)
			return false;
		t1->cblkdatabuffer = new_block;
		t1->cblkdatabuffersize = (uint32_t)total_seg_len;
	}
	size_t offset = 0;
	for (auto& b : cblk->seg_buffers) {
		memcpy(t1->cblkdatabuffer + offset, b->buf, b->len);
		offset += b->len;
	}
	seg_data_chunk chunk;
	chunk.len = t1->cblkdatabuffersize;
	chunk.buf = t1->cblkdatabuffer;

	cblk_dec cblkexp;
	memset(&cblkexp, 0, sizeof(cblk_dec));
	cblkexp.seg_buffers = &chunk;
	cblkexp.x0 = block->x;
	cblkexp.y0 = block->y;
	cblkexp.x1 = block->x + cblk->width();
	cblkexp.y1 = block->y + cblk->height();
	assert(cblk->width() > 0);
	assert(cblk->height() > 0);
	cblkexp.numSegments = cblk->numSegments;
	auto segs = new seg[cblk->numSegments];
	for (uint32_t i = 0; i < cblk->numSegments; ++i){
		auto segp = segs + i;
		memset(segp, 0, sizeof(seg));
		auto sgrk = cblk->segs + i;
		segp->len = sgrk->len;
		assert(segp->len <= total_seg_len);
		segp->numpasses = sgrk->numpasses;
	}
	cblkexp.segs = segs;
	// subtract roishift as it was added when packet was parsed
	// and exp uses subtracted value
	cblkexp.numbps = cblk->numbps;

    bool ret =t1->decompress_cblk(&cblkexp,
								block->band_orientation,
								block->cblk_sty);

	delete[] segs;
	if (!ret)
		return false;

	return block->tilec->postDecompress(t1->data, block,false);
}

}
}
