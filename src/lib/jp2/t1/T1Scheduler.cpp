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

namespace grk {

void T1Scheduler::scheduleEncode(TileCodingParams *tcp,
							grk_tile *tile,
							const double *mct_norms,
							uint32_t mct_numcomps,
							bool doRateControl) {

	uint32_t compno, resno, bandno;
	uint64_t precno;
	tile->distotile = 0;
	std::vector<CompressBlockExec*> blocks;
	uint32_t maxCblkW = 0;
	uint32_t maxCblkH = 0;

	for (compno = 0; compno < tile->numcomps; ++compno) {
		auto tilec = tile->comps + compno;
		auto tccp = tcp->tccps + compno;
		for (resno = 0; resno < tilec->numresolutions; ++resno) {
			auto res = &tilec->resolutions[resno];
			for (bandno = 0; bandno < res->numBandWindows; ++bandno) {
				auto band = &res->bandWindow[bandno];
				for (precno = 0; precno < (uint64_t)res->pw * res->ph; ++precno) {
					auto prc = &band->precincts[precno];
					for (uint64_t cblkno = 0; cblkno < (int64_t) prc->cw * prc->ch;
							++cblkno) {
						auto cblk = prc->enc + cblkno;
						auto block = new CompressBlockExec();
						block->x = cblk->x0;
						block->y = cblk->y0;
						block->tiledp = tilec->getBuffer()->cblk_ptr( resno, bandno,
								block->x, block->y);
						maxCblkW = std::max<uint32_t>(maxCblkW,
								(uint32_t) (1 << tccp->cblkw));
						maxCblkH = std::max<uint32_t>(maxCblkH,
								(uint32_t) (1 << tccp->cblkh));
						block->compno = compno;
						block->band_orientation = band->orientation;
						block->cblk = cblk;
						block->cblk_sty = tccp->cblk_sty;
						block->qmfbid = tccp->qmfbid;
						block->resno = resno;
						block->inv_step = (int32_t)band->inv_step;
						block->inv_step_ht = 1.0f/band->stepsize;
						block->stepsize = band->stepsize;
						block->mct_norms = mct_norms;
						block->mct_numcomps = mct_numcomps;
						block->k_msbs = (uint8_t)(band->numbps - cblk->numbps);
						blocks.push_back(block);

					}
				}
			}
		}
	}
	T1CompressScheduler compressor(tcp, tile, maxCblkW, maxCblkH, doRateControl);
	compressor.compress(&blocks);
}

bool T1Scheduler::prepareScheduleDecode(TileComponent *tilec, TileComponentCodingParams *tccp,
		std::vector<DecompressBlockExec*> *blocks) {
	if (!tilec->getBuffer()->alloc()) {
		GRK_ERROR( "Not enough memory for tile data");
		return false;
	}
	for (uint32_t resno = 0; resno < tilec->resolutions_to_decompress; ++resno) {
		auto res = &tilec->resolutions[resno];
		for (uint32_t bandno = 0; bandno < res->numBandWindows; ++bandno) {
			Subband *GRK_RESTRICT band = res->bandWindow + bandno;
			for (uint64_t precno = 0; precno < (uint64_t)res->pw * res->ph; ++precno) {
				auto precinct = band->precincts + precno;
				if (!tilec->subbandIntersectsAOI(resno,
												bandno,
												precinct)){

					continue;
				}
				for (uint64_t cblkno = 0;
						cblkno < (uint64_t) precinct->cw * precinct->ch;
						++cblkno) {
					auto cblk = precinct->dec + cblkno;
					if (tilec->subbandIntersectsAOI(resno,
													bandno,
													cblk)){

						auto block = new DecompressBlockExec();
						block->sparseBuffer = tilec->getSparseBuffer();
						block->x = cblk->x0;
						block->y = cblk->y0;
						block->tiledp = tilec->getBuffer()->cblk_ptr( resno, bandno,
								block->x, block->y);
						block->stride = tilec->getBuffer()->stride(resno,bandno);
						block->band_orientation = band->orientation;
						block->cblk = cblk;
						block->cblk_sty = tccp->cblk_sty;
						block->qmfbid = tccp->qmfbid;
						block->resno = resno;
						block->roishift = tccp->roishift;
						block->stepsize = band->stepsize;

						block->k_msbs = (uint8_t)(band->numbps - cblk->numbps);
						blocks->push_back(block);
					}

				}
			}
		}
	}
	return true;
}


bool T1Scheduler::scheduleDecode(TileCodingParams *tcp,
		                    uint16_t blockw, uint16_t blockh,
		                    std::vector<DecompressBlockExec*> *blocks) {
	T1DecompressScheduler decompressor(tcp, blockw, blockh);
	return decompressor.decompress(blocks);
}

}
