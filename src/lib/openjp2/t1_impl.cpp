/*
*    Copyright (C) 2016-2018 Grok Image Compression Inc.
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
#include "t1_decode.h"
#include "t1_decode_opt.h"
#include "t1_encode.h"


namespace grk {

t1_impl::t1_impl(bool isEncoder, 
				tcp_t *tcp,
				uint16_t maxCblkW,
				uint16_t maxCblkH) : t1_decoder(nullptr), t1_encoder(nullptr) {
	(void)tcp;
	if (isEncoder) {
		t1_encoder = new t1_encode();
		if (!t1_encoder->allocateBuffers(maxCblkW,	maxCblkH)) {
			throw std::exception();
		}
	}
	else {
		tccp_t *tccp = &tcp->tccps[0];
		if (!tccp->cblksty)
			t1_decoder = new t1_decode_opt(maxCblkW,maxCblkH);
		else
			t1_decoder = new t1_decode(maxCblkW, maxCblkH);
	}
}
t1_impl::~t1_impl() {
	delete t1_decoder;
	delete t1_encoder;
}
void t1_impl::preEncode(encodeBlockInfo* block, tcd_tile_t *tile, uint32_t& max) {
	t1_encoder->preEncode(block, tile, max);
}
double t1_impl::encode(encodeBlockInfo* block, 
						tcd_tile_t *tile, 
						uint32_t max,
						bool doRateControl) {
	double dist = t1_encoder->encode_cblk(block->cblk,
										(uint8_t)block->bandno,
										block->compno,
										(tile->comps + block->compno)->numresolutions - 1 - block->resno,
										block->qmfbid,
										block->stepsize,
										block->cblksty,
										tile->numcomps,
										block->mct_norms,
										block->mct_numcomps,
										max, 
										doRateControl);
#ifdef DEBUG_LOSSLESS_T1
		t1_decode* t1Decode = new t1_decode(t1_encoder->w, t1_encoder->h);

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
		t1Decode->decode_cblk(cblkDecode, block->bandno, 0, 0);

		//compare
		auto index = 0;
		for (uint32_t j = 0; j < t1_encoder->h; ++j) {
			for (uint32_t i = 0; i < t1_encoder->w; ++i) {
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
	return dist;
}
bool t1_impl::decode(decodeBlockInfo* block) {
	return t1_decoder->decode_cblk(	block->cblk,
						(uint8_t)block->bandno,
						block->roishift,
						block->cblksty);
}

void t1_impl::postDecode(decodeBlockInfo* block) {
	t1_decoder->postDecode(block);
}


}
