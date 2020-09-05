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
#include <T1Part1.h>
#include "grk_includes.h"
#include "testing.h"
#include <algorithm>
using namespace std;

namespace grk {
namespace t1_part1{

T1Part1::T1Part1(bool isEncoder, TileCodingParams *tcp, uint32_t maxCblkW,
		uint32_t maxCblkH) : t1(nullptr){
	(void) tcp;
	t1 = t1_create(isEncoder);
	if (!isEncoder) {
	   t1->cblkdatabuffersize = maxCblkW * maxCblkH * (uint32_t)sizeof(int32_t);
	   t1->cblkdatabuffer = (uint8_t*)grk_malloc(t1->cblkdatabuffersize);
   }
}
T1Part1::~T1Part1() {
	t1_destroy( t1);
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


void T1Part1::preEncode(encodeBlockInfo *block, grk_tile *tile,
		uint32_t &maximum) {
	auto cblk = block->cblk;
	auto w = cblk->width();
	auto h = cblk->height();
	if (!t1_allocate_buffers(t1, w,h))
		return;
	t1->data_stride = w;
	auto tileLineAdvance = (tile->comps + block->compno)->buf->stride() - w;
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
double T1Part1::compress(encodeBlockInfo *block, grk_tile *tile,
		uint32_t max, bool doRateControl) {
	auto cblk = block->cblk;
	cblk_enc cblkexp;
	memset(&cblkexp, 0, sizeof(cblk_enc));

	cblkexp.x0 = block->x;
	cblkexp.y0 = block->y;
	cblkexp.x1 = block->x + cblk->width();
	cblkexp.y1 = block->y + cblk->height();
	assert(cblk->width() > 0);
	assert(cblk->height() > 0);

	cblkexp.data = cblk->paddedCompressedData;
	cblkexp.data_size = cblk->compressedDataSize;

	auto disto = t1_encode_cblk(t1, &cblkexp, max, block->bandno,
			block->compno,
			(tile->comps + block->compno)->numresolutions - 1 - block->resno,
			block->qmfbid, block->stepsize, block->cblk_sty,
			block->mct_norms, block->mct_numcomps, doRateControl);

	cblk->numPassesTotal = cblkexp.totalpasses;
	cblk->numbps = cblkexp.numbps;
	for (uint32_t i = 0; i < cblk->numPassesTotal; ++i) {
		auto passexp = cblkexp.passes + i;
		auto passgrk = cblk->passes + i;
		passgrk->distortiondec = passexp->distortiondec;
		passgrk->len = passexp->len;
		passgrk->rate = passexp->rate;
		passgrk->term = passexp->term;
	}

	t1_code_block_enc_deallocate(&cblkexp);
	cblkexp.data = nullptr;

 	return disto;
}

bool T1Part1::decompress(decodeBlockInfo *block) {
	auto cblk = block->cblk;
	bool ret;

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
	chunk.data = t1->cblkdatabuffer;

	cblk_dec cblkexp;
	memset(&cblkexp, 0, sizeof(cblk_dec));
	cblkexp.chunks = &chunk;
	cblkexp.x0 = block->x;
	cblkexp.y0 = block->y;
	cblkexp.x1 = block->x + cblk->width();
	cblkexp.y1 = block->y + cblk->height();
	assert(cblk->width() > 0);
	assert(cblk->height() > 0);
	cblkexp.real_num_segs = cblk->numSegments;
	auto segs = new seg[cblk->numSegments];
	for (uint32_t i = 0; i < cblk->numSegments; ++i){
		auto segp = segs + i;
		memset(segp, 0, sizeof(seg));
		auto sgrk = cblk->segs + i;
		segp->len = sgrk->len;
		assert(segp->len <= total_seg_len);
		segp->real_num_passes = sgrk->numpasses;
	}
	cblkexp.segs = segs;
	// subtract roishift as it was added when packet was parsed
	// and exp uses subtracted value
	cblkexp.numbps = cblk->numbps - block->roishift;

    ret =t1_decode_cblk(t1,
    				&cblkexp,
    				block->bandno,
					block->roishift,
					block->cblk_sty);

	delete[] segs;
	return ret;
}

bool T1Part1::postDecode(decodeBlockInfo *block) {

	auto cblk = block->cblk;
	if (cblk->seg_buffers.empty())
		return true;
	uint32_t qmfbid = block->qmfbid;
	float stepsize_over_two = block->stepsize/2;
	auto tilec_data = block->tiledp;
	uint32_t stride = block->stride;
	uint32_t cblk_w = (uint32_t) (cblk->x1 - cblk->x0);
	uint32_t cblk_h = (uint32_t) (cblk->y1 - cblk->y0);

	auto src = t1->data;
	uint32_t roishift = block->roishift;

	//1. ROI
	if (roishift) {
		auto src_roi = src;
		int32_t thresh = 1 << roishift;
		for (uint32_t j = 0; j < cblk_h; ++j) {
			for (uint32_t i = 0; i < cblk_w; ++i) {
				int32_t val = src_roi[i];
				int32_t mag = abs(val);
				if (mag >= thresh) {
					mag >>= roishift;
					src_roi[i] = val < 0 ? -mag : mag;
				}
			}
			src_roi += cblk_w;
		}
	}

	if (!block->tilec->whole_tile_decoding) {
    	if (qmfbid == 1) {
    		for (uint32_t j = 0; j < cblk_h; ++j) {
    			uint32_t i = 0;
    			for (; i < (cblk_w & ~(uint32_t) 3U); i += 4U) {
    				src[i + 0U] /= 2;
    				src[i + 1U] /= 2;
    				src[i + 2U] /= 2;
    				src[i + 3U] /= 2;
    			}
    			for (; i < cblk_w; ++i)
    				src[i] /= 2;
    			src += cblk_w;
     		}
    	} else {
			float *GRK_RESTRICT tiledp = (float*) src;
			for (uint32_t j = 0; j < cblk_h; ++j) {
				float *GRK_RESTRICT tiledp2 = tiledp;
				for (uint32_t i = 0; i < cblk_w; ++i) {
					float tmp = (float) (*src) * stepsize_over_two;
					*tiledp2 = tmp;
					src++;
					tiledp2++;
				}
				tiledp += cblk_w;
			}
    	}
		// write directly from t1 to sparse array
        if (!block->tilec->m_sa->write(block->x,
					  block->y,
					  block->x + cblk_w,
					  block->y + cblk_h,
					  t1->data,
					  1,
					  cblk_w,
					  true)) {
			  return false;
		  }
	} else {
		auto dest = tilec_data;
		if (qmfbid == 1) {
			int32_t *GRK_RESTRICT tiledp = dest;
			for (uint32_t j = 0; j < cblk_h; ++j) {
				uint32_t i = 0;
				for (; i < (cblk_w & ~(uint32_t) 3U); i += 4U) {
					int32_t tmp0 = src[i + 0U];
					int32_t tmp1 = src[i + 1U];
					int32_t tmp2 = src[i + 2U];
					int32_t tmp3 = src[i + 3U];
					((int32_t*) tiledp)[i + 0U] = tmp0/ 2;
					((int32_t*) tiledp)[i + 1U] = tmp1/ 2;
					((int32_t*) tiledp)[i + 2U] = tmp2/ 2;
					((int32_t*) tiledp)[i + 3U] = tmp3/ 2;
				}
				for (; i < cblk_w; ++i)
					((int32_t*) tiledp)[i] =  src[i] / 2;
				src += (size_t) cblk_w;
				tiledp += stride;
			}
		} else {
			float *GRK_RESTRICT tiledp = (float*) dest;
			for (uint32_t j = 0; j < cblk_h; ++j) {
				float *GRK_RESTRICT tiledp2 = tiledp;
				for (uint32_t i = 0; i < cblk_w; ++i) {
					float tmp = (float) (*src) * stepsize_over_two;
					*tiledp2 = tmp;
					src++;
					tiledp2++;
				}
				tiledp += stride;
			}
		}

	}

	// note: if no MCT, then we could do dc shift and clamp here

	return true;
}

}
}
