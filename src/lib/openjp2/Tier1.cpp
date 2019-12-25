/*
 *    Copyright (C) 2016-2019 Grok Image Compression Inc.
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
#include "T1Decoder.h"
#include "T1Encoder.h"

namespace grk {

bool Tier1::encodeCodeblocks(grk_tcp *tcp, grk_tcd_tile *tile,
		const double *mct_norms, uint32_t mct_numcomps,	bool doRateControl) {

	uint32_t compno, resno, bandno, precno;
	tile->distotile = 0;
	std::vector<encodeBlockInfo*> blocks;
	uint16_t maxCblkW = 0;
	uint16_t maxCblkH = 0;

	for (compno = 0; compno < tile->numcomps; ++compno) {
		grk_tcd_tilecomp *tilec = &tile->comps[compno];
		grk_tccp *tccp = &tcp->tccps[compno];
		for (resno = 0; resno < tilec->numresolutions; ++resno) {
			grk_tcd_resolution *res = &tilec->resolutions[resno];

			for (bandno = 0; bandno < res->numbands; ++bandno) {
				grk_tcd_band *restrict band = &res->bands[bandno];
				int32_t bandconst = 8192 * 8192
						/ ((int32_t) floor(band->stepsize * 8192));

				for (precno = 0; precno < res->pw * res->ph; ++precno) {
					grk_tcd_precinct *prc = &band->precincts[precno];
					int32_t cblkno;
					int32_t bandOdd = band->bandno & 1;
					int32_t bandModTwo = band->bandno & 2;

					for (cblkno = 0; cblkno < (int32_t) (prc->cw * prc->ch);
							++cblkno) {
						grk_tcd_cblk_enc *cblk = prc->cblks.enc + cblkno;
						int32_t x = cblk->x0 - band->x0;
						int32_t y = cblk->y0 - band->y0;
						if (bandOdd) {
							grk_tcd_resolution *pres = &tilec->resolutions[resno
									- 1];
							x += pres->x1 - pres->x0;
						}
						if (bandModTwo) {
							grk_tcd_resolution *pres = &tilec->resolutions[resno
									- 1];
							y += pres->y1 - pres->y0;
						}

						maxCblkW = std::max<int16_t>(maxCblkW,
								(uint16_t) (1 << tccp->cblkw));
						maxCblkH = std::max<int16_t>(maxCblkH,
								(uint16_t) (1 << tccp->cblkh));
						auto block = new encodeBlockInfo();
						block->compno = compno;
						block->bandno = band->bandno;
						block->cblk = cblk;
						block->cblk_sty = tccp->cblk_sty;
						block->qmfbid = tccp->qmfbid;
						block->resno = resno;
						block->bandconst = bandconst;
						block->stepsize = band->stepsize;
						block->x = x;
						block->y = y;
						block->mct_norms = mct_norms;
						block->mct_numcomps = mct_numcomps;
						block->tiledp = tile_buf_get_ptr(tilec->buf, resno,
								bandno, (uint32_t) x, (uint32_t) y);
						blocks.push_back(block);

					}
				}
			}
		}
	}

	T1Encoder encoder(tcp, tile, maxCblkW, maxCblkH, doRateControl);
	return encoder.encode(&blocks);
}

bool Tier1::prepareDecodeCodeblocks(grk_tcd_tilecomp *tilec, grk_tccp *tccp,
		std::vector<decodeBlockInfo*> *blocks) {
	uint32_t resno, bandno, precno;
	if (!tile_buf_alloc_component_data_decode(tilec->buf)) {
		GROK_ERROR( "Not enough memory for tile data");
		return false;
	}

	for (resno = 0; resno < tilec->minimum_num_resolutions; ++resno) {
		grk_tcd_resolution *res = &tilec->resolutions[resno];

		for (bandno = 0; bandno < res->numbands; ++bandno) {
			grk_tcd_band *restrict band = &res->bands[bandno];

			for (precno = 0; precno < res->pw * res->ph; ++precno) {
				grk_tcd_precinct *precinct = &band->precincts[precno];
				int32_t cblkno;
				for (cblkno = 0;
						cblkno < (int32_t) (precinct->cw * precinct->ch);
						++cblkno) {
					grk_rect cblk_rect;
					grk_tcd_cblk_dec *cblk = &precinct->cblks.dec[cblkno];
					int32_t x, y; /* relative code block offset */
					/* get code block offset relative to band*/
					x = cblk->x0;
					y = cblk->y0;

					/* check if block overlaps with decode region */
					cblk_rect = grk_rect(x, y, x + (1 << tccp->cblkw),
							y + (1 << tccp->cblkh));
					if (!tile_buf_hit_test(tilec->buf, &cblk_rect))
						continue;

					x -= band->x0;
					y -= band->y0;

					/* add band offset relative to previous resolution */
					if (band->bandno & 1) {
						grk_tcd_resolution *pres = &tilec->resolutions[resno - 1];
						x += pres->x1 - pres->x0;
					}
					if (band->bandno & 2) {
						grk_tcd_resolution *pres = &tilec->resolutions[resno - 1];
						y += pres->y1 - pres->y0;
					}

					auto block = new decodeBlockInfo();
					block->bandno = band->bandno;
					block->cblk = cblk;
					block->cblk_sty = tccp->cblk_sty;
					block->qmfbid = tccp->qmfbid;
					block->resno = resno;
					block->roishift = tccp->roishift;
					block->stepsize = band->stepsize;
					block->tilec = tilec;
					block->x = x;
					block->y = y;
					block->tiledp = tile_buf_get_ptr(tilec->buf, resno, bandno,
							(uint32_t) x, (uint32_t) y);
					blocks->push_back(block);

				}
			}
		}
	}
	return true;
}

bool Tier1::decodeCodeblocks(grk_tcp *tcp, uint16_t blockw, uint16_t blockh,
		std::vector<decodeBlockInfo*> *blocks) {
	T1Decoder decoder(tcp, blockw, blockh);
	return decoder.decode(blocks);
}

}
