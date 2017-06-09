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

#include "Tier1.h"
#include "t1.h"
#include "T1Decoder.h"
#include "T1Encoder.h"


namespace grk {


bool Tier1::encodeCodeblocks(tcd_tile_t *tile,
									tcp_t *tcp,
									const double * mct_norms,
									uint32_t mct_numcomps,
									uint32_t numThreads) {

	bool do_opt = true;
	uint32_t compno, resno, bandno, precno;
	tile->distotile = 0;
	for (compno = 0; compno < tile->numcomps; ++compno) {
		tccp_t* tccp = tcp->tccps + compno;
		if (tccp->cblksty != 0 &&
			tccp->cblksty != J2K_CCP_CBLKSTY_RESET &&
			tccp->cblksty != J2K_CCP_CBLKSTY_TERMALL) {
			do_opt = false;
			break;
		}
	}

	std::vector<encodeBlockInfo*> blocks;
	auto maxCblkW = 0;
	auto maxCblkH = 0;

	for (compno = 0; compno < tile->numcomps; ++compno) {
		tcd_tilecomp_t* tilec = &tile->comps[compno];
		tccp_t* tccp = &tcp->tccps[compno];
		for (resno = 0; resno < tilec->numresolutions; ++resno) {
			tcd_resolution_t *res = &tilec->resolutions[resno];

			for (bandno = 0; bandno < res->numbands; ++bandno) {
				tcd_band_t* restrict band = &res->bands[bandno];
				int32_t bandconst = 8192 * 8192 / ((int32_t)floor(band->stepsize * 8192));

				for (precno = 0; precno < res->pw * res->ph; ++precno) {
					tcd_precinct_t *prc = &band->precincts[precno];
					int32_t cblkno;
					int32_t bandOdd = band->bandno & 1;
					int32_t bandModTwo = band->bandno & 2;

					for (cblkno = 0; cblkno < (int32_t)(prc->cw * prc->ch); ++cblkno) {
						tcd_cblk_enc_t* cblk = prc->cblks.enc + cblkno;
						int32_t x = cblk->x0 - band->x0;
						int32_t y = cblk->y0 - band->y0;
						if (bandOdd) {
							tcd_resolution_t *pres = &tilec->resolutions[resno - 1];
							x += pres->x1 - pres->x0;
						}
						if (bandModTwo) {
							tcd_resolution_t *pres = &tilec->resolutions[resno - 1];
							y += pres->y1 - pres->y0;
						}

						maxCblkW = std::max<int32_t>(maxCblkW, 1 << tccp->cblkw);
						maxCblkH = std::max<int32_t>(maxCblkH, 1 << tccp->cblkh);
						auto block = new encodeBlockInfo();
						block->compno = compno;
						block->bandno = band->bandno;
						block->cblk = cblk;
						block->cblksty = tccp->cblksty;
						block->qmfbid = tccp->qmfbid;
						block->resno = resno;
						block->bandconst = bandconst;
						block->stepsize = band->stepsize;
						block->x = x;
						block->y = y;
						block->mct_norms = mct_norms;
						block->mct_numcomps = mct_numcomps;
						block->tiledp = tile_buf_get_ptr(tilec->buf, resno, bandno, (uint32_t)x, (uint32_t)y);
						blocks.push_back(block);

					} 
				} 
			} 
		} 
	} 

	T1Encoder encoder(do_opt,
					maxCblkW,
					maxCblkH,
					numThreads);
	return encoder.encode(tile,&blocks);
}


bool Tier1::prepareDecodeCodeblocks(tcd_tilecomp_t* tilec,
											tccp_t* tccp,
											std::vector<decodeBlockInfo*>* blocks,
											event_mgr_t * p_manager) {
	return t1_prepare_decode_cblks(tilec, tccp, blocks, p_manager);
}


bool Tier1::decodeCodeblocks(uint16_t blockw, uint16_t blockh, std::vector<decodeBlockInfo*>* blocks, int32_t numThreads) {
	T1Decoder decoder(blockw, blockh);
	return decoder.decode(blocks, numThreads);
}



}