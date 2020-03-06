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
#ifdef _MSC_VER
#include <intrin.h>
#endif
#include "ojph_block_decoder.h"
#include "ojph_block_encoder.h"
#include "ojph_mem.h"
using namespace ojph;
using namespace ojph::local;

#include "grok_includes.h"
#include "T1HT.h"
#include "testing.h"
#include "grok_malloc.h"
#include <algorithm>
using namespace std;


namespace grk {
namespace t1_ht {

T1HT::T1HT(bool isEncoder,
			grk_tcp *tcp,
			uint16_t maxCblkW,
			uint16_t maxCblkH) :
				coded_data_size(isEncoder ? 0 : (uint32_t)(maxCblkW*maxCblkH* sizeof(int32_t))),
				coded_data(isEncoder ? nullptr : new uint8_t[coded_data_size]),
				unencoded_data_size(maxCblkW*maxCblkH),
				unencoded_data(new int32_t[unencoded_data_size]),
				allocator( new mem_fixed_allocator),
				elastic_alloc(new mem_elastic_allocator(1048576))
{
	(void) isEncoder;
	(void) tcp;
}
T1HT::~T1HT() {
   delete[] coded_data;
   delete[] unencoded_data;
}
void T1HT::preEncode(encodeBlockInfo *block, grk_tcd_tile *tile,
		uint32_t &maximum) {
	(void)block;
	(void)tile;

	auto cblk = block->cblk;
	auto w = cblk->x1 - cblk->x0;
	auto h = cblk->y1 - cblk->y0;
	uint32_t tile_width = (tile->comps + block->compno)->width();
	auto tileLineAdvance = tile_width - w;
	uint32_t tileIndex = 0;
	uint32_t cblk_index = 0;
	maximum = 0;

	//convert to sign-magnitude
	if (block->qmfbid == 1) {
		int32_t shift = 31 - (block->k_msbs + 1);
		for (auto j = 0U; j < h; ++j) {
			for (auto i = 0U; i < w; ++i) {
				int32_t temp = block->tiledp[tileIndex];
		        int32_t val = temp >= 0 ? temp : -temp;
		        int32_t sign = temp >= 0 ? 0 : 0x80000000;
		        int32_t res = sign | (val << shift);
		        unencoded_data[cblk_index] = res;
				maximum = max(maximum, (uint32_t)res);
				tileIndex++;
				cblk_index++;
			}
			tileIndex += tileLineAdvance;
		}
	} else {
		for (auto j = 0U; j < h; ++j) {
			int32_t shift = 31 - (block->k_msbs + 1) - 11;
			for (auto i = 0U; i < w; ++i) {
				int32_t temp = block->tiledp[tileIndex];
				int32_t t = (int32_t)((float)temp * block->inv_step_ht * (1<<shift));
				int32_t val = t >= 0 ? t : -t;
				maximum = max((uint32_t)val, maximum);
				int32_t sign = t >= 0 ? 0 : 0x80000000;
				int32_t res = sign | val;
				unencoded_data[cblk_index] = res;
				tileIndex++;
				cblk_index++;
			}
			tileIndex += tileLineAdvance;
		}
	}
}
double T1HT::encode(encodeBlockInfo *block, grk_tcd_tile *tile, uint32_t maximum,
		bool doRateControl) {
	(void)doRateControl;
	(void)tile;

	 coded_lists *next_coded = nullptr;
	 int pass_length[2] = {0,0};
	int32_t shift = 31 - (block->k_msbs + 1);
	auto cblk = block->cblk;
	cblk->numbps = 0;
	//if (maximum >= (uint32_t)1<<(31 - (block->k_msbs+1)))
	{
		uint16_t w =  (uint16_t)(cblk->x1 - cblk->x0);
		uint16_t h =  (uint16_t)(cblk->y1 - cblk->y0);

		 ojph_encode_codeblock(unencoded_data, block->k_msbs,1,
								   w, h, w,
								   pass_length,
								   elastic_alloc,
								   next_coded);

		 cblk->num_passes_encoded = 1;
		 cblk->passes[0].len = (uint16_t)pass_length[0];
		 cblk->passes[0].rate = (uint16_t)pass_length[0];
		 uint32_t abs_max = (maximum & 0x7FFFFFFF) >> shift;
		 cblk->numbps = 1;
		 assert(cblk->data);
		 memcpy(cblk->data, next_coded->buf, pass_length[0]);
	}

  return 0;
}
bool T1HT::decode(decodeBlockInfo *block) {
	auto cblk = block->cblk;
	if (!cblk->seg_buffers.get_len())
		return true;

	auto min_buf_vec = &cblk->seg_buffers;
	uint16_t total_seg_len = (uint16_t) (min_buf_vec->get_len());
	if (coded_data_size < total_seg_len) {
		delete[] coded_data;
		coded_data = new uint8_t[total_seg_len];
		coded_data_size = total_seg_len;
	}
	size_t offset = 0;
	// note: min_buf_vec only contains segments of non-zero length
	for (int32_t i = 0; i < min_buf_vec->size(); ++i) {
		grk_buf *seg = (grk_buf*) min_buf_vec->get(i);
		memcpy(coded_data + offset, seg->buf, seg->len);
		offset += seg->len;
	}

	size_t num_passes = 0;
	for (uint32_t i = 0; i < cblk->numSegments; ++i){
		auto sgrk = cblk->segs + i;
		num_passes += sgrk->numpasses;
	}

   if (num_passes)
	   ojph_decode_codeblock(coded_data, unencoded_data,
									   block->k_msbs,
									   (int)num_passes,
									   (int)offset,
									   0,
									   cblk->x1 - cblk->x0,
									   cblk->y1 - cblk->y0,
									   cblk->x1 - cblk->x0);
   else
	   memset(unencoded_data, 0, (cblk->x1 - cblk->x0) * (cblk->y1 - cblk->y0) * sizeof(int32_t));
   return true;

}

void T1HT::postDecode(decodeBlockInfo *block) {
	auto cblk = block->cblk;
	uint16_t w =  (uint16_t)(cblk->x1 - cblk->x0);
	uint16_t h =  (uint16_t)(cblk->y1 - cblk->y0);

	auto t1_data = unencoded_data;

	/*
	// ROI shift
	if (block->roishift) {
		int32_t threshold = 1 << block->roishift;
		for (auto j = 0U; j < h; ++j) {
			for (auto i = 0U; i < w; ++i) {
				auto value = *t1_data;
				auto magnitude = abs(value);
				if (magnitude >= threshold) {
					magnitude >>= block->roishift;
					// ((value > 0) - (value < 0)) == signum(value)
					*t1_data = ((value > 0) - (value < 0)) * magnitude;
				}
				t1_data++;
			}
		}
		//reset t1_data to start of buffer
		t1_data = unencoded_data;
	}
*/
	uint32_t tile_width = block->tilec->width();
	if (block->qmfbid == 1) {
		int32_t shift = 31 - (block->k_msbs + 1);
		int32_t *restrict tile_data = block->tiledp;
		for (auto j = 0U; j < h; ++j) {
			int32_t *restrict tile_row_data = tile_data;
			for (auto i = 0U; i < w; ++i) {
				int32_t temp = *t1_data;
				int32_t val = (temp & 0x7FFFFFFF) >> shift;
				tile_row_data[i] = (temp & 0x80000000) ? -val : val;
				t1_data++;
			}
			tile_data += tile_width;
		}
	} else {
		int32_t *restrict tile_data = block->tiledp;
		for (auto j = 0U; j < h; ++j) {
			float *restrict tile_row_data = (float*)tile_data;
			for (auto i = 0U; i < w; ++i) {
		       float val = (((*t1_data & 0x7FFFFFFF) * block->stepsize));
		       tile_row_data[i] = (*t1_data & 0x80000000) ? -val : val;
			   t1_data++;
			}
			tile_data += tile_width;
		}
	}
}

}
}
