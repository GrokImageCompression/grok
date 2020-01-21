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
#include "T1Part1OPJ.h"
#include "testing.h"
#include "grok_malloc.h"
#include <algorithm>
#include <cmath>

namespace grk {
namespace t1_part1{

T1Part1OPJ::T1Part1OPJ(bool isEncoder, grk_tcp *tcp, uint16_t maxCblkW,
		uint16_t maxCblkH) : t1(nullptr){
	(void) tcp;
	t1 = opj_t1_create(isEncoder);
	if (!isEncoder) {
	   t1->cblkdatabuffersize = maxCblkW * maxCblkH * sizeof(int32_t);
	   t1->cblkdatabuffer = (uint8_t*)grok_malloc(t1->cblkdatabuffersize);
   }
}
T1Part1OPJ::~T1Part1OPJ() {
	opj_t1_destroy( t1);
}

static inline int32_t int_fix_mul_t1(int32_t a, int32_t b) {
#if defined(_MSC_VER) && (_MSC_VER >= 1400) && !defined(__INTEL_COMPILER) && defined(_M_IX86)
	int64_t temp = __emul(a, b);
#else
	int64_t temp = (int64_t) a * (int64_t) b;
#endif
	temp += 4096;
	assert((temp >> (13 + 11 - T1_NMSEDEC_FRACBITS)) <= (int64_t)0x7FFFFFFF);
	assert(
			(temp >> (13 + 11 - T1_NMSEDEC_FRACBITS)) >= (-(int64_t)0x7FFFFFFF - (int64_t)1));
	return (int32_t) (temp >> (13 + 11 - T1_NMSEDEC_FRACBITS));
}

void T1Part1OPJ::preEncode(encodeBlockInfo *block, grk_tcd_tile *tile,
		uint32_t &max) {
	auto cblk = block->cblk;
	auto tilec = tile;
	auto w = cblk->x1 - cblk->x0;
	auto h = cblk->y1 - cblk->y0;
	if (!opj_t1_allocate_buffers(t1, w,h))
		return;
	t1->data_stride = w;
	uint32_t tile_width = (tilec->x1 - tilec->x0);
	auto tileLineAdvance = tile_width - w;
	auto tiledp = block->tiledp;
	uint32_t tileIndex = 0;
	uint32_t cblk_index = 0;
	max = 0;
	if (block->qmfbid == 1) {
		for (auto j = 0U; j < h; ++j) {
			for (auto i = 0U; i < w; ++i) {
				int32_t temp = (block->tiledp[tileIndex] *= (1<< T1_NMSEDEC_FRACBITS));
				max = std::max((uint32_t)abs(temp), max);
				t1->data[cblk_index] = temp;
				tileIndex++;
				cblk_index++;
			}
			tileIndex += tileLineAdvance;
		}
	} else {
		for (auto j = 0U; j < h; ++j) {
			for (auto i = 0U; i < w; ++i) {
				int32_t temp = int_fix_mul_t1(tiledp[tileIndex], block->bandconst);
				max = std::max((uint32_t)abs(temp), max);
				t1->data[cblk_index] = temp;
				tileIndex++;
				cblk_index++;
			}
			tileIndex += tileLineAdvance;
		}
	}
}
double T1Part1OPJ::encode(encodeBlockInfo *block, grk_tcd_tile *tile,
		uint32_t max, bool doRateControl) {
	auto cblk = block->cblk;
	opj_tcd_cblk_enc_t cblkopj;
	memset(&cblkopj, 0, sizeof(opj_tcd_cblk_enc_t));

	cblkopj.x0 = block->x;
	cblkopj.y0 = block->y;
	cblkopj.x1 = block->x + cblk->x1 - cblk->x0;
	cblkopj.y1 = block->y + cblk->y1 - cblk->y0;
	assert(cblk->x1 - cblk->x0 > 0);
	assert(cblk->y1 - cblk->y0 > 0);

	cblkopj.data = cblk->data;
	cblkopj.data_size = cblk->data_size;

	auto disto = opj_t1_encode_cblk(t1, &cblkopj, max, block->bandno,
			block->compno,
			(tile->comps + block->compno)->numresolutions - 1 - block->resno,
			block->qmfbid, block->stepsize, block->cblk_sty, tile->numcomps,
			block->mct_norms, block->mct_numcomps, doRateControl);

	cblk->num_passes_encoded = cblkopj.totalpasses;
	cblk->numbps = cblkopj.numbps;
	for (uint32_t i = 0; i < cblk->num_passes_encoded; ++i) {
		auto passopj = cblkopj.passes + i;
		auto passgrk = cblk->passes + i;
		passgrk->distortiondec = passopj->distortiondec;
		passgrk->len = passopj->len;
		passgrk->rate = passopj->rate;
		passgrk->term = passopj->term;
	}

	opj_t1_code_block_enc_deallocate(&cblkopj);
	cblkopj.data = nullptr;

 	return disto;
}

bool T1Part1OPJ::decode(decodeBlockInfo *block) {
	auto cblk = block->cblk;
	bool ret;

  	if (!cblk->seg_buffers.get_len())
		return true;

	auto min_buf_vec = &cblk->seg_buffers;
	uint16_t total_seg_len = (uint16_t) (min_buf_vec->get_len() + numSynthBytes);
	if (t1->cblkdatabuffersize < total_seg_len) {
		uint8_t *new_block = (uint8_t*) grok_realloc(t1->cblkdatabuffer,
				total_seg_len);
		if (!new_block)
			return false;
		t1->cblkdatabuffer = new_block;
		t1->cblkdatabuffersize = total_seg_len;
	}
	size_t offset = 0;
	// note: min_buf_vec only contains segments of non-zero length
	for (int32_t i = 0; i < min_buf_vec->size(); ++i) {
		grk_min_buf *seg = (grk_min_buf*) min_buf_vec->get(i);
		memcpy(t1->cblkdatabuffer + offset, seg->buf, seg->len);
		offset += seg->len;
	}
	t1->mustuse_cblkdatabuffer = false;
	opj_tcd_seg_data_chunk_t chunk;
	chunk.len = t1->cblkdatabuffersize;
	chunk.data = t1->cblkdatabuffer;

	opj_tcd_cblk_dec_t cblkopj;
	memset(&cblkopj, 0, sizeof(opj_tcd_cblk_dec_t));
	cblkopj.numchunks = 1;
	cblkopj.chunks = &chunk;
	cblkopj.x0 = block->x;
	cblkopj.y0 = block->y;
	cblkopj.x1 = block->x + cblk->x1 - cblk->x0;
	cblkopj.y1 = block->y + cblk->y1 - cblk->y0;
	assert(cblk->x1 - cblk->x0 > 0);
	assert(cblk->y1 - cblk->y0 > 0);
	cblkopj.real_num_segs = cblk->numSegments;
	auto segs = new opj_tcd_seg_t[cblk->numSegments];
	for (int i = 0; i < cblk->numSegments; ++i){
		auto sopj = segs + i;
		memset(sopj, 0, sizeof(opj_tcd_seg_t));
		auto sgrk = cblk->segs + i;
		sopj->len = sgrk->len;
		sopj->real_num_passes = sgrk->numpasses;
	}
	cblkopj.segs = segs;
	// subtract roishift as it was added when packet was parsed
	// and opj uses subtracted value
	cblkopj.numbps = cblk->numbps - block->roishift;

    ret =
    		opj_t1_decode_cblk(t1,
    				&cblkopj,
    				block->bandno,
					block->roishift,
					block->cblk_sty,
					false);

	delete[] segs;
	return ret;
}

void T1Part1OPJ::postDecode(decodeBlockInfo *block) {

	auto cblk = block->cblk;
	if (!cblk->seg_buffers.get_len())
		return;

	opj_tcd_cblk_dec_t cblkopj;
	memset(&cblkopj, 0, sizeof(opj_tcd_cblk_dec_t));
	cblkopj.x0 = block->x;
	cblkopj.y0 = block->y;
	cblkopj.x1 = block->x + cblk->x1 - cblk->x0;
	cblkopj.y1 = block->y + cblk->y1 - cblk->y0;
	auto tileW = block->tilec->x1 - block->tilec->x0;
	auto tileH = block->tilec->y1 - block->tilec->y0;
	assert(cblkopj.x1 <= tileW);
	assert(cblkopj.y1 <= tileH);
    post_decode(t1,
    		&cblkopj,
			block->roishift,
			block->qmfbid,
			block->stepsize,
			block->tiledp,
			block->tilec->x1 - block->tilec->x0,
			block->tilec->y1 - block->tilec->y0
			);
}
}
}
