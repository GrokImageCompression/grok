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
#include "grk_includes.h"
#include "T1Part1.h"
#include "TileProcessor.h"
#include "t1_common.h"
#include "T1.h"
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
void T1Part1::preCompress(CompressBlockExec *block, grk_tile *tile,
		uint32_t &maximum) {
	auto cblk = block->cblk;
	auto w = cblk->width();
	auto h = cblk->height();
	if (!t1->alloc(w,h))
		return;
	auto tileLineAdvance = (tile->comps + block->compno)->getBuffer()->getHighestBufferResWindowREL()->stride - w;
	uint32_t tileIndex = 0;
	uint32_t cblk_index = 0;
	maximum = 0;
	auto uncompressedData = t1->getUncompressedData();
	if (block->qmfbid == 1) {
		for (auto j = 0U; j < h; ++j) {
			for (auto i = 0U; i < w; ++i) {
				int32_t temp = (block->tiledp[tileIndex] *= (1<< T1_NMSEDEC_FRACBITS));
				temp = (int32_t)to_smr(temp);
				uint32_t mag = smr_abs(temp);
				if (mag > maximum)
					maximum = mag;
				uncompressedData[cblk_index] = temp;
				tileIndex++;
				cblk_index++;
			}
			tileIndex += tileLineAdvance;
		}
	} else {
		auto tiledp = (float*)block->tiledp;
		for (auto j = 0U; j < h; ++j) {
			for (auto i = 0U; i < w; ++i) {
				int32_t temp = (int32_t)grk_lrintf((tiledp[tileIndex] /block->stepsize) * (1 << T1_NMSEDEC_FRACBITS));
				temp = (int32_t)to_smr(temp);
				uint32_t mag = smr_abs(temp);
				if (mag > maximum)
					maximum = mag;
				uncompressedData[cblk_index] = temp;
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

	auto disto = t1->compress_cblk(&cblkexp, max, block->bandOrientation,
			block->compno,
			(uint8_t)((block->tile->comps + block->compno)->numresolutions - 1 - block->resno),
			block->qmfbid,
			block->stepsize,
			block->cblk_sty,
			block->mct_norms,
			block->mct_numcomps,
			block->doRateControl);

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
  	if (!cblk->seg_buffers.empty()) {
		size_t totalSegLen = cblk->getSegBuffersLen() + grk_cblk_dec_compressed_data_pad_right;
		t1->allocCompressedData(totalSegLen);
		size_t offset = 0;
		auto compressedData = t1->getCompressedDataBuffer();
		for (auto& b : cblk->seg_buffers) {
			memcpy(compressedData+ offset, b->buf, b->len);
			offset += b->len;
		}
		cblk->allocUncompressedData(true);
		t1->attachUncompressedData(cblk->getUncomressedDataPtr(),
									cblk->width(),
									cblk->height());
		cblk->m_failed = t1->decompress_cblk(cblk,
						compressedData,
						block->bandOrientation,
						block->cblk_sty);
		if (!cblk->m_failed)
			return false;
  	}

	return block->tilec->postProcess(t1->getUncompressedData(), block,false);
}

}
}
