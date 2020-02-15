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
#include "T1HT.h"
#include "testing.h"
#include "grok_malloc.h"
#include <algorithm>
using namespace std;
#include "ojph_block_decoder.h"
#include "t1_decode_base.h"

namespace grk {
namespace t1_ht {

T1HT::T1HT(bool isEncoder, grk_tcp *tcp, uint16_t maxCblkW,
		uint16_t maxCblkH) : coded_data(nullptr),
							coded_data_size(0),
							decoded_data(nullptr){
	(void) tcp;
	if (isEncoder) {
	} else {
		coded_data_size = maxCblkW*maxCblkH* sizeof(int32_t);
		coded_data = new uint8_t[coded_data_size];
		decoded_data = new int32_t[maxCblkW*maxCblkH];

	}
}
T1HT::~T1HT() {
   delete[] coded_data;
   delete[] decoded_data;
}
void T1HT::preEncode(encodeBlockInfo *block, grk_tcd_tile *tile,
		uint32_t &max) {
	(void)block;
	(void)tile;
	(void)max;
}
double T1HT::encode(encodeBlockInfo *block, grk_tcd_tile *tile, uint32_t max,
		bool doRateControl) {
	(void)block;
	(void)tile;
	(void)max;
	(void)doRateControl;
  return 0;
}
bool T1HT::decode(decodeBlockInfo *block) {
	auto cblk = block->cblk;
	bool ret;
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
	   ojph::local::ojph_decode_codeblock(coded_data, decoded_data,
									   block->k_msbs,
									   num_passes,
									   offset,
									   0,
									   cblk->x1 - cblk->x0,
									   cblk->y1 - cblk->y0,
									   cblk->x1 - cblk->x0);
   else
	   memset(decoded_data, 0, (cblk->x1 - cblk->x0) * (cblk->y1 - cblk->y0) * sizeof(int32_t));
   return true;

}

void T1HT::postDecode(decodeBlockInfo *block) {
	auto cblk = block->cblk;
	uint16_t w =  cblk->x1 - cblk->x0;
	uint16_t h =  cblk->y1 - cblk->y0;

	auto t1_data = decoded_data;

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
		t1_data = decoded_data;
	}
*/
	int32_t shift = 31 - (block->k_msbs + 1);
	uint32_t tile_width = block->tilec->width();
	if (block->qmfbid == 1) {
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
		float *restrict tile_data = (float*) block->tiledp;
		for (auto j = 0U; j < h; ++j) {
			float *restrict tile_row_data = tile_data;
			for (auto i = 0U; i < w; ++i) {
			   int32_t temp = *t1_data;
		       float val = (temp & 0x7FFFFFFF) * block->stepsize;
		       tile_row_data[i] = (temp & 0x80000000) ? -val : val;
			   t1_data++;
			}
			tile_data += tile_width;
		}
	}
}

}
}
