/*
*    Copyright (C) 2016-2017 Grok Image Compression Inc.
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
#include "testing.h"
#include "t1_impl.h"
#include "mqc.h"
#include "t1.h"
#include "t1_opt.h"

namespace grk {

static inline int32_t int_fix_mul_t1(int32_t a, int32_t b)
{
#if defined(_MSC_VER) && (_MSC_VER >= 1400) && !defined(__INTEL_COMPILER) && defined(_M_IX86)
	int64_t temp = __emul(a, b);
#else
	int64_t temp = (int64_t)a * (int64_t)b;
#endif
	temp += 4096;
	assert((temp >> (13 + 11 - T1_NMSEDEC_FRACBITS)) <= (int64_t)0x7FFFFFFF);
	assert((temp >> (13 + 11 - T1_NMSEDEC_FRACBITS)) >= (-(int64_t)0x7FFFFFFF - (int64_t)1));
	return (int32_t)(temp >> (13 + 11 - T1_NMSEDEC_FRACBITS));
}

t1_impl::t1_impl(bool isEncoder, tcp_t *tcp, tcd_tile_t *tile, uint32_t maxCblkW,uint32_t maxCblkH) : t1(nullptr), t1_opt(nullptr), doOpt(false) {
	if (isEncoder) {
		doOpt = true;
		for (uint32_t compno = 0; compno < tile->numcomps; ++compno) {
			tccp_t* tccp = tcp->tccps + compno;
			if (tccp->cblksty != 0 &&
				tccp->cblksty != J2K_CCP_CBLKSTY_RESET &&
				tccp->cblksty != J2K_CCP_CBLKSTY_TERMALL) {
				doOpt = false;
				break;
			}
		}
	}
	if (doOpt) {
		t1_opt = new t1_opt_t(isEncoder);
		if (!t1_opt_allocate_buffers(t1_opt,
									maxCblkW,
									maxCblkH)) {
			throw std::exception();
		}
	}
	else {
		t1 = new t1_t(isEncoder, (uint16_t)maxCblkW, (uint16_t)maxCblkH);
		if (!t1_allocate_buffers(t1,
			maxCblkW,
			maxCblkH)) {
			throw std::exception();
		}
	}
}
t1_impl::~t1_impl() {
	if (t1)
		delete t1;
	if (t1_opt)
		delete t1_opt;
}

// ENCODED

void t1_impl::preEncode(encodeBlockInfo* block, tcd_tile_t *tile, uint32_t& max) {
	auto state = grok_plugin_get_debug_state();
	if (doOpt) {
		//1. prepare low-level encode
		auto tilec = tile->comps + block->compno;
		t1_opt_init_buffers(t1_opt,
			(block->cblk->x1 - block->cblk->x0),
			(block->cblk->y1 - block->cblk->y0));

		uint32_t tile_width = (tilec->x1 - tilec->x0);
		auto tileLineAdvance = tile_width - t1_opt->w;
		auto tiledp = block->tiledp;
#ifdef DEBUG_LOSSLESS_T1
		block->unencodedData = new int32_t[t1_opt->w * t1_opt->h];
#endif
		uint32_t tileIndex = 0;
		max = 0;
		uint32_t cblk_index = 0;
		if (block->qmfbid == 1) {
			for (auto j = 0U; j < t1_opt->h; ++j) {
				for (auto i = 0U; i < t1_opt->w; ++i) {
					int32_t tmp = 0;
					// the next few lines were messing up post-encode comparison
					// between plugin and grok open source
					/*
					// pass through otherwise
					if (!(state & OPJ_PLUGIN_STATE_DEBUG) || (state & OPJ_PLUGIN_STATE_PRE_TR1)) {
					tmp = block->tiledp[tileIndex] *= (1<<T1_NMSEDEC_FRACBITS);
					}
					else
					{
					tmp = block->tiledp[tileIndex];
					}
					*/
#ifdef DEBUG_LOSSLESS_T1
					block->unencodedData[cblk_index] = block->tiledp[tileIndex];
#endif
					tmp = block->tiledp[tileIndex] *= (1 << T1_NMSEDEC_FRACBITS);
					uint32_t mag = (uint32_t)abs(tmp);
					max = std::max<uint32_t>(max, mag);
					t1_opt->data[cblk_index] = mag | ((uint32_t)(tmp < 0) << T1_DATA_SIGN_BIT_INDEX);
					tileIndex++;
					cblk_index++;
				}
				tileIndex += tileLineAdvance;
			}
		}
		else {
			for (auto j = 0U; j < t1_opt->h; ++j) {
				for (auto i = 0U; i < t1_opt->w; ++i) {
					// In lossy mode, we do a direct pass through of the image data in two cases while in debug encode mode:
					// 1. plugin is being used for full T1 encoding, so no need to quantize in OPJ
					// 2. plugin is only being used for pre T1 encoding, and we are applying quantization
					//    in the plugin DWT step
					int32_t tmp = 0;
					if (!(state & OPJ_PLUGIN_STATE_DEBUG) ||
						((state & OPJ_PLUGIN_STATE_PRE_TR1) && !(state & OPJ_PLUGIN_STATE_DWT_QUANTIZATION))) {
						tmp = int_fix_mul_t1(tiledp[tileIndex], block->bandconst);
					}
					else {
						tmp = tiledp[tileIndex];
					}

					uint32_t mag = (uint32_t)abs(tmp);
					uint32_t sign_mag = mag | ((uint32_t)(tmp < 0) << T1_DATA_SIGN_BIT_INDEX);
					max = std::max<uint32_t>(max, mag);
					t1_opt->data[cblk_index] = sign_mag;

					tileIndex++;
					cblk_index++;
				}
				tileIndex += tileLineAdvance;
			}
		}
	}
	else {
		//1. prepare low-level encode
		uint32_t tileIndex = 0, tileLineAdvance;
		t1_allocate_buffers(t1,
			block->cblk->x1 - block->cblk->x0,
			block->cblk->y1 - block->cblk->y0);

		auto tilec = tile->comps + block->compno;
		uint32_t tile_width = tilec->x1 - tilec->x0;
		tileLineAdvance = tile_width - t1->w;
		block->tiledp =
			tile_buf_get_ptr(tilec->buf, block->resno, block->bandno, block->x, block->y);
		t1->data = block->tiledp;
		t1->data_stride = tile_width;
		if (block->qmfbid == 1) {
			for (auto j = 0U; j < t1->h; ++j) {
				for (auto i = 0U; i < t1->w; ++i) {
					// pass through otherwise
					if (!(state & OPJ_PLUGIN_STATE_DEBUG) || (state & OPJ_PLUGIN_STATE_PRE_TR1)) {
						block->tiledp[tileIndex] *= (1 << T1_NMSEDEC_FRACBITS);
					}
					tileIndex++;
				}
				tileIndex += tileLineAdvance;
			}
		}
		else {
			for (auto j = 0U; j < t1->h; ++j) {
				for (auto i = 0U; i < t1->w; ++i) {
					// In lossy mode, we do a direct pass through of the image data in two cases while in debug encode mode:
					// 1. plugin is being used for full T1 encoding, so no need to quantize in OPJ
					// 2. plugin is only being used for pre T1 encoding, and we are applying quantization
					//    in the plugin DWT step
					if (!(state & OPJ_PLUGIN_STATE_DEBUG) ||
						((state & OPJ_PLUGIN_STATE_PRE_TR1) && !(state & OPJ_PLUGIN_STATE_DWT_QUANTIZATION))) {
						block->tiledp[tileIndex] = int_fix_mul_t1(block->tiledp[tileIndex], block->bandconst);
					}
					tileIndex++;
				}
				tileIndex += tileLineAdvance;
			}
		}
	}

}


double t1_impl::encode(encodeBlockInfo* block, tcd_tile_t *tile, uint32_t max) {
	double dist = 0;
	if (doOpt) {
		dist =   t1_opt_encode_cblk(t1_opt,
			block->cblk,
			block->bandno,
			block->compno,
			(tile->comps + block->compno)->numresolutions - 1 - block->resno,
			block->qmfbid,
			block->stepsize,
			block->cblksty,
			tile->numcomps,
			block->mct_norms,
			block->mct_numcomps,
			max);
#ifdef DEBUG_LOSSLESS_T1
		t1_t* t1Decode = new t1_t(false, t1_opt->w, t1_opt->h);

		tcd_cblk_dec_t* cblkDecode = new tcd_cblk_dec_t();
		cblkDecode->data = nullptr;
		cblkDecode->segs = nullptr;
		if (!cblkDecode->alloc()) {
			return dist;
		}
		cblkDecode->x0 = block->cblk->x0;
		cblkDecode->x1 = block->cblk->x1;
		cblkDecode->y0 = block->cblk->y0;
		cblkDecode->y1 = block->cblk->y1;
		cblkDecode->numbps = block->cblk->numbps;
		cblkDecode->numSegments = 1;
		memset(cblkDecode->segs, 0, sizeof(tcd_seg_t));
		auto seg = cblkDecode->segs;
		seg->numpasses = block->cblk->num_passes_encoded;
		auto rate = seg->numpasses ? block->cblk->passes[seg->numpasses - 1].rate : 0;
		seg->len = rate;
		seg->dataindex = 0;
		min_buf_vec_push_back(&cblkDecode->seg_buffers, block->cblk->data, (uint16_t)rate);
		//decode
		t1_decode_cblk(t1Decode, cblkDecode, block->bandno, 0, 0);

		//compare
		auto index = 0;
		for (uint32_t j = 0; j < t1_opt->h; ++j) {
			for (uint32_t i = 0; i < t1_opt->w; ++i) {
				auto valBefore = block->unencodedData[index];
				auto valAfter = t1Decode->data[index] / 2;
				if (valAfter != valBefore) {
					printf("T1 encode @ block location (%d,%d); original data=%x, round trip data=%x\n", i, j, valBefore, valAfter);
				}
				index++;
			}
		}

		delete t1Decode;
		// the next line triggers an exception, so commented out at the moment
		//grok_free(cblkDecode->segs);
		delete cblkDecode;
		delete[] block->unencodedData;
		block->unencodedData = nullptr;
#endif
	}
	else {
		dist = t1_encode_cblk(t1,
			block->cblk,
			block->bandno,
			block->compno,
			(tile->comps + block->compno)->numresolutions - 1 - block->resno,
			block->qmfbid,
			block->stepsize,
			block->cblksty,
			tile->numcomps,
			block->mct_norms,
			block->mct_numcomps);
	}
	return dist;
}

// DECODE

bool t1_impl::decode(decodeBlockInfo* block) {
	if (!t1_allocate_buffers(t1,
							(block->cblk->x1 - block->cblk->x0),
							(block->cblk->y1 - block->cblk->y0))) {
		return false;
	}
	return t1_decode_cblk(t1,
						block->cblk,
						block->bandno,
						(uint32_t)block->roishift,
						block->cblksty);
}

void t1_impl::postDecode(decodeBlockInfo* block) {
	auto t1_data = t1->data;
	// ROI shift
	if (block->roishift) {
		int32_t threshold = 1 << block->roishift;
		for (auto j = 0U; j < t1->h; ++j) {
			for (auto i = 0U; i < t1->w; ++i) {
				auto value = *t1_data;
				auto magnitude = abs(value);
				if (magnitude >= threshold) {
					magnitude >>= block->roishift;
					// ((value > 0) - (value < 0)) == signum(value)
					*t1_data = ((value > 0) - (value < 0))* magnitude;
				}
				t1_data++;
			}
		}
		//reset t1_data to start of buffer
		t1_data = t1->data;
	}

	//dequantization
	uint32_t tile_width = block->tilec->x1 - block->tilec->x0;
	if (block->qmfbid == 1) {
		int32_t* restrict tile_data = block->tiledp;
		for (auto j = 0U; j < t1->h; ++j) {
			int32_t* restrict tile_row_data = tile_data;
			for (auto i = 0U; i < t1->w; ++i) {
				tile_row_data[i] = *t1_data / 2;
				t1_data++;
			}
			tile_data += tile_width;
		}
	}
	else {
		float* restrict tile_data = (float*)block->tiledp;
		for (auto j = 0U; j < t1->h; ++j) {
			float* restrict tile_row_data = tile_data;
			for (auto i = 0U; i < t1->w; ++i) {
				tile_row_data[i] = (float)*t1_data * block->stepsize;
				t1_data++;
			}
			tile_data += tile_width;
		}
	}
}

}