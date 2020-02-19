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
 *
 *    This source code incorporates work covered by the following copyright and
 *    permission notice:
 *
 * The copyright in this software is being made available under the 2-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2002-2014, Universite catholique de Louvain (UCL), Belgium
 * Copyright (c) 2002-2014, Professor Benoit Macq
 * Copyright (c) 2001-2003, David Janssens
 * Copyright (c) 2002-2003, Yannick Verschueren
 * Copyright (c) 2003-2007, Francois-Olivier Devaux
 * Copyright (c) 2003-2014, Antonin Descampe
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2006-2007, Parvatha Elangovan
 * Copyright (c) 2008, 2011-2012, Centre National d'Etudes Spatiales (CNES), FR
 * Copyright (c) 2012, CS Systemes d'Information, France
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "grok_includes.h"
#include "Tier1.h"
#include <memory>
#include "WaveletForward.h"
#include <algorithm>
using namespace std;

namespace grk {



/*
 if
 - r xx, yy, zz, 0   (disto_alloc == 1 and rates == 0)
 or
 - q xx, yy, zz, 0   (fixed_quality == 1 and distoratio == 0)

 then don't try to find an optimal threshold but rather take everything not included yet.

 It is possible to have some lossy layers and the last layer for sure lossless

 */
bool TileProcessor::layer_needs_rate_control(uint32_t layno) {

	auto enc_params = &cp->m_coding_param.m_enc;
	return ((enc_params->m_disto_alloc == 1) && (tcp->rates[layno] > 0.0))
			|| ((enc_params->m_fixed_quality == 1)
					&& (tcp->distoratio[layno] > 0.0f));
}

bool TileProcessor::needs_rate_control() {
	for (uint32_t i = 0; i < tcp->numlayers; ++i) {
		if (layer_needs_rate_control(i))
			return true;
	}
	return false;
}

// lossless in the sense that no code passes are removed; it mays still be a lossless layer
// due to irreversible DWT and quantization
bool TileProcessor::make_single_lossless_layer() {
	if (tcp->numlayers == 1
			&& !layer_needs_rate_control(0)) {
		makelayer_final(0);
		return true;
	}
	return false;
}

void TileProcessor::makelayer_feasible(uint32_t layno, uint16_t thresh,
		bool final) {
	uint32_t compno, resno, bandno, precno, cblkno;
	uint32_t passno;
	auto tcd_tile = tile;
	tcd_tile->distolayer[layno] = 0;
	for (compno = 0; compno < tcd_tile->numcomps; compno++) {
		auto tilec = tcd_tile->comps + compno;
		for (resno = 0; resno < tilec->numresolutions; resno++) {
			auto res = tilec->resolutions + resno;
			for (bandno = 0; bandno < res->numbands; bandno++) {
				auto band = res->bands + bandno;

				for (precno = 0; precno < res->pw * res->ph; precno++) {
					auto prc = band->precincts + precno;

					for (cblkno = 0; cblkno < prc->cw * prc->ch; cblkno++) {
						auto cblk = prc->cblks.enc + cblkno;
						auto layer = cblk->layers + layno;
						uint32_t cumulative_included_passes_in_block;

						if (layno == 0) {
							cblk->num_passes_included_in_previous_layers = 0;
						}

						cumulative_included_passes_in_block =
								cblk->num_passes_included_in_previous_layers;

						for (passno =
								cblk->num_passes_included_in_previous_layers;
								passno < cblk->num_passes_encoded; passno++) {
							auto pass = &cblk->passes[passno];

							//truncate or include feasible, otherwise ignore
							if (pass->slope) {
								if (pass->slope <= thresh)
									break;
								cumulative_included_passes_in_block = passno
										+ 1;
							}
						}

						layer->numpasses = cumulative_included_passes_in_block
								- cblk->num_passes_included_in_previous_layers;

						if (!layer->numpasses) {
							layer->disto = 0;
							continue;
						}

						// update layer
						if (cblk->num_passes_included_in_previous_layers == 0) {
							layer->len =
									cblk->passes[cumulative_included_passes_in_block
											- 1].rate;
							layer->data = cblk->data;
							layer->disto =
									cblk->passes[cumulative_included_passes_in_block
											- 1].distortiondec;
						} else {
							layer->len =
									cblk->passes[cumulative_included_passes_in_block
											- 1].rate
											- cblk->passes[cblk->num_passes_included_in_previous_layers
													- 1].rate;
							layer->data =
									cblk->data
											+ cblk->passes[cblk->num_passes_included_in_previous_layers
													- 1].rate;
							layer->disto =
									cblk->passes[cumulative_included_passes_in_block
											- 1].distortiondec
											- cblk->passes[cblk->num_passes_included_in_previous_layers
													- 1].distortiondec;
						}

						tcd_tile->distolayer[layno] += layer->disto;
						if (final)
							cblk->num_passes_included_in_previous_layers =
									cumulative_included_passes_in_block;
					}
				}
			}
		}
	}
}

/*
 Hybrid rate control using bisect algorithm with optimal truncation points
 */
bool TileProcessor::pcrd_bisect_feasible(uint64_t *p_data_written,
		uint64_t len) {

	bool single_lossless = make_single_lossless_layer();
	double cumdisto[100];
	const double K = 1;
	double maxSE = 0;

	auto tcd_tile = tile;
	auto tcd_tcp = tcp;

	tcd_tile->numpix = 0;
	uint32_t state = grok_plugin_get_debug_state();

	RateInfo rateInfo;
	for (uint32_t compno = 0; compno < tcd_tile->numcomps; compno++) {
		auto tilec = &tcd_tile->comps[compno];
		tilec->numpix = 0;
		for (uint32_t resno = 0; resno < tilec->numresolutions; resno++) {
			auto res = &tilec->resolutions[resno];

			for (uint32_t bandno = 0; bandno < res->numbands; bandno++) {
				auto band = &res->bands[bandno];

				for (uint32_t precno = 0; precno < res->pw * res->ph;
						precno++) {
					auto prc = &band->precincts[precno];
					for (uint32_t cblkno = 0; cblkno < prc->cw * prc->ch;
							cblkno++) {
						auto cblk = &prc->cblks.enc[cblkno];
						uint32_t numPix = ((cblk->x1 - cblk->x0)
								* (cblk->y1 - cblk->y0));
						if (!(state & GROK_PLUGIN_STATE_PRE_TR1)) {
							encode_synch_with_plugin(this,compno, resno, bandno,
									precno, cblkno, band, cblk, &numPix);
						}

						if (!single_lossless) {
							RateControl::convexHull(cblk->passes,
									cblk->num_passes_encoded);
							rateInfo.synch(cblk);

							tcd_tile->numpix += numPix;
							tilec->numpix += numPix;
						}
					} /* cbklno */
				} /* precno */
			} /* bandno */
		} /* resno */

		if (!single_lossless) {
			maxSE += (double) (((uint64_t) 1 << image->comps[compno].prec)
					- 1)
					* (double) (((uint64_t) 1 << image->comps[compno].prec)
							- 1) * (double) tilec->numpix;
		}
	} /* compno */

	if (single_lossless) {
		makelayer_final( 0);
		return true;
	}

	uint32_t min_slope = rateInfo.getMinimumThresh();
	uint32_t max_slope = USHRT_MAX;

	uint32_t upperBound = max_slope;
	for (uint32_t layno = 0; layno < tcd_tcp->numlayers; layno++) {
		uint32_t lowerBound = min_slope;
		uint64_t maxlen =
				tcd_tcp->rates[layno] > 0.0f ?
						std::min<uint64_t>(
								((uint64_t) ceil(tcd_tcp->rates[layno])), len) :
						len;

		/* Threshold for Marcela Index */
		// start by including everything in this layer
		uint32_t goodthresh = 0;
		// thresh from previous iteration - starts off uninitialized
		// used to bail out if difference with current thresh is small enough
		uint32_t prevthresh = 0;
		if (layer_needs_rate_control(layno)) {
			auto t2 = new T2(image, cp);
			double distotarget = tcd_tile->distotile
					- ((K * maxSE)
							/ pow(10.0, tcd_tcp->distoratio[layno] / 10.0));

			for (uint32_t i = 0; i < 128; ++i) {
				uint32_t thresh = (lowerBound + upperBound) >> 1;
				if (prevthresh != 0 && prevthresh == thresh)
					break;
				makelayer_feasible(layno, (uint16_t) thresh, false);
				prevthresh = thresh;
				if (cp->m_coding_param.m_enc.m_fixed_quality) {
					double distoachieved =
							layno == 0 ?
									tcd_tile->distolayer[0] :
									cumdisto[layno - 1]
											+ tcd_tile->distolayer[layno];

					if (distoachieved < distotarget) {
						upperBound = thresh;
						continue;
					}
					lowerBound = thresh;
				} else {
					if (!t2->encode_packets_simulate(tcd_tileno,
							tcd_tile, layno + 1, p_data_written, maxlen,
							tp_pos)) {
						lowerBound = thresh;
						continue;
					}
					upperBound = thresh;
				}
			}
			// choose conservative value for goodthresh
			goodthresh = upperBound;
			delete t2;

			makelayer_feasible(layno, (uint16_t) goodthresh, true);
			cumdisto[layno] =
					(layno == 0) ?
							tcd_tile->distolayer[0] :
							(cumdisto[layno - 1] + tcd_tile->distolayer[layno]);
			// upper bound for next layer is initialized to lowerBound for current layer, minus one
			upperBound = lowerBound - 1;
			;
		} else {
			makelayer_final(layno);
		}
	}
	return true;
}

/*
 Simple bisect algorithm to calculate optimal layer truncation points
 */
bool TileProcessor::pcrd_bisect_simple(uint64_t *p_data_written, uint64_t len) {
	uint32_t compno, resno, bandno, precno, cblkno, layno;
	uint32_t passno;
	double cumdisto[100];
	const double K = 1;
	double maxSE = 0;

	grk_tcd_tile *tcd_tile = tile;
	grk_tcp *tcd_tcp = tcp;

	double min_slope = DBL_MAX;
	double max_slope = -1;

	tcd_tile->numpix = 0;
	uint32_t state = grok_plugin_get_debug_state();

	bool single_lossless = make_single_lossless_layer();

	for (compno = 0; compno < tcd_tile->numcomps; compno++) {
		auto tilec = &tcd_tile->comps[compno];
		tilec->numpix = 0;
		for (resno = 0; resno < tilec->numresolutions; resno++) {
			auto res = &tilec->resolutions[resno];
			for (bandno = 0; bandno < res->numbands; bandno++) {
				auto band = &res->bands[bandno];
				for (precno = 0; precno < res->pw * res->ph; precno++) {
					auto prc = &band->precincts[precno];
					for (cblkno = 0; cblkno < prc->cw * prc->ch; cblkno++) {
						auto cblk = &prc->cblks.enc[cblkno];
						uint32_t numPix = ((cblk->x1 - cblk->x0)
								* (cblk->y1 - cblk->y0));
						if (!(state & GROK_PLUGIN_STATE_PRE_TR1)) {
							encode_synch_with_plugin(this,compno, resno, bandno,
									precno, cblkno, band, cblk, &numPix);
						}

						if (!single_lossless) {
							for (passno = 0; passno < cblk->num_passes_encoded;
									passno++) {
								grk_tcd_pass *pass = &cblk->passes[passno];
								int32_t dr;
								double dd, rdslope;

								if (passno == 0) {
									dr = (int32_t) pass->rate;
									dd = pass->distortiondec;
								} else {
									dr = (int32_t) (pass->rate
											- cblk->passes[passno - 1].rate);
									dd =
											pass->distortiondec
													- cblk->passes[passno - 1].distortiondec;
								}

								if (dr == 0) {
									continue;
								}

								rdslope = dd / dr;
								if (rdslope < min_slope) {
									min_slope = rdslope;
								}

								if (rdslope > max_slope) {
									max_slope = rdslope;
								}
							} /* passno */
							tcd_tile->numpix += numPix;
							tilec->numpix += numPix;
						}

					} /* cbklno */
				} /* precno */
			} /* bandno */
		} /* resno */

		if (!single_lossless)
			maxSE += (double) (((uint64_t) 1 << image->comps[compno].prec)
					- 1)
					* (double) (((uint64_t) 1 << image->comps[compno].prec)
							- 1) * (double) tilec->numpix;

	} /* compno */

	if (single_lossless) {
		return true;
	}

	double upperBound = max_slope;
	for (layno = 0; layno < tcd_tcp->numlayers; layno++) {
		if (layer_needs_rate_control(layno)) {
			double lowerBound = min_slope;
			uint64_t maxlen =
					tcd_tcp->rates[layno] > 0.0f ?
							std::min<uint64_t>(
									((uint64_t) ceil(tcd_tcp->rates[layno])),
									len) :
							len;

			/* Threshold for Marcela Index */
			// start by including everything in this layer
			double goodthresh = 0;

			// thresh from previous iteration - starts off uninitialized
			// used to bail out if difference with current thresh is small enough
			double prevthresh = -1;
			double distotarget = tcd_tile->distotile
					- ((K * maxSE)
							/ pow(10.0, tcd_tcp->distoratio[layno] / 10.0));

			auto t2 = new T2(image, cp);
			double thresh;
			for (uint32_t i = 0; i < 128; ++i) {
				thresh =
						(upperBound == -1) ?
								lowerBound : (lowerBound + upperBound) / 2;
				make_layer_simple(layno, thresh, false);
				if (prevthresh != -1 && (fabs(prevthresh - thresh)) < 0.001)
					break;
				prevthresh = thresh;
				if (cp->m_coding_param.m_enc.m_fixed_quality) {
					double distoachieved =
							layno == 0 ?
									tcd_tile->distolayer[0] :
									cumdisto[layno - 1]
											+ tcd_tile->distolayer[layno];

					if (distoachieved < distotarget) {
						upperBound = thresh;
						continue;
					}
					lowerBound = thresh;
				} else {
					if (!t2->encode_packets_simulate(tcd_tileno,
							tcd_tile, layno + 1, p_data_written, maxlen,
							tp_pos)) {
						lowerBound = thresh;
						continue;
					}
					upperBound = thresh;
				}
			}
			// choose conservative value for goodthresh
			goodthresh = (upperBound == -1) ? thresh : upperBound;
			delete t2;

			make_layer_simple(layno, goodthresh, true);
			cumdisto[layno] =
					(layno == 0) ?
							tcd_tile->distolayer[0] :
							(cumdisto[layno - 1] + tcd_tile->distolayer[layno]);

			// upper bound for next layer will equal lowerBound for previous layer, minus one
			upperBound = lowerBound - 1;
		} else {
			makelayer_final(layno);
			// this has to be the last layer, so return
			assert(layno == tcd_tcp->numlayers - 1);
			return true;
		}
	}
	return true;
}
static void prepareBlockForFirstLayer(grk_tcd_cblk_enc *cblk) {
	cblk->num_passes_included_in_previous_layers = 0;
	cblk->num_passes_included_in_current_layer = 0;
	cblk->numlenbits = 0;
}

/*
 Form layer for bisect rate control algorithm
 */
void TileProcessor::make_layer_simple(uint32_t layno, double thresh,
		bool final) {
	uint32_t compno, resno, bandno, precno, cblkno;
	uint32_t passno;
	grk_tcd_tile *tcd_tile = tile;
	tcd_tile->distolayer[layno] = 0;
	for (compno = 0; compno < tcd_tile->numcomps; compno++) {
		auto tilec = tcd_tile->comps + compno;
		for (resno = 0; resno < tilec->numresolutions; resno++) {
			auto res = tilec->resolutions + resno;
			for (bandno = 0; bandno < res->numbands; bandno++) {
				auto band = res->bands + bandno;
				for (precno = 0; precno < res->pw * res->ph; precno++) {
					auto prc = band->precincts + precno;
					for (cblkno = 0; cblkno < prc->cw * prc->ch; cblkno++) {
						auto cblk = prc->cblks.enc + cblkno;
						auto layer = cblk->layers + layno;
						uint32_t cumulative_included_passes_in_block;
						if (layno == 0) {
							prepareBlockForFirstLayer(cblk);
						}
						if (thresh == 0) {
							cumulative_included_passes_in_block =
									cblk->num_passes_encoded;
						} else {
							cumulative_included_passes_in_block =
									cblk->num_passes_included_in_previous_layers;
							for (passno =
									cblk->num_passes_included_in_previous_layers;
									passno < cblk->num_passes_encoded;
									passno++) {
								uint32_t dr;
								double dd;
								grk_tcd_pass *pass = &cblk->passes[passno];
								if (cumulative_included_passes_in_block == 0) {
									dr = pass->rate;
									dd = pass->distortiondec;
								} else {
									dr =
											pass->rate
													- cblk->passes[cumulative_included_passes_in_block
															- 1].rate;
									dd =
											pass->distortiondec
													- cblk->passes[cumulative_included_passes_in_block
															- 1].distortiondec;
								}

								if (!dr) {
									if (dd != 0)
										cumulative_included_passes_in_block =
												passno + 1;
									continue;
								}
								auto slope = dd / dr;
								/* do not rely on float equality, check with DBL_EPSILON margin */
								if (thresh - slope < DBL_EPSILON)
									cumulative_included_passes_in_block = passno
											+ 1;
							}
						}

						layer->numpasses = cumulative_included_passes_in_block
								- cblk->num_passes_included_in_previous_layers;
						if (!layer->numpasses) {
							layer->disto = 0;
							continue;
						}

						// update layer
						if (cblk->num_passes_included_in_previous_layers == 0) {
							layer->len =
									cblk->passes[cumulative_included_passes_in_block
											- 1].rate;
							layer->data = cblk->data;
							layer->disto =
									cblk->passes[cumulative_included_passes_in_block
											- 1].distortiondec;
						} else {
							layer->len =
									cblk->passes[cumulative_included_passes_in_block
											- 1].rate
											- cblk->passes[cblk->num_passes_included_in_previous_layers
													- 1].rate;
							layer->data =
									cblk->data
											+ cblk->passes[cblk->num_passes_included_in_previous_layers
													- 1].rate;
							layer->disto =
									cblk->passes[cumulative_included_passes_in_block
											- 1].distortiondec
											- cblk->passes[cblk->num_passes_included_in_previous_layers
													- 1].distortiondec;
						}

						tcd_tile->distolayer[layno] += layer->disto;
						if (final)
							cblk->num_passes_included_in_previous_layers =
									cumulative_included_passes_in_block;
					}
				}
			}
		}
	}
}

// Add all remaining passes to this layer
void TileProcessor::makelayer_final(uint32_t layno) {
	uint32_t compno, resno, bandno, precno, cblkno;
	auto tcd_tile = tile;
	tcd_tile->distolayer[layno] = 0;

	for (compno = 0; compno < tcd_tile->numcomps; compno++) {
		auto tilec = tcd_tile->comps + compno;
		for (resno = 0; resno < tilec->numresolutions; resno++) {
			auto res = tilec->resolutions + resno;
			for (bandno = 0; bandno < res->numbands; bandno++) {
				auto band = res->bands + bandno;
				for (precno = 0; precno < res->pw * res->ph; precno++) {
					auto prc = band->precincts + precno;
					for (cblkno = 0; cblkno < prc->cw * prc->ch; cblkno++) {
						auto cblk = prc->cblks.enc + cblkno;
						auto layer = cblk->layers + layno;
						if (layno == 0) {
							prepareBlockForFirstLayer(cblk);
						}
						uint32_t cumulative_included_passes_in_block =
								cblk->num_passes_included_in_previous_layers;
						if (cblk->num_passes_encoded
								> cblk->num_passes_included_in_previous_layers)
							cumulative_included_passes_in_block =
									cblk->num_passes_encoded;

						layer->numpasses = cumulative_included_passes_in_block
								- cblk->num_passes_included_in_previous_layers;

						if (!layer->numpasses) {
							layer->disto = 0;
							continue;
						}

						// update layer
						if (cblk->num_passes_included_in_previous_layers == 0) {
							layer->len =
									cblk->passes[cumulative_included_passes_in_block
											- 1].rate;
							layer->data = cblk->data;
							layer->disto =
									cblk->passes[cumulative_included_passes_in_block
											- 1].distortiondec;
						} else {
							layer->len =
									cblk->passes[cumulative_included_passes_in_block
											- 1].rate
											- cblk->passes[cblk->num_passes_included_in_previous_layers
													- 1].rate;
							layer->data =
									cblk->data
											+ cblk->passes[cblk->num_passes_included_in_previous_layers
													- 1].rate;
							layer->disto =
									cblk->passes[cumulative_included_passes_in_block
											- 1].distortiondec
											- cblk->passes[cblk->num_passes_included_in_previous_layers
													- 1].distortiondec;
						}
						tcd_tile->distolayer[layno] += layer->disto;
						cblk->num_passes_included_in_previous_layers =
								cumulative_included_passes_in_block;
						assert(
								cblk->num_passes_included_in_previous_layers
										== cblk->num_passes_encoded);
					}
				}
			}
		}
	}
}
bool TileProcessor::init(grk_image *p_image, grk_coding_parameters *p_cp) {
	this->image = p_image;
	this->cp = p_cp;

	tile = (grk_tcd_tile*) grok_calloc(1, sizeof(grk_tcd_tile));
	if (!tile) {
		return false;
	}

	tile->comps = (TileComponent*) grok_calloc(p_image->numcomps,
			sizeof(TileComponent));
	if (!tile->comps) {
		return false;
	}

	tile->numcomps = p_image->numcomps;
	tp_pos = p_cp->m_coding_param.m_enc.m_tp_pos;
	return true;
}


uint32_t TileComponent::width(){
	return (uint32_t) (x1 - x0);
}
uint32_t TileComponent::height(){
	return (uint32_t) (y1 - y0);
}
uint64_t TileComponent::size(){
	return  area() * sizeof(int32_t);
}
uint64_t TileComponent::area(){
	return (uint64_t)width() * height() ;
}
void TileComponent::finalizeCoordinates(bool isEncoder){
	auto highestRes =
			(!isEncoder) ?
								minimum_num_resolutions : numresolutions;
	auto res =  resolutions + highestRes - 1;
	x0 = res->x0;
	x1 = res->x1;
	y0 = res->y0;
	y1 = res->y1;

	res = resolutions + numresolutions - 1;
	unreduced_tile_dim = grk_rect(res->x0, res->y0, res->x1, res->y1);

}
/* ----------------------------------------------------------------------- */

inline bool TileProcessor::init_tile(uint16_t tile_no,
		grk_image *output_image, bool isEncoder, float fraction,
		size_t sizeof_block) {
	uint32_t (*l_gain_ptr)(uint8_t) = nullptr;
	uint32_t compno, resno, bandno, precno;
	uint32_t p, q;
	uint32_t l_level_no;
	uint32_t l_pdx, l_pdy;
	uint32_t l_gain;
	uint32_t l_x0b, l_y0b;
	uint32_t l_tx0, l_ty0;
	/* extent of precincts , top left, bottom right**/
	uint32_t l_tl_prc_x_start, l_tl_prc_y_start, l_br_prc_x_end, l_br_prc_y_end;
	/* number of precinct for a resolution */
	uint32_t l_nb_precincts;
	/* number of code blocks for a precinct*/
	uint64_t l_nb_code_blocks, cblkno;
	/* room needed to store l_nb_code_blocks code blocks for a precinct*/
	uint64_t l_nb_code_blocks_size;

	uint32_t state = grok_plugin_get_debug_state();

	auto l_cp = cp;
	auto l_tcp = &(l_cp->tcps[tile_no]);
	auto l_tile = tile;
	auto l_tccp = l_tcp->tccps;
	auto l_tilec = l_tile->comps;
	auto l_image = image;
	auto l_image_comp = image->comps;

	if (l_tcp->m_tile_data)
		l_tcp->m_tile_data->rewind();

	p = tile_no % l_cp->tw; /* tile coordinates */
	q = tile_no / l_cp->tw;
	/*fprintf(stderr, "Tile coordinate = %d,%d\n", p, q);*/

	/* 4 borders of the tile rescale on the image if necessary */
	l_tx0 = l_cp->tx0 + p * l_cp->tdx; /* can't be greater than l_image->x1 so won't overflow */
	l_tile->x0 = std::max<uint32_t>(l_tx0, l_image->x0);
	l_tile->x1 = std::min<uint32_t>(uint_adds(l_tx0, l_cp->tdx), l_image->x1);
	if (l_tile->x1 <= l_tile->x0) {
		GROK_ERROR( "Tile x coordinates are not valid");
		return false;
	}
	l_ty0 = l_cp->ty0 + q * l_cp->tdy; /* can't be greater than l_image->y1 so won't overflow */
	l_tile->y0 = std::max<uint32_t>(l_ty0, l_image->y0);
	l_tile->y1 = std::min<uint32_t>(uint_adds(l_ty0, l_cp->tdy), l_image->y1);
	if (l_tile->y1 <= l_tile->y0) {
		GROK_ERROR( "Tile y coordinates are not valid");
		return false;
	}

	/* testcase 1888.pdf.asan.35.988 */
	if (l_tccp->numresolutions == 0) {
		GROK_ERROR(
				"tiles require at least one resolution");
		return false;
	}
	/*fprintf(stderr, "Tile border = %d,%d,%d,%d\n", l_tile->x0, l_tile->y0,l_tile->x1,l_tile->y1);*/

	for (compno = 0; compno < l_tile->numcomps; ++compno) {
		/*fprintf(stderr, "compno = %d/%d\n", compno, l_tile->numcomps);*/
		if (l_image_comp->dx == 0 || l_image_comp->dy == 0) {
			return false;
		}
		l_image_comp->resno_decoded = 0;
		/* border of each l_tile component in tile coordinates */
		auto x0 = ceildiv<uint32_t>(l_tile->x0, l_image_comp->dx);
		auto y0 = ceildiv<uint32_t>(l_tile->y0, l_image_comp->dy);
		auto x1 = ceildiv<uint32_t>(l_tile->x1, l_image_comp->dx);
		auto y1 = ceildiv<uint32_t>(l_tile->y1, l_image_comp->dy);
		/*fprintf(stderr, "\tTile compo border = %d,%d,%d,%d\n", l_tilec->X0(), l_tilec->Y0(),l_tilec->x1,l_tilec->y1);*/

		uint32_t numresolutions = l_tccp->numresolutions;
		if (numresolutions < l_cp->m_coding_param.m_dec.m_reduce) {
			l_tilec->minimum_num_resolutions = 1;
		} else {
			l_tilec->minimum_num_resolutions = numresolutions
					- l_cp->m_coding_param.m_dec.m_reduce;
		}
		if (!l_tilec->resolutions) {
			l_tilec->resolutions = new grk_tcd_resolution[numresolutions];
			l_tilec->numAllocatedResolutions = numresolutions;
		} else if (numresolutions > l_tilec->numAllocatedResolutions) {
			grk_tcd_resolution *new_resolutions =
					new grk_tcd_resolution[numresolutions];
			for (uint32_t i = 0; i < l_tilec->numresolutions; ++i) {
				new_resolutions[i] = l_tilec->resolutions[i];
			}
			delete[] l_tilec->resolutions;
			l_tilec->resolutions = new_resolutions;
			l_tilec->numAllocatedResolutions = numresolutions;
		}
		l_tilec->numresolutions = numresolutions;
		l_level_no = l_tilec->numresolutions;
		auto l_res = l_tilec->resolutions;
		auto l_step_size = l_tccp->stepsizes;
		if (l_tccp->qmfbid == 0) {
			l_gain_ptr = &dwt_utils::getgain_real;
		} else {
			l_gain_ptr = &dwt_utils::getgain;
		}
		l_tilec->whole_tile_decoding = whole_tile_decoding;
		/*fprintf(stderr, "\tlevel_no=%d\n",l_level_no);*/

		for (resno = 0; resno < l_tilec->numresolutions; ++resno) {
			/*fprintf(stderr, "\t\tresno = %d/%d\n", resno, l_tilec->numresolutions);*/
			uint32_t tlcbgxstart, tlcbgystart;
			uint32_t cbgwidthexpn, cbgheightexpn;
			uint32_t cblkwidthexpn, cblkheightexpn;

			--l_level_no;

			/* border for each resolution level (global) */
			l_res->x0 = uint_ceildivpow2(x0, l_level_no);
			l_res->y0 = uint_ceildivpow2(y0, l_level_no);
			l_res->x1 = uint_ceildivpow2(x1, l_level_no);
			l_res->y1 = uint_ceildivpow2(y1, l_level_no);
			/*fprintf(stderr, "\t\t\tres_x0= %d, res_y0 =%d, res_x1=%d, res_y1=%d\n", l_res->x0, l_res->y0, l_res->x1, l_res->y1);*/
			/* p. 35, table A-23, ISO/IEC FDIS154444-1 : 2000 (18 august 2000) */
			l_pdx = l_tccp->prcw[resno];
			l_pdy = l_tccp->prch[resno];
			/*fprintf(stderr, "\t\t\tpdx=%d, pdy=%d\n", l_pdx, l_pdy);*/
			/* p. 64, B.6, ISO/IEC FDIS15444-1 : 2000 (18 august 2000)  */
			l_tl_prc_x_start = uint_floordivpow2(l_res->x0, l_pdx) << l_pdx;
			l_tl_prc_y_start = uint_floordivpow2(l_res->y0, l_pdy) << l_pdy;
			uint64_t temp = (uint64_t)uint_ceildivpow2(l_res->x1, l_pdx) << l_pdx;
			if (temp > UINT_MAX){
				GROK_ERROR("Resolution x1 value %d must be less than 2^32", temp);
				return false;
			}
			l_br_prc_x_end = (uint32_t)temp;
			temp = (uint64_t)uint_ceildivpow2(l_res->y1, l_pdy) << l_pdy;
			if (temp > UINT_MAX){
				GROK_ERROR("Resolution y1 value %d must be less than 2^32", temp);
				return false;
			}
			l_br_prc_y_end = (uint32_t)temp;

			/*fprintf(stderr, "\t\t\tprc_x_start=%d, prc_y_start=%d, br_prc_x_end=%d, br_prc_y_end=%d \n", l_tl_prc_x_start, l_tl_prc_y_start, l_br_prc_x_end ,l_br_prc_y_end );*/

			l_res->pw =
					(l_res->x0 == l_res->x1) ?
							0 : ((l_br_prc_x_end - l_tl_prc_x_start) >> l_pdx);
			l_res->ph =
					(l_res->y0 == l_res->y1) ?
							0 : ((l_br_prc_y_end - l_tl_prc_y_start) >> l_pdy);
			/*fprintf(stderr, "\t\t\tres_pw=%d, res_ph=%d\n", l_res->pw, l_res->ph );*/

			if (mult_will_overflow(l_res->pw, l_res->ph)) {
				GROK_ERROR(
						"l_nb_precincts calculation would overflow ");
				return false;
			}
			l_nb_precincts = l_res->pw * l_res->ph;

			if (mult_will_overflow(l_nb_precincts,
					(uint32_t) sizeof(grk_tcd_precinct))) {
				GROK_ERROR(
						"l_nb_precinct_size calculation would overflow ");
				return false;
			}
			if (resno == 0) {
				tlcbgxstart = l_tl_prc_x_start;
				tlcbgystart = l_tl_prc_y_start;
				cbgwidthexpn = l_pdx;
				cbgheightexpn = l_pdy;
				l_res->numbands = 1;
			} else {
				tlcbgxstart = uint_ceildivpow2(l_tl_prc_x_start, 1);
				tlcbgystart = uint_ceildivpow2(l_tl_prc_y_start, 1);
				cbgwidthexpn = l_pdx - 1;
				cbgheightexpn = l_pdy - 1;
				l_res->numbands = 3;
			}

			cblkwidthexpn = std::min<uint32_t>(l_tccp->cblkw, cbgwidthexpn);
			cblkheightexpn = std::min<uint32_t>(l_tccp->cblkh, cbgheightexpn);
			size_t nominalBlockSize = (1 << cblkwidthexpn)
					* (1 << cblkheightexpn);
			auto l_band = l_res->bands;

			for (bandno = 0; bandno < l_res->numbands; ++bandno) {
				uint32_t numbps;
				/*fprintf(stderr, "\t\t\tband_no=%d/%d\n", bandno, l_res->numbands );*/

				if (resno == 0) {
					l_band->bandno = 0;
					l_band->x0 = uint_ceildivpow2(x0, l_level_no);
					l_band->y0 = uint_ceildivpow2(y0, l_level_no);
					l_band->x1 = uint_ceildivpow2(x1, l_level_no);
					l_band->y1 = uint_ceildivpow2(y1, l_level_no);
				} else {
					l_band->bandno = bandno + 1;
					/* x0b = 1 if bandno = 1 or 3 */
					l_x0b = l_band->bandno & 1;
					/* y0b = 1 if bandno = 2 or 3 */
					l_y0b = (uint32_t) ((l_band->bandno) >> 1);
					/* l_band border (global) */
					l_band->x0 = uint64_ceildivpow2(
							x0 - ((uint64_t) l_x0b << l_level_no),
							l_level_no + 1);
					l_band->y0 = uint64_ceildivpow2(
							y0 - ((uint64_t) l_y0b << l_level_no),
							l_level_no + 1);
					l_band->x1 = uint64_ceildivpow2(
							x1 - ((uint64_t) l_x0b << l_level_no),
							l_level_no + 1);
					l_band->y1 = uint64_ceildivpow2(
							y1 - ((uint64_t) l_y0b << l_level_no),
							l_level_no + 1);
				}

				/** avoid an if with storing function pointer */
				l_gain = (*l_gain_ptr)((uint8_t) l_band->bandno);
				numbps = l_image_comp->prec + l_gain;
				l_band->stepsize = (float) (((1.0 + l_step_size->mant / 2048.0)
						* pow(2.0, (int32_t) (numbps - l_step_size->expn))))
						* fraction;

				// see Taubman + Marcellin - Equation 10.22
				l_band->numbps = l_tccp->roishift
						+ std::max<int32_t>(0,
								(int32_t) (l_step_size->expn + l_tccp->numgbits)
										- 1);

				if (!l_band->precincts && (l_nb_precincts > 0U)) {
					l_band->precincts = new grk_tcd_precinct[l_nb_precincts];
					l_band->numAllocatedPrecincts = l_nb_precincts;
				} else if (l_band->numAllocatedPrecincts < l_nb_precincts) {
					grk_tcd_precinct *new_precincts =
							new grk_tcd_precinct[l_nb_precincts];
					for (size_t i = 0; i < l_band->numAllocatedPrecincts; ++i) {
						new_precincts[i] = l_band->precincts[i];
					}
					if (l_band->precincts)
						delete[] l_band->precincts;
					l_band->precincts = new_precincts;
					l_band->numAllocatedPrecincts = l_nb_precincts;
				}
				l_band->numPrecincts = l_nb_precincts;
				auto l_current_precinct = l_band->precincts;
				for (precno = 0; precno < l_nb_precincts; ++precno) {
					uint32_t tlcblkxstart, tlcblkystart, brcblkxend, brcblkyend;
					uint32_t cbgxstart = tlcbgxstart
							+ (precno % l_res->pw) * (1 << cbgwidthexpn);
					uint32_t cbgystart = tlcbgystart
							+ (precno / l_res->pw) * (1 << cbgheightexpn);
					uint32_t cbgxend = cbgxstart + (1 << cbgwidthexpn);
					uint32_t cbgyend = cbgystart + (1 << cbgheightexpn);
					/*fprintf(stderr, "\t precno=%d; bandno=%d, resno=%d; compno=%d\n", precno, bandno , resno, compno);*/
					/*fprintf(stderr, "\t tlcbgxstart(=%d) + (precno(=%d) percent res->pw(=%d)) * (1 << cbgwidthexpn(=%d)) \n",tlcbgxstart,precno,l_res->pw,cbgwidthexpn);*/

					/* precinct size (global) */
					/*fprintf(stderr, "\t cbgxstart=%d, l_band->x0 = %d \n",cbgxstart, l_band->x0);*/

					l_current_precinct->x0 = std::max<uint32_t>(cbgxstart,
							l_band->x0);
					l_current_precinct->y0 = std::max<uint32_t>(cbgystart,
							l_band->y0);
					l_current_precinct->x1 = std::min<uint32_t>(cbgxend,
							l_band->x1);
					l_current_precinct->y1 = std::min<uint32_t>(cbgyend,
							l_band->y1);
					/*fprintf(stderr, "\t prc_x0=%d; prc_y0=%d, prc_x1=%d; prc_y1=%d\n",l_current_precinct->x0, l_current_precinct->y0 ,l_current_precinct->x1, l_current_precinct->y1);*/

					tlcblkxstart = uint_floordivpow2(l_current_precinct->x0,
							cblkwidthexpn) << cblkwidthexpn;
					/*fprintf(stderr, "\t tlcblkxstart =%d\n",tlcblkxstart );*/
					tlcblkystart = uint_floordivpow2(l_current_precinct->y0,
							cblkheightexpn) << cblkheightexpn;
					/*fprintf(stderr, "\t tlcblkystart =%d\n",tlcblkystart );*/
					brcblkxend = uint_ceildivpow2(l_current_precinct->x1,
							cblkwidthexpn) << cblkwidthexpn;
					/*fprintf(stderr, "\t brcblkxend =%d\n",brcblkxend );*/
					brcblkyend = uint_ceildivpow2(l_current_precinct->y1,
							cblkheightexpn) << cblkheightexpn;
					/*fprintf(stderr, "\t brcblkyend =%d\n",brcblkyend );*/
					l_current_precinct->cw = ((brcblkxend - tlcblkxstart)
							>> cblkwidthexpn);
					l_current_precinct->ch = ((brcblkyend - tlcblkystart)
							>> cblkheightexpn);

					l_nb_code_blocks = (uint64_t) l_current_precinct->cw
							* l_current_precinct->ch;
					/*fprintf(stderr, "\t\t\t\t precinct_cw = %d x recinct_ch = %d\n",l_current_precinct->cw, l_current_precinct->ch);      */
					l_nb_code_blocks_size = l_nb_code_blocks * sizeof_block;

					if (!l_current_precinct->cblks.blocks
							&& (l_nb_code_blocks > 0U)) {
						l_current_precinct->cblks.blocks = grok_malloc(
								l_nb_code_blocks_size);
						if (!l_current_precinct->cblks.blocks) {
							return false;
						}
						/*fprintf(stderr, "\t\t\t\tAllocate cblks of a precinct (grk_tcd_cblk_dec): %d\n",l_nb_code_blocks_size);*/
						memset(l_current_precinct->cblks.blocks, 0,
								l_nb_code_blocks_size);

						l_current_precinct->block_size = l_nb_code_blocks_size;
					} else if (l_nb_code_blocks_size
							> l_current_precinct->block_size) {
						void *new_blocks = grok_realloc(
								l_current_precinct->cblks.blocks,
								l_nb_code_blocks_size);
						if (!new_blocks) {
							grok_free(l_current_precinct->cblks.blocks);
							l_current_precinct->cblks.blocks = nullptr;
							l_current_precinct->block_size = 0;
							GROK_ERROR(
									"Not enough memory for current precinct codeblock element");
							return false;
						}
						l_current_precinct->cblks.blocks = new_blocks;
						/*fprintf(stderr, "\t\t\t\tReallocate cblks of a precinct (grk_tcd_cblk_dec): from %d to %d\n",l_current_precinct->block_size, l_nb_code_blocks_size);     */

						memset(	((uint8_t*) l_current_precinct->cblks.blocks)
										+ l_current_precinct->block_size, 0,
								l_nb_code_blocks_size
										- l_current_precinct->block_size);
						l_current_precinct->block_size = l_nb_code_blocks_size;
					}

					l_current_precinct->initTagTrees();

					for (cblkno = 0; cblkno < l_nb_code_blocks; ++cblkno) {
						uint32_t cblkxstart = tlcblkxstart
								+ (uint32_t) (cblkno % l_current_precinct->cw)
										* (1 << cblkwidthexpn);
						uint32_t cblkystart = tlcblkystart
								+ (uint32_t) (cblkno / l_current_precinct->cw)
										* (1 << cblkheightexpn);
						uint32_t cblkxend = cblkxstart + (1 << cblkwidthexpn);
						uint32_t cblkyend = cblkystart + (1 << cblkheightexpn);

						if (isEncoder) {
							grk_tcd_cblk_enc *l_code_block =
									l_current_precinct->cblks.enc + cblkno;

							if (!l_code_block->alloc()) {
								return false;
							}
							/* code-block size (global) */
							l_code_block->x0 = std::max<uint32_t>(cblkxstart,
									l_current_precinct->x0);
							l_code_block->y0 = std::max<uint32_t>(cblkystart,
									l_current_precinct->y0);
							l_code_block->x1 = std::min<uint32_t>(cblkxend,
									l_current_precinct->x1);
							l_code_block->y1 = std::min<uint32_t>(cblkyend,
									l_current_precinct->y1);

							if (!current_plugin_tile
									|| (state & GROK_PLUGIN_STATE_DEBUG)) {
								if (!l_code_block->alloc_data(
										nominalBlockSize)) {
									return false;
								}
							}
						} else {
							grk_tcd_cblk_dec *l_code_block =
									l_current_precinct->cblks.dec + cblkno;
							if (!current_plugin_tile
									|| (state & GROK_PLUGIN_STATE_DEBUG)) {
								if (!l_code_block->alloc()) {
									return false;
								}
							}

							/* code-block size (global) */
							l_code_block->x0 = std::max<uint32_t>(cblkxstart,
									l_current_precinct->x0);
							l_code_block->y0 = std::max<uint32_t>(cblkystart,
									l_current_precinct->y0);
							l_code_block->x1 = std::min<uint32_t>(cblkxend,
									l_current_precinct->x1);
							l_code_block->y1 = std::min<uint32_t>(cblkyend,
									l_current_precinct->y1);
						}
					}
					++l_current_precinct;
				} /* precno */
				++l_band;
				++l_step_size;
			} /* bandno */
			++l_res;
		} /* resno */
		l_tilec->finalizeCoordinates(isEncoder);
		if (!l_tilec->create_buffer(isEncoder,
				l_tccp->qmfbid ? false : true,
						output_image, l_image_comp->dx,
				l_image_comp->dy)) {
			return false;
		}
		l_tilec->buf->data_size_needed = l_tilec->size();

		++l_tccp;
		++l_tilec;
		++l_image_comp;
	} /* compno */

	// decoder plugin debug sanity check on tile struct
	if (!isEncoder) {
		if (state & GROK_PLUGIN_STATE_DEBUG) {
			if (!tile_equals(current_plugin_tile, l_tile)) {
				GROK_WARN("plugin tile differs from grok tile",
						nullptr);
			}
		}
	}
	tile->packno = 0;
	return true;
}

bool TileProcessor::init_encode_tile(uint16_t tile_no) {
	return init_tile(tile_no, nullptr, true, 1.0F,
			sizeof(grk_tcd_cblk_enc));
}

bool TileProcessor::init_decode_tile(grk_image *output_image,
		uint16_t tile_no) {
	return init_tile(tile_no, output_image, false, 0.5F,
			sizeof(grk_tcd_cblk_dec));

}

/*
 Get size of tile data, summed over all components, reflecting actual precision of data.
 grk_image always stores data in 32 bit format.
 */
uint64_t TileProcessor::get_decoded_tile_size() {
	uint32_t i;
	uint64_t l_data_size = 0;
	uint32_t l_size_comp;

	auto l_tile_comp = tile->comps;
	auto l_img_comp = image->comps;

	for (i = 0; i < image->numcomps; ++i) {
		l_size_comp = (l_img_comp->prec + 7) >> 3;

		if (l_size_comp == 3) {
			l_size_comp = 4;
		}

		auto area = l_tile_comp->buf->reduced_image_dim.area();
		l_data_size += l_size_comp * area;

		++l_img_comp;
		++l_tile_comp;
	}

	return l_data_size;
}

bool TileProcessor::encode_tile(uint16_t tile_no, BufferedStream *p_stream,
		uint64_t *p_data_written, uint64_t max_length,
		 grk_codestream_info  *p_cstr_info) {
	uint32_t state = grok_plugin_get_debug_state();
	if (cur_tp_num == 0) {

		tcd_tileno = tile_no;
		tcp = &cp->tcps[tile_no];

		if (p_cstr_info) {
			uint32_t l_num_packs = 0;
			uint32_t i;
			auto l_tilec_idx = &tile->comps[0]; /* based on component 0 */
			auto l_tccp = tcp->tccps; /* based on component 0 */

			for (i = 0; i < l_tilec_idx->numresolutions; i++) {
				auto l_res_idx = &l_tilec_idx->resolutions[i];

				p_cstr_info->tile[tile_no].pw[i] = (int) l_res_idx->pw;
				p_cstr_info->tile[tile_no].ph[i] = (int) l_res_idx->ph;

				l_num_packs += l_res_idx->pw * l_res_idx->ph;
				p_cstr_info->tile[tile_no].pdx[i] = (int) l_tccp->prcw[i];
				p_cstr_info->tile[tile_no].pdy[i] = (int) l_tccp->prch[i];
			}
			p_cstr_info->tile[tile_no].packet =
					( grk_packet_info  * ) grok_calloc(
							(size_t) p_cstr_info->numcomps
									* (size_t) p_cstr_info->numlayers
									* l_num_packs, sizeof( grk_packet_info) );
			if (!p_cstr_info->tile[tile_no].packet) {
				GROK_ERROR(
						"tcd_encode_tile: Out of memory error when allocating packet memory");
				return false;
			}
		}
		if (state & GROK_PLUGIN_STATE_DEBUG) {
			set_context_stream(this);
		}

		// When debugging the encoder, we do all of T1 up to and including DWT in the plugin, and pass this in as image data.
		// This way, both Grok and plugin start with same inputs for context formation and MQ coding.
		bool debugEncode = state & GROK_PLUGIN_STATE_DEBUG;
		bool debugMCT = (state & GROK_PLUGIN_STATE_MCT_ONLY) ? true : false;

		if (!current_plugin_tile || debugEncode) {

			if (!debugEncode) {
				if (!dc_level_shift_encode()) {
					return false;
				}
				if (!mct_encode()) {
					return false;
				}
			}
			if (!debugEncode || debugMCT) {
				if (!dwt_encode()) {
					return false;
				}
			}
			if (!t1_encode()) {
				return false;
			}
		}
		if (!rate_allocate_encode(max_length, p_cstr_info)) {
			return false;
		}
	}
	if (p_cstr_info) {
		p_cstr_info->index_write = 1;
	}
	if (!t2_encode(p_stream, p_data_written, max_length,
			p_cstr_info)) {
		return false;
	}
	return true;
}

#if 0
/** Returns whether a tile component should be fully decoded,
 * taking into account win_* members.
 *
 * @param compno Component number
 * @return true if the tile component should be fully decoded
 */
bool TileProcessor::is_whole_tilecomp_decoding( uint32_t compno)
{
    auto tilec = tile->comps + compno;
    auto image_comp = image->comps + compno;
    /* Compute the intersection of the area of interest, expressed in tile coordinates */
    /* with the tile coordinates */
    uint32_t tcx0 = max<uint32_t>(
                          (uint32_t)tilec->x0,
                          ceildiv<uint32_t>(win_x0, image_comp->dx));
    uint32_t tcy0 = max<uint32_t>(
                          (uint32_t)tilec->y0,
						  ceildiv<uint32_t>(win_y0, image_comp->dy));
    uint32_t tcx1 = min<uint32_t>(
                          (uint32_t)tilec->x1,
						  ceildiv<uint32_t>(win_x1, image_comp->dx));
    uint32_t tcy1 = min<uint32_t>(
                          (uint32_t)tilec->y1,
						  ceildiv<uint32_t>(win_y1, image_comp->dy));

    uint32_t shift = tilec->numresolutions - tilec->minimum_num_resolutions;
    /* Tolerate small margin within the reduced resolution factor to consider if */
    /* the whole tile path must be taken */
    return (tcx0 >= (uint32_t)tilec->x0 &&
            tcy0 >= (uint32_t)tilec->y0 &&
            tcx1 <= (uint32_t)tilec->x1 &&
            tcy1 <= (uint32_t)tilec->y1 &&
            (shift >= 32 ||
             (((tcx0 - (uint32_t)tilec->x0) >> shift) == 0 &&
              ((tcy0 - (uint32_t)tilec->y0) >> shift) == 0 &&
              (((uint32_t)tilec->x1 - tcx1) >> shift) == 0 &&
              (((uint32_t)tilec->y1 - tcy1) >> shift) == 0)));

}
#endif

bool TileProcessor::decode_tile(ChunkBuffer *src_buf, uint16_t tile_no) {
	tcp = cp->tcps + tile_no;

	// optimization for regions that are close to largest decoded resolution
	// (currently breaks tests, so disabled)
	/*
    for (uint32_t compno = 0; compno < image->numcomps; compno++) {
		if (!is_whole_tilecomp_decoding(compno)) {
			whole_tile_decoding = false;
			break;
		}
	}
	*/
    if (!whole_tile_decoding) {
	  /* Compute restricted tile-component and tile-resolution coordinates */
	  /* of the window of interest */
	  for (uint32_t compno = 0; compno < image->numcomps; compno++) {
		  uint32_t resno;
		  auto tilec = tile->comps + compno;

		  /* Compute the intersection of the area of interest, expressed in tile coordinates */
		  /* with the tile coordinates */
		  auto dims = tilec->buf->reduced_image_dim;
		  uint32_t win_x0 = max<uint32_t>(
							  (uint32_t)tilec->x0,
							  dims.x0);
		  uint32_t win_y0 = max<uint32_t>(
							  (uint32_t)tilec->y0,
							  dims.y0);
		  uint32_t win_x1 = min<uint32_t>(
							  (uint32_t)tilec->x1,
							  dims.x1);
		  uint32_t win_y1 = min<uint32_t>(
							  (uint32_t)tilec->y1,
							  dims.y1);
		  if (win_x1 < win_x0 ||  win_y1 < win_y0) {
			  /* We should not normally go there. The circumstance is when */
			  /* the tile coordinates do not intersect the area of interest */
			  /* Upper level logic should not even try to decode that tile */
			  GROK_ERROR("Invalid tilec->win_xxx values\n");
			  return false;
		  }

		  for (resno = 0; resno < tilec->minimum_num_resolutions; ++resno) {
			  auto res = tilec->resolutions + resno;
			  res->win_x0 = uint_ceildivpow2(win_x0,
												 tilec->minimum_num_resolutions - 1 - resno);
			  res->win_y0 = uint_ceildivpow2(win_y0,
												 tilec->minimum_num_resolutions - 1 - resno);
			  res->win_x1 = uint_ceildivpow2(win_x1,
												 tilec->minimum_num_resolutions - 1 - resno);
			  res->win_y1 = uint_ceildivpow2(win_y1,
												 tilec->minimum_num_resolutions - 1 - resno);
		  }
	  }
    }



	bool doT2 = !current_plugin_tile
			|| (current_plugin_tile->decode_flags & GROK_DECODE_T2);

	bool doT1 = !current_plugin_tile
			|| (current_plugin_tile->decode_flags & GROK_DECODE_T1);

	bool doPostT1 = !current_plugin_tile
			|| (current_plugin_tile->decode_flags & GROK_DECODE_POST_T1);

	if (doT2) {
		uint64_t l_data_read = 0;
		if (!t2_decode(tile_no, src_buf, &l_data_read)) {
			return false;
		}
		// synch plugin with T2 data
		decode_synch_plugin_with_host(this);
	}

	if (doT1) {
		if (!t1_decode()) {
			return false;
		}
	}

	if (doPostT1) {

		if (!dwt_decode()) {
			return false;
		}
		if (!mct_decode()) {
			return false;
		}
		if (!dc_level_shift_decode()) {
			return false;
		}
	}
	return true;
}

/*

 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ToDo: does the code below assume consistent resolution dimensions across components? Because of subsampling, I don't think
 we can assume that this is the code, but need to check that code is aware of this.
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



 For each component, copy decoded resolutions from the tile data buffer
 into p_dest buffer.

 So, p_dest stores a sub-region of the tcd data, based on the number
 of resolutions decoded. (why doesn't tile data buffer also match number of resolutions decoded ?)

 Note: p_dest stores data in the actual precision of the decompressed image,
 vs. tile data buffer which is always 32 bits.

 If we are decoding all resolutions, then this step is not necessary ??

 */
bool TileProcessor::update_tile_data(uint8_t *p_dest,
		uint64_t dest_length) {
	uint32_t i, j, k;
	uint64_t l_data_size = get_decoded_tile_size();
	if (l_data_size > dest_length) {
		return false;
	}

	auto l_tilec = tile->comps;
	auto l_img_comp = image->comps;

	for (i = 0; i < image->numcomps; ++i) {
		uint32_t l_size_comp = (l_img_comp->prec + 7) >> 3;

		uint32_t l_stride = 0, l_width = 0, l_height = 0;

		l_width = l_tilec->buf->reduced_image_dim.width();
		l_height = l_tilec->buf->reduced_image_dim.height();

		const int32_t *l_src_ptr = l_tilec->buf->get_ptr( 0, 0, 0,0);

		if (l_size_comp == 3) {
			l_size_comp = 4;
		}

		switch (l_size_comp) {
		case 1: {
			char *l_dest_ptr = (char*) p_dest;
			if (l_img_comp->sgnd) {
				for (j = 0; j < l_height; ++j) {
					for (k = 0; k < l_width; ++k) {
						*(l_dest_ptr++) = (char) (*(l_src_ptr++));
					}
					l_src_ptr += l_stride;
				}
			} else {
				for (j = 0; j < l_height; ++j) {
					for (k = 0; k < l_width; ++k) {
						*(l_dest_ptr++) = (char) ((*(l_src_ptr++)) & 0xff);
					}
					l_src_ptr += l_stride;
				}
			}
			p_dest = (uint8_t*) l_dest_ptr;
		}
			break;
		case 2: {
			int16_t *l_dest_ptr = (int16_t*) p_dest;
			if (l_img_comp->sgnd) {
				for (j = 0; j < l_height; ++j) {
					for (k = 0; k < l_width; ++k) {
						*(l_dest_ptr++) = (int16_t) (*(l_src_ptr++));
					}
					l_src_ptr += l_stride;
				}
			} else {
				for (j = 0; j < l_height; ++j) {
					for (k = 0; k < l_width; ++k) {
						//cast and mask to avoid sign extension
						*(l_dest_ptr++) = (int16_t) ((*(l_src_ptr++)) & 0xffff);
					}
					l_src_ptr += l_stride;
				}
			}

			p_dest = (uint8_t*) l_dest_ptr;
		}
			break;
		case 4: {
			int32_t *l_dest_ptr = (int32_t*) p_dest;
			for (j = 0; j < l_height; ++j) {
				for (k = 0; k < l_width; ++k) {
					*(l_dest_ptr++) = (*(l_src_ptr++));
				}
				l_src_ptr += l_stride;
			}
			p_dest = (uint8_t*) l_dest_ptr;
			}
			break;
		}
		++l_img_comp;
		++l_tilec;
	}

	return true;
}

void TileProcessor::free_tile() {
	uint32_t compno, resno, bandno, precno;
	if (!tile) {
		return;
	}
	auto l_tile = tile;
	if (!l_tile) {
		return;
	}
	auto l_tile_comp = l_tile->comps;
	for (compno = 0; compno < l_tile->numcomps; ++compno) {
		auto l_res = l_tile_comp->resolutions;
		if (l_res) {
			auto l_nb_resolutions = l_tile_comp->numAllocatedResolutions;
			for (resno = 0; resno < l_nb_resolutions; ++resno) {
				auto l_band = l_res->bands;
				for (bandno = 0; bandno < 3; ++bandno) {
					auto l_precinct = l_band->precincts;
					if (l_precinct) {
						for (precno = 0; precno < l_band->numAllocatedPrecincts;
								++precno) {
							l_precinct->deleteTagTrees();
							if (m_is_decoder) {
								code_block_dec_deallocate(l_precinct);
							} else {
								code_block_enc_deallocate(l_precinct);
							}
							++l_precinct;
						}
						delete[] l_band->precincts;
						l_band->precincts = nullptr;
					}
					++l_band;
				} /* for (resno */
				++l_res;
			}
			delete[] l_tile_comp->resolutions;
			l_tile_comp->resolutions = nullptr;
		}
		delete l_tile_comp->buf;
		l_tile_comp->buf = nullptr;
		++l_tile_comp;
	}
	grok_free(l_tile->comps);
	l_tile->comps = nullptr;
	grok_free(tile);
	tile = nullptr;
}

void TileProcessor::get_tile_data(uint8_t *p_data) {
	uint32_t i, j, k = 0;

	for (i = 0; i < image->numcomps; ++i) {
		grk_image *l_image = image;
		int32_t *l_src_ptr;
		TileComponent *l_tilec = tile->comps + i;
		 grk_image_comp  *l_img_comp = l_image->comps + i;
		uint32_t l_size_comp, l_width, l_height, l_offset_x, l_offset_y,
				l_image_width, l_stride;
		uint64_t l_tile_offset;

		l_tilec->get_dimensions(l_image, l_img_comp, &l_size_comp,
				&l_width, &l_height, &l_offset_x, &l_offset_y, &l_image_width,
				&l_stride, &l_tile_offset);

		l_src_ptr = l_img_comp->data + l_tile_offset;

		switch (l_size_comp) {
		case 1: {
			char *l_dest_ptr = (char*) p_data;
			if (l_img_comp->sgnd) {
				for (j = 0; j < l_height; ++j) {
					for (k = 0; k < l_width; ++k) {
						*(l_dest_ptr) = (char) (*l_src_ptr);
						++l_dest_ptr;
						++l_src_ptr;
					}
					l_src_ptr += l_stride;
				}
			} else {
				for (j = 0; j < l_height; ++j) {
					for (k = 0; k < l_width; ++k) {
						*(l_dest_ptr) = (char) ((*l_src_ptr) & 0xff);
						++l_dest_ptr;
						++l_src_ptr;
					}
					l_src_ptr += l_stride;
				}
			}

			p_data = (uint8_t*) l_dest_ptr;
		}
			break;
		case 2: {
			int16_t *l_dest_ptr = (int16_t*) p_data;
			if (l_img_comp->sgnd) {
				for (j = 0; j < l_height; ++j) {
					for (k = 0; k < l_width; ++k) {
						*(l_dest_ptr++) = (int16_t) (*(l_src_ptr++));
					}
					l_src_ptr += l_stride;
				}
			} else {
				for (j = 0; j < l_height; ++j) {
					for (k = 0; k < l_width; ++k) {
						*(l_dest_ptr++) = (int16_t) ((*(l_src_ptr++)) & 0xffff);
					}
					l_src_ptr += l_stride;
				}
			}

			p_data = (uint8_t*) l_dest_ptr;
		}
			break;
		case 4: {
			int32_t *l_dest_ptr = (int32_t*) p_data;
			for (j = 0; j < l_height; ++j) {
				for (k = 0; k < l_width; ++k) {
					*(l_dest_ptr++) = *(l_src_ptr++);
				}
				l_src_ptr += l_stride;
			}

			p_data = (uint8_t*) l_dest_ptr;
		}
			break;
		}
	}
}
bool TileProcessor::t2_decode(uint16_t tile_no, ChunkBuffer *src_buf,
		uint64_t *p_data_read) {
	auto l_t2 = new T2(image, cp);

	if (!l_t2->decode_packets(tile_no, tile, src_buf, p_data_read)) {
		delete l_t2;
		return false;
	}
	delete l_t2;

	return true;
}

bool TileProcessor::t1_decode() {
	uint32_t compno;
	auto l_tile = tile;
	auto l_tile_comp = l_tile->comps;
	auto l_tccp = tcp->tccps;
	std::vector<decodeBlockInfo*> blocks;
	auto t1_wrap = std::unique_ptr<Tier1>(new Tier1());
	for (compno = 0; compno < l_tile->numcomps; ++compno) {
		if (!t1_wrap->prepareDecodeCodeblocks(l_tile_comp, l_tccp, &blocks)) {
			return false;
		}
		++l_tile_comp;
		++l_tccp;
	}
	// !!! assume that code block dimensions do not change over components
	return t1_wrap->decodeCodeblocks(this->cp,
			tcp,
			(uint16_t) tcp->tccps->cblkw,
			(uint16_t) tcp->tccps->cblkh, &blocks);
}

bool TileProcessor::dwt_decode() {
	grk_tcd_tile *l_tile = tile;
	int64_t compno = 0;
	bool rc = true;
	for (compno = 0; compno < (int64_t) l_tile->numcomps; compno++) {
		auto l_tile_comp = l_tile->comps + compno;
		auto l_tccp = tcp->tccps + compno;
		auto l_img_comp = image->comps + compno;
		 if (!Wavelet::decode(this, l_tile_comp, l_img_comp->resno_decoded + 1,l_tccp->qmfbid)) {
			rc = false;
			continue;

		}
	}

	return rc;
}
bool TileProcessor::mct_decode() {
	auto l_tile = tile;
	auto l_tcp = tcp;
	auto l_tile_comp = l_tile->comps;
	uint64_t i;

	if (!l_tcp->mct) {
		return true;
	}

	uint64_t image_samples  = l_tile_comp->buf->reduced_image_dim.area();
	uint64_t l_samples = image_samples;

	if (l_tile->numcomps >= 3) {
		/* testcase 1336.pdf.asan.47.376 */
		if (l_tile->comps[0].buf->reduced_image_dim.area() < image_samples
				|| l_tile->comps[1].buf->reduced_image_dim.area()
						< image_samples
				|| l_tile->comps[2].buf->reduced_image_dim.area()< image_samples) {
			GROK_ERROR(
					"Tiles don't all have the same dimension. Skip the MCT step.");
			return false;
		} else if (l_tcp->mct == 2) {
			uint8_t **l_data;

			if (!l_tcp->m_mct_decoding_matrix) {
				return true;
			}

			l_data = (uint8_t**) grok_malloc(
					l_tile->numcomps * sizeof(uint8_t*));
			if (!l_data) {
				return false;
			}

			for (i = 0; i < l_tile->numcomps; ++i) {
				l_data[i] = (uint8_t*) l_tile_comp->buf->get_ptr( 0, 0,
						0, 0);
				++l_tile_comp;
			}

			if (!mct_decode_custom(/* MCT data */
			(uint8_t*) l_tcp->m_mct_decoding_matrix,
			/* size of components */
			l_samples,
			/* components */
			l_data,
			/* nb of components (i.e. size of pData) */
			l_tile->numcomps,
			/* tells if the data is signed */
			image->comps->sgnd)) {
				grok_free(l_data);
				return false;
			}

			grok_free(l_data);
		} else {
			if (l_tcp->tccps->qmfbid == 1) {
				grk::mct_decode(l_tile->comps[0].buf->get_ptr( 0, 0, 0, 0),
						l_tile->comps[1].buf->get_ptr(0, 0, 0, 0),
						l_tile->comps[2].buf->get_ptr(0, 0, 0, 0),
						l_samples);
			} else {
				grk::mct_decode_real(
						(float*) l_tile->comps[0].buf->get_ptr( 0, 0, 0,
								0),
						(float*) l_tile->comps[1].buf->get_ptr(0, 0, 0,
								0),
						(float*) l_tile->comps[2].buf->get_ptr( 0, 0, 0,
								0), l_samples);
			}
		}
	} else {
		GROK_ERROR(
				"Number of components (%d) is inconsistent with a MCT. Skip the MCT step.\n",
				l_tile->numcomps);
	}

	return true;
}

bool TileProcessor::dc_level_shift_decode() {
	uint32_t compno = 0;
	for (compno = 0; compno < tile->numcomps; compno++) {
		int32_t l_min = INT32_MAX, l_max = INT32_MIN;
		uint32_t x0 ;
		uint32_t y0;
		uint32_t x1;
		uint32_t y1 ;
		auto l_tile_comp = tile->comps + compno;
		auto l_tccp = tcp->tccps + compno;
		auto l_img_comp = image->comps + compno;
		uint32_t l_stride = 0;

		int32_t *l_current_ptr = l_tile_comp->buf->get_ptr( 0, 0, 0, 0);

		x0 = 0;
		y0 = 0;
		x1 = (uint32_t)(l_tile_comp->buf->reduced_image_dim.width());
		y1 = (uint32_t)(l_tile_comp->buf->reduced_image_dim.height());

		assert(x1 >= x0);
		assert(l_tile_comp->width() >= (x1 - x0));


		if (l_img_comp->sgnd) {
			l_min = -(1 << (l_img_comp->prec - 1));
			l_max = (1 << (l_img_comp->prec - 1)) - 1;
		} else {
			l_min = 0;
			l_max = (1 << l_img_comp->prec) - 1;
		}

		if (l_tccp->qmfbid == 1) {
			for (uint32_t j = y0; j < y1; ++j) {
				for (uint32_t i = x0; i < x1; ++i) {
					*l_current_ptr = int_clamp(
							*l_current_ptr + l_tccp->m_dc_level_shift, l_min,
							l_max);
					l_current_ptr++;
				}
				l_current_ptr += l_stride;
			}
		} else {
			for (uint32_t j = y0; j < y1; ++j) {
				for (uint32_t i = x0; i < x1; ++i) {
					float l_value = *((float*) l_current_ptr);
					*l_current_ptr = int_clamp(
							(int32_t) grok_lrintf(l_value)
									+ l_tccp->m_dc_level_shift, l_min, l_max);
					l_current_ptr++;
				}
				l_current_ptr += l_stride;
			}
		}
	}
	return true;
}

/**
 * Deallocates the encoding data of the given precinct.
 */
void TileProcessor::code_block_dec_deallocate(grk_tcd_precinct *p_precinct) {
	uint64_t cblkno, l_nb_code_blocks;
	auto l_code_block = p_precinct->cblks.dec;
	if (l_code_block) {
		/*fprintf(stderr,"deallocate codeblock:{\n");*/
		/*fprintf(stderr,"\t x0=%d, y0=%d, x1=%d, y1=%d\n",l_code_block->x0, l_code_block->y0, l_code_block->x1, l_code_block->y1);*/
		/*fprintf(stderr,"\t numbps=%d, numlenbits=%d, len=%d, numPassesInPacket=%d, real_num_segs=%d, numSegmentsAllocated=%d\n ",
		 l_code_block->numbps, l_code_block->numlenbits, l_code_block->len, l_code_block->numPassesInPacket, l_code_block->numSegments, l_code_block->numSegmentsAllocated );*/

		l_nb_code_blocks = p_precinct->block_size / sizeof(grk_tcd_cblk_dec);
		/*fprintf(stderr,"nb_code_blocks =%d\t}\n", l_nb_code_blocks);*/

		for (cblkno = 0; cblkno < l_nb_code_blocks; ++cblkno) {
			l_code_block->cleanup();
			++l_code_block;
		}
		grok_free(p_precinct->cblks.dec);
		p_precinct->cblks.dec = nullptr;
	}
}

/**
 * Deallocates the encoding data of the given precinct.
 */
void TileProcessor::code_block_enc_deallocate(grk_tcd_precinct *p_precinct) {
	uint64_t cblkno, l_nb_code_blocks;
	auto l_code_block = p_precinct->cblks.enc;
	if (l_code_block) {
		l_nb_code_blocks = p_precinct->block_size / sizeof(grk_tcd_cblk_enc);
		for (cblkno = 0; cblkno < l_nb_code_blocks; ++cblkno) {
			l_code_block->cleanup();
			++l_code_block;
		}
		grok_free(p_precinct->cblks.enc);
		p_precinct->cblks.enc = nullptr;
	}
}

uint64_t TileProcessor::get_encoded_tile_size() {
	uint32_t i = 0;
	uint32_t l_size_comp, l_remaining;
	uint64_t l_data_size = 0;

	auto l_tilec = tile->comps;
	auto l_img_comp = image->comps;
	for (i = 0; i < image->numcomps; ++i) {
		l_size_comp = l_img_comp->prec >> 3; /*(/ 8)*/
		l_remaining = l_img_comp->prec & 7; /* (%8) */

		if (l_remaining) {
			++l_size_comp;
		}

		if (l_size_comp == 3) {
			l_size_comp = 4;
		}

		l_data_size += l_size_comp * l_tilec->area();
		++l_img_comp;
		++l_tilec;
	}

	return l_data_size;
}

bool TileProcessor::dc_level_shift_encode() {
	uint32_t compno;
	uint64_t l_nb_elem, i;
	int32_t *l_current_ptr;

	auto l_tile = tile;
	auto l_tile_comp = l_tile->comps;
	auto l_tccp = tcp->tccps;
	auto l_img_comp = image->comps;

	for (compno = 0; compno < l_tile->numcomps; compno++) {
		l_current_ptr = l_tile_comp->buf->get_ptr( 0, 0, 0, 0);
		l_nb_elem = l_tile_comp->area();

		if (l_tccp->qmfbid == 1) {
			for (i = 0; i < l_nb_elem; ++i) {
				*l_current_ptr -= l_tccp->m_dc_level_shift;
				++l_current_ptr;
			}
		} else {
			for (i = 0; i < l_nb_elem; ++i) {
				*l_current_ptr = (*l_current_ptr - l_tccp->m_dc_level_shift)
						* (1 << 11);
				++l_current_ptr;
			}
		}

		++l_img_comp;
		++l_tccp;
		++l_tile_comp;
	}

	return true;
}

bool TileProcessor::mct_encode() {
	auto l_tile = tile;
	auto l_tile_comp = tile->comps;
	uint64_t samples = l_tile_comp->area();
	uint32_t i;
	auto l_tcp = tcp;

	if (!tcp->mct) {
		return true;
	}

	if (tcp->mct == 2) {
		if (!tcp->m_mct_coding_matrix) {
			return true;
		}

		auto l_data = (uint8_t**) grok_malloc(l_tile->numcomps * sizeof(uint8_t*));
		if (!l_data) {
			return false;
		}

		for (i = 0; i < l_tile->numcomps; ++i) {
			l_data[i] = (uint8_t*) l_tile_comp->buf->get_ptr( 0, 0, 0,
					0);
			++l_tile_comp;
		}

		if (!mct_encode_custom(/* MCT data */
		(uint8_t*) tcp->m_mct_coding_matrix,
		/* size of components */
		samples,
		/* components */
		l_data,
		/* nb of components (i.e. size of pData) */
		l_tile->numcomps,
		/* tells if the data is signed */
		image->comps->sgnd)) {
			grok_free(l_data);
			return false;
		}

		grok_free(l_data);
	} else if (l_tcp->tccps->qmfbid == 0) {
		grk::mct_encode_real(l_tile->comps[0].buf->get_ptr( 0, 0, 0, 0),
				l_tile->comps[1].buf->get_ptr( 0, 0, 0, 0),
				l_tile->comps[2].buf->get_ptr( 0, 0, 0, 0), samples);
	} else {
		grk::mct_encode(l_tile->comps[0].buf->get_ptr( 0, 0, 0, 0),
				l_tile->comps[1].buf->get_ptr(0, 0, 0, 0),
				l_tile->comps[2].buf->get_ptr(0, 0, 0, 0), samples);
	}

	return true;
}

bool TileProcessor::dwt_encode() {
	auto l_tile = tile;
	uint32_t compno = 0;
	bool rc = true;
	for (compno = 0; compno < (int64_t) l_tile->numcomps; ++compno) {
		auto tile_comp = tile->comps + compno;
		auto l_tccp = tcp->tccps + compno;
		if (!Wavelet::encode(tile_comp, l_tccp->qmfbid)) {
			rc = false;
			continue;

		}
	}
	return rc;
}

bool TileProcessor::t1_encode() {
	const double *l_mct_norms;
	uint32_t l_mct_numcomps = 0U;
	auto l_tcp = tcp;

	if (l_tcp->mct == 1) {
		l_mct_numcomps = 3U;
		/* irreversible encoding */
		if (l_tcp->tccps->qmfbid == 0) {
			l_mct_norms = mct_get_mct_norms_real();
		} else {
			l_mct_norms = mct_get_mct_norms();
		}
	} else {
		l_mct_numcomps = image->numcomps;
		l_mct_norms = (const double*) (l_tcp->mct_norms);
	}

	auto t1_wrap = std::unique_ptr<Tier1>(new Tier1());

	return t1_wrap->encodeCodeblocks(l_tcp, tile, l_mct_norms,
			l_mct_numcomps, needs_rate_control());
}

bool TileProcessor::t2_encode(BufferedStream *p_stream,
		uint64_t *p_data_written, uint64_t max_dest_size,
		 grk_codestream_info  *p_cstr_info) {

	auto l_t2 = new T2(image, cp);
#ifdef DEBUG_LOSSLESS_T2
	for (uint32_t compno = 0; compno < p_image->numcomps; ++compno) {
		TileComponent *tilec = &p_tile->comps[compno];
		tilec->round_trip_resolutions = new grk_tcd_resolution[tilec->numresolutions];
		for (uint32_t resno = 0; resno < tilec->numresolutions; ++resno) {
			auto res = tilec->resolutions + resno;
			auto roundRes = tilec->round_trip_resolutions + resno;
			roundRes->x0 = res->x0;
			roundRes->y0 = res->y0;
			roundRes->x1 = res->x1;
			roundRes->y1 = res->y1;
			roundRes->numbands = res->numbands;
			for (uint32_t bandno = 0; bandno < roundRes->numbands; ++bandno) {
				roundRes->bands[bandno] = res->bands[bandno];
				roundRes->bands[bandno].x0 = res->bands[bandno].x0;
				roundRes->bands[bandno].y0 = res->bands[bandno].y0;
				roundRes->bands[bandno].x1 = res->bands[bandno].x1;
				roundRes->bands[bandno].y1 = res->bands[bandno].y1;
			}

			// allocate
			for (uint32_t bandno = 0; bandno < roundRes->numbands; ++bandno) {
				auto band = res->bands + bandno;
				auto decodeBand = roundRes->bands + bandno;
				if (!band->numPrecincts)
					continue;
				decodeBand->precincts = new grk_tcd_precinct[band->numPrecincts];
				decodeBand->precincts_data_size = (uint32_t)(band->numPrecincts * sizeof(grk_tcd_precinct));
				for (size_t precno = 0; precno < band->numPrecincts; ++precno) {
					auto prec = band->precincts + precno;
					auto decodePrec = decodeBand->precincts + precno;
					decodePrec->cw = prec->cw;
					decodePrec->ch = prec->ch;
					if (prec->cblks.enc && prec->cw && prec->ch) {
						decodePrec->initTagTrees();
						decodePrec->cblks.dec = new grk_tcd_cblk_dec[decodePrec->cw * decodePrec->ch];
						for (uint32_t cblkno = 0; cblkno < decodePrec->cw * decodePrec->ch; ++cblkno) {
							auto cblk = prec->cblks.enc + cblkno;
							auto decodeCblk = decodePrec->cblks.dec + cblkno;
							decodeCblk->x0 = cblk->x0;
							decodeCblk->y0 = cblk->y0;
							decodeCblk->x1 = cblk->x1;
							decodeCblk->y1 = cblk->y1;
							decodeCblk->alloc();
						}
					}
				}
			}
		}
	}
#endif

	if (!l_t2->encode_packets(tcd_tileno, tile,
			tcp->numlayers, p_stream, p_data_written, max_dest_size,
			p_cstr_info, tp_num, tp_pos, cur_pino)) {
		delete l_t2;
		return false;
	}

	delete l_t2;

#ifdef DEBUG_LOSSLESS_T2
	for (uint32_t compno = 0; compno < p_image->numcomps; ++compno) {
		TileComponent *tilec = &p_tile->comps[compno];
		for (uint32_t resno = 0; resno < tilec->numresolutions; ++resno) {
			auto roundRes = tilec->round_trip_resolutions + resno;
			for (uint32_t bandno = 0; bandno < roundRes->numbands; ++bandno) {
				auto decodeBand = roundRes->bands + bandno;
				if (decodeBand->precincts) {
					for (size_t precno = 0; precno < decodeBand->numPrecincts; ++precno) {
						auto decodePrec = decodeBand->precincts + precno;
						decodePrec->cleanupDecodeBlocks();
					}
					delete[] decodeBand->precincts;
					decodeBand->precincts = nullptr;
				}
			}
		}
		delete[] tilec->round_trip_resolutions;
	}
#endif

	return true;
}

bool  TileProcessor::rate_allocate_encode(uint64_t max_dest_size,
		 grk_codestream_info  *p_cstr_info) {
	grk_coding_parameters *l_cp = cp;
	uint64_t l_nb_written = 0;

	if (p_cstr_info) {
		p_cstr_info->index_write = 0;
	}

	if (l_cp->m_coding_param.m_enc.m_disto_alloc
			|| l_cp->m_coding_param.m_enc.m_fixed_quality) {
		// rate control by rate/distortion or fixed quality
		switch (l_cp->m_coding_param.m_enc.rateControlAlgorithm) {
		case 0:
			if (!pcrd_bisect_simple( &l_nb_written, max_dest_size)) {
				return false;
			}
			break;
		case 1:
			if (!pcrd_bisect_feasible(&l_nb_written, max_dest_size)) {
				return false;
			}
			break;
		default:
			if (!pcrd_bisect_feasible(&l_nb_written, max_dest_size)) {
				return false;
			}
			break;

		}

	}

	return true;
}


bool TileProcessor::needs_copy_tile_data(grk_image* output_image, uint32_t num_tiles) {
	return  (num_tiles > 1);

}

/*
 p_data stores the number of resolutions decoded, in the actual precision of the decoded image.

 This method copies a sub-region of this region into p_output_image (which stores data in 32 bit precision)

 */
bool TileProcessor::copy_decoded_tile_to_output_image(uint8_t *p_data,
		grk_image *p_output_image, bool clearOutputOnInit) {
	uint32_t i = 0, j = 0, k = 0;
	auto image_src = image;
	for (i = 0; i < image_src->numcomps; i++) {

		auto tilec = tile->comps + i;
		auto img_comp_src = image_src->comps + i;
		auto img_comp_dest = p_output_image->comps + i;

		if (img_comp_dest->w * img_comp_dest->h == 0) {
			GROK_ERROR(
					"Output image has invalid dimensions %d x %d\n",
					img_comp_dest->w, img_comp_dest->h);
			return false;
		}

		/* Allocate output component buffer if necessary */
		if (!img_comp_dest->data) {
			if (!grk_image_single_component_data_alloc(img_comp_dest)) {
				return false;
			}
			if (clearOutputOnInit) {
				memset(img_comp_dest->data, 0,
						img_comp_dest->w * img_comp_dest->h * sizeof(int32_t));
			}
		}

		/* Copy info from decoded comp image to output image */
		img_comp_dest->resno_decoded = img_comp_src->resno_decoded;

		/*-----*/
		/* Compute the precision of the output buffer */
		uint32_t size_comp = (img_comp_src->prec + 7) >> 3;
		if (size_comp == 3) {
			size_comp = 4;
		}

		/* Border of the current output component. (x0_dest,y0_dest) corresponds to origin of dest buffer */
		auto reduce = cp->m_coding_param.m_dec.m_reduce;
		uint32_t x0_dest = uint_ceildivpow2(img_comp_dest->x0,	reduce);
		uint32_t y0_dest = uint_ceildivpow2(img_comp_dest->y0,	reduce);
		uint32_t x1_dest = x0_dest + img_comp_dest->w; /* can't overflow given that image->x1 is uint32 */
		uint32_t y1_dest = y0_dest + img_comp_dest->h;

		/*-----*/

		/* Current tile component size*/
		/*if (i == 0) {
		 fprintf(stdout, "SRC: res_x0=%d, res_x1=%d, res_y0=%d, res_y1=%d\n",
		 res->x0, res->x1, res->y0, res->y1);
		 }*/

		grk_rect src_dim = tilec->buf->reduced_image_dim;

		uint32_t width_src = (uint32_t)src_dim.width();
		uint32_t height_src = (uint32_t)src_dim.height();

		/*if (i == 0) {
		 fprintf(stdout, "DEST: x0_dest=%d, x1_dest=%d, y0_dest=%d, y1_dest=%d (%d)\n",
		 x0_dest, x1_dest, y0_dest, y1_dest, reduce );
		 }*/

		/*-----*/
		/* Compute the area (offset_x0_src, offset_y0_src, offset_x1_src, offset_y1_src)
		 * of the input buffer (decoded tile component) which will be moved
		 * to the output buffer. Compute the area of the output buffer (offset_x0_dest,
		 * offset_y0_dest, width_dest, height_dest)  which will be modified
		 * by this input area.
		 * */
		uint32_t offset_x0_src = 0, offset_y0_src = 0, offset_x1_src = 0,
				offset_y1_src = 0;

		uint32_t offset_x0_dest = 0, offset_y0_dest = 0;
		uint32_t width_dest = 0, height_dest = 0;
		if (x0_dest < src_dim.x0) {
			offset_x0_dest = src_dim.x0 - x0_dest;
			offset_x0_src = 0;

			if (x1_dest >= src_dim.x1) {
				width_dest = width_src;
				offset_x1_src = 0;
			} else {
				width_dest = x1_dest - src_dim.x0;
				offset_x1_src = (width_src - width_dest);
			}
		} else {
			offset_x0_dest = 0U;
			offset_x0_src = x0_dest - src_dim.x0;

			if (x1_dest >= src_dim.x1) {
				width_dest = width_src - offset_x0_src;
				offset_x1_src = 0;
			} else {
				width_dest = img_comp_dest->w;
				offset_x1_src = src_dim.x1 - x1_dest;
			}
		}

		if (y0_dest < src_dim.y0) {
			offset_y0_dest = src_dim.y0 - y0_dest;
			offset_y0_src = 0;

			if (y1_dest >= src_dim.y1) {
				height_dest = height_src;
				offset_y1_src = 0;
			} else {
				height_dest = y1_dest - src_dim.y0;
				offset_y1_src = (height_src - height_dest);
			}
		} else {
			offset_y0_dest = 0U;
			offset_y0_src = y0_dest - src_dim.y0;

			if (y1_dest >= src_dim.y1) {
				height_dest = height_src - offset_y0_src;
				offset_y1_src = 0;
			} else {
				height_dest = img_comp_dest->h;
				offset_y1_src = src_dim.y1 - y1_dest;
			}
		}


		if ((offset_x0_src > width_src) || (offset_y0_src > height_src)
				|| (offset_x1_src > width_src)
				|| (offset_y1_src > height_src)) {
			return false;
		}

		if (width_dest > img_comp_dest->w || height_dest > img_comp_dest->h)
			return false;

		if (width_src > img_comp_src->w || height_src > img_comp_src->h)
			return false;

		/*-----*/

		/* Compute the input buffer offset */
		size_t start_offset_src = (size_t) offset_x0_src
				+ (size_t) offset_y0_src * (size_t) width_src;
		size_t line_offset_src = (size_t) offset_x1_src
				+ (size_t) offset_x0_src;
		size_t end_offset_src = (size_t) offset_y1_src * (size_t) width_src
				- (size_t) offset_x0_src;

		/* Compute the output buffer offset */
		size_t start_offset_dest = (size_t) offset_x0_dest
				+ (size_t) offset_y0_dest * (size_t) img_comp_dest->w;
		size_t line_offset_dest = (size_t) img_comp_dest->w
				- (size_t) width_dest;

		/* Move the output buffer index to the first place where we will write*/
		auto dest_ind = start_offset_dest;

		/* Move to the first place where we will read*/
		auto src_ind = start_offset_src;

		/*if (i == 0) {
		 fprintf(stdout, "COMPO[%d]:\n",i);
		 fprintf(stdout, "SRC: start_x_src=%d, start_y_src=%d, width_src=%d, height_src=%d\n"
		 "\t tile offset:%d, %d, %d, %d\n"
		 "\t buffer offset: %d; %d, %d\n",
		 res->x0, res->y0, width_src, height_src,
		 offset_x0_src, offset_y0_src, offset_x1_src, offset_y1_src,
		 start_offset_src, line_offset_src, end_offset_src);

		 fprintf(stdout, "DEST: offset_x0_dest=%d, offset_y0_dest=%d, width_dest=%d, height_dest=%d\n"
		 "\t start offset: %d, line offset= %d\n",
		 offset_x0_dest, offset_y0_dest, width_dest, height_dest, start_offset_dest, line_offset_dest);
		 }*/

		switch (size_comp) {
		case 1: {
			char *src_ptr = (char*) p_data;
			if (img_comp_src->sgnd) {
				for (j = 0; j < height_dest; ++j) {
					for (k = 0; k < width_dest; ++k) {
						img_comp_dest->data[dest_ind++] =
								(int32_t) src_ptr[src_ind++]; /* Copy only the data needed for the output image */
					}

					dest_ind += line_offset_dest; /* Move to the next place where we will write */
					src_ind += line_offset_src; /* Move to the next place where we will read */
				}
			} else {
				for (j = 0; j < height_dest; ++j) {
					for (k = 0; k < width_dest; ++k) {
						img_comp_dest->data[dest_ind++] =
								(int32_t) (src_ptr[src_ind++] & 0xff);
					}

					dest_ind += line_offset_dest;
					src_ind += line_offset_src;
				}
			}
			src_ind += end_offset_src; /* Move to the end of this component-part of the input buffer */
			p_data = (uint8_t*) (src_ptr + src_ind); /* Keep the current position for the next component-part */
		}
			break;
		case 2: {
			int16_t *src_ptr = (int16_t*) p_data;

			if (img_comp_src->sgnd) {
				for (j = 0; j < height_dest; ++j) {
					for (k = 0; k < width_dest; ++k) {
						img_comp_dest->data[dest_ind++] = src_ptr[src_ind++];
					}

					dest_ind += line_offset_dest;
					src_ind += line_offset_src;
				}
			} else {
				for (j = 0; j < height_dest; ++j) {
					for (k = 0; k < width_dest; ++k) {
						img_comp_dest->data[dest_ind++] = src_ptr[src_ind++]
								& 0xffff;
					}
					dest_ind += line_offset_dest;
					src_ind += line_offset_src;
				}
			}
			src_ind += end_offset_src;
			p_data = (uint8_t*) (src_ptr + src_ind);
		}
			break;
		case 4: {
			auto src_ptr = (int32_t*) p_data;

			for (j = 0; j < height_dest; ++j) {
				for (k = 0; k < width_dest; ++k) {
					img_comp_dest->data[dest_ind++] = src_ptr[src_ind++];
					//memcpy(img_comp_dest->data+dest_ind, src_ptr+src_ind, sizeof(int32_t)*width_dest);
				}

				dest_ind += line_offset_dest;
				src_ind += line_offset_src;
			}

			src_ind += end_offset_src;
			p_data = (uint8_t*) (src_ptr + src_ind);
		}
			break;
		}
	}

	return true;
}

bool TileProcessor::copy_tile_data(uint8_t *p_src, uint64_t src_length) {
	uint64_t i, j;
	uint32_t l_size_comp, l_remaining;
	uint64_t l_nb_elem;
	uint64_t l_data_size = get_encoded_tile_size();
	if (!p_src || (l_data_size != src_length)) {
		return false;
	}

	auto l_tilec = tile->comps;
	auto l_img_comp = image->comps;
	for (i = 0; i < image->numcomps; ++i) {
		l_size_comp = l_img_comp->prec >> 3; /*(/ 8)*/
		l_remaining = l_img_comp->prec & 7; /* (%8) */
		l_nb_elem = l_tilec->area();

		if (l_remaining) {
			++l_size_comp;
		}

		if (l_size_comp == 3) {
			l_size_comp = 4;
		}

		switch (l_size_comp) {
		case 1: {
			char *l_src_ptr = (char*) p_src;
			int32_t *l_dest_ptr = l_tilec->buf->data;

			if (l_img_comp->sgnd) {
				for (j = 0; j < l_nb_elem; ++j) {
					*(l_dest_ptr++) = (int32_t) (*(l_src_ptr++));
				}
			} else {
				for (j = 0; j < l_nb_elem; ++j) {
					*(l_dest_ptr++) = (*(l_src_ptr++)) & 0xff;
				}
			}

			p_src = (uint8_t*) l_src_ptr;
		}
			break;
		case 2: {
			int32_t *l_dest_ptr = l_tilec->buf->data;
			int16_t *l_src_ptr = (int16_t*) p_src;

			if (l_img_comp->sgnd) {
				for (j = 0; j < l_nb_elem; ++j) {
					*(l_dest_ptr++) = (int32_t) (*(l_src_ptr++));
				}
			} else {
				for (j = 0; j < l_nb_elem; ++j) {
					*(l_dest_ptr++) = (*(l_src_ptr++)) & 0xffff;
				}
			}

			p_src = (uint8_t*) l_src_ptr;
		}
			break;
		case 4: {
			int32_t *l_src_ptr = (int32_t*) p_src;
			int32_t *l_dest_ptr = l_tilec->buf->data;

			for (j = 0; j < l_nb_elem; ++j) {
				*(l_dest_ptr++) = (int32_t) (*(l_src_ptr++));
			}

			p_src = (uint8_t*) l_src_ptr;
		}
			break;
		}

		++l_img_comp;
		++l_tilec;
	}

	return true;
}

grk_tcd_cblk_enc::~grk_tcd_cblk_enc() {
	cleanup();
}
bool grk_tcd_cblk_enc::alloc() {
	if (!layers) {
		/* no memset since data */
		layers = (grk_tcd_layer*) grok_calloc(100, sizeof(grk_tcd_layer));
		if (!layers) {
			return false;
		}
	}
	if (!passes) {
		passes = (grk_tcd_pass*) grok_calloc(100, sizeof(grk_tcd_pass));
		if (!passes) {
			return false;
		}
	}
#ifdef DEBUG_LOSSLESS_T2
	packet_length_info = new std::vector<grk_packet_length_info>();
#endif
	return true;
}

/**
 * Allocates data memory for an encoding code block.
 * We actually allocate 2 more bytes than specified, and then offset data by +2.
 * This is done so that we can safely initialize the MQ coder pointer to data-1,
 * without risk of accessing uninitialized memory.
 */
bool grk_tcd_cblk_enc::alloc_data(size_t nominalBlockSize) {
	uint32_t l_data_size = (uint32_t) (nominalBlockSize * sizeof(uint32_t));
	if (l_data_size > data_size) {
		if (owns_data && actualData) {
			delete[] actualData;
		}
		actualData = new uint8_t[l_data_size + cblk_compressed_data_pad_left];
		actualData[0]=0;
		actualData[1]=0;

		data = actualData + cblk_compressed_data_pad_left;
		data_size = l_data_size;
		owns_data = true;
	}
	return true;
}

void grk_tcd_cblk_enc::cleanup() {
	if (owns_data && actualData) {
		delete[] actualData;
		actualData = nullptr;
		data = nullptr;
		owns_data = false;
	}

	if (layers) {
		grok_free(layers);
		layers = nullptr;
	}

	if (passes) {
		grok_free(passes);
		passes = nullptr;
	}
#ifdef DEBUG_LOSSLESS_T2
	delete packet_length_info;
	packet_length_info = nullptr;
#endif
}

bool grk_tcd_cblk_dec::alloc() {
	if (!segs) {
		segs = new grk_tcd_seg[default_numbers_segments];
		/*fprintf(stderr, "Allocate %d elements of code_block->data\n", default_numbers_segments * sizeof(grk_tcd_seg));*/

		numSegmentsAllocated = default_numbers_segments;

		/*fprintf(stderr, "Allocate 8192 elements of code_block->data\n");*/
		/*fprintf(stderr, "numSegmentsAllocated of code_block->data = %d\n", p_code_block->numSegmentsAllocated);*/

#ifdef DEBUG_LOSSLESS_T2
		packet_length_info = new std::vector<grk_packet_length_info>();
#endif
	} else {
		/* sanitize */
		auto l_segs = segs;
		uint32_t l_current_max_segs = numSegmentsAllocated;

		/* Note: since seg_buffers simply holds references to another data buffer,
		 we do not need to copy it  to the sanitized block  */
		seg_buffers.cleanup();
		init();
		segs = l_segs;
		numSegmentsAllocated = l_current_max_segs;
	}
	return true;
}

void grk_tcd_cblk_dec::init() {
	compressedData = grk_buf();
	grok_aligned_free(decoded_data);
	decoded_data = nullptr;
	segs = nullptr;
	x0 = 0;
	y0 = 0;
	x1 = 0;
	y1 = 0;
	numbps = 0;
	numlenbits = 0;
	numPassesInPacket = 0;
	numSegments = 0;
#ifdef DEBUG_LOSSLESS_T2
	included = 0;
#endif
	numSegmentsAllocated = 0;
}

void grk_tcd_cblk_dec::cleanup() {
	seg_buffers.cleanup();
	delete[] segs;
	segs = nullptr;
#ifdef DEBUG_LOSSLESS_T2
	delete packet_length_info;
	packet_length_info = nullptr;
#endif
	grok_aligned_free(decoded_data);
}

void grk_tcd_precinct::deleteTagTrees() {
	delete incltree;
	incltree = nullptr;
	delete imsbtree;
	imsbtree = nullptr;
}

void grk_tcd_precinct::initTagTrees() {

	// if l_current_precinct->cw == 0 or l_current_precinct->ch == 0, then the precinct has no code blocks, therefore 
	// no need for inclusion and msb tag trees
	if (cw > 0 && ch > 0) {
		if (!incltree) {
			try {
				incltree = new TagTree(cw, ch);
			} catch (std::exception &e) {
				GROK_WARN( "No incltree created.");
			}
		} else {
			if (!incltree->init(cw, ch)) {
				GROK_WARN(
						"Failed to re-initialize incltree.");
				delete incltree;
				incltree = nullptr;
			}
		}

		if (!imsbtree) {
			try {
				imsbtree = new TagTree(cw, ch);
			} catch (std::exception &e) {
				GROK_WARN( "No imsbtree created.");
			}
		} else {
			if (!imsbtree->init(cw, ch)) {
				GROK_WARN(
						"Failed to re-initialize imsbtree.");
				delete imsbtree;
				imsbtree = nullptr;
			}
		}
	}
}


void TileComponent::get_dimensions(grk_image *l_image,
		 grk_image_comp  *l_img_comp, uint32_t *l_size_comp, uint32_t *l_width,
		uint32_t *l_height, uint32_t *l_offset_x, uint32_t *l_offset_y,
		uint32_t *l_image_width, uint32_t *l_stride, uint64_t *l_tile_offset) {
	uint32_t l_remaining;
	*l_size_comp = l_img_comp->prec >> 3; /* (/8) */
	l_remaining = l_img_comp->prec & 7; /* (%8) */
	if (l_remaining) {
		*l_size_comp += 1;
	}

	if (*l_size_comp == 3) {
		*l_size_comp = 4;
	}

	*l_width = width();
	*l_height = height();
	*l_offset_x = ceildiv<uint32_t>(l_image->x0, l_img_comp->dx);
	*l_offset_y = ceildiv<uint32_t>(l_image->y0, l_img_comp->dy);
	*l_image_width = ceildiv<uint32_t>(l_image->x1 - l_image->x0,
			l_img_comp->dx);
	*l_stride = *l_image_width - *l_width;
	*l_tile_offset = (x0 - *l_offset_x)
			+ (uint64_t) (y0 - *l_offset_y) * *l_image_width;
}

bool TileComponent::create_buffer(bool isEncoder,
		bool irreversible,
		grk_image *output_image, uint32_t dx, uint32_t dy) {
	auto new_buffer = new TileBuffer();
	new_buffer->data = nullptr;
	new_buffer->reduced_tile_dim = grk_rect(x0, y0, x1, y1);
	new_buffer->reduced_image_dim = new_buffer->reduced_tile_dim;
	new_buffer->unreduced_tile_dim = unreduced_tile_dim;
	grk_rect max_image_dim = unreduced_tile_dim ;

	if (output_image) {
		// tile coordinates
		new_buffer->unreduced_image_dim = grk_rect(ceildiv<uint32_t>(output_image->x0, dx),
									ceildiv<uint32_t>(output_image->y0, dy),
									ceildiv<uint32_t>(output_image->x1, dx),
									ceildiv<uint32_t>(output_image->y1, dy));

		new_buffer->reduced_image_dim = new_buffer->unreduced_image_dim;

		max_image_dim = new_buffer->reduced_image_dim;

		new_buffer->reduced_image_dim.ceildivpow2(numresolutions - minimum_num_resolutions);

		/* clip output image to tile */
		new_buffer->reduced_tile_dim.clip(new_buffer->reduced_image_dim, &new_buffer->reduced_image_dim);
		new_buffer->unreduced_tile_dim.clip(new_buffer->unreduced_image_dim, &new_buffer->unreduced_image_dim);
	}

	/* for encode, we don't need to allocate resolutions */
	if (!isEncoder) {
		/* fill resolutions vector */
        int32_t max_num_res = numresolutions;
		TileBufferResolution *prev_res = nullptr;
		for (int32_t resno = (int32_t) (max_num_res - 1); resno >= 0; --resno) {
			uint32_t bandno;
			grk_tcd_resolution *tcd_res = resolutions + resno;
			TileBufferResolution *res = (TileBufferResolution*) grok_calloc(1,
					sizeof(TileBufferResolution));
			if (!res) {
				delete new_buffer;
				return false;
			}

			res->bounds.x = tcd_res->x1 - tcd_res->x0;
			res->bounds.y = tcd_res->y1 - tcd_res->y0;
			res->origin.x = tcd_res->x0;
			res->origin.y = tcd_res->y0;

			for (bandno = 0; bandno < tcd_res->numbands; ++bandno) {
				grk_tcd_band *band = tcd_res->bands + bandno;
				grk_rect band_rect;
				band_rect = grk_rect(band->x0, band->y0, band->x1, band->y1);

				res->band_region[bandno] =
						prev_res ? prev_res->band_region[bandno] : max_image_dim;
				if (resno > 0) {

					/*For next level down, E' = ceil((E-b)/2) where b in {0,1} identifies band
					 * see Chapter 11 of Taubman and Marcellin for more details
					 * */
					grk_pt shift;
					shift.x = -(int64_t)(band->bandno & 1);
					shift.y = -(int64_t)(band->bandno >> 1);

					res->band_region[bandno].pan(&shift);
					res->band_region[bandno].ceildivpow2(1);

					// boundary padding. This number is slightly larger than it should be theoretically,
					// but we want to make sure that we don't have bugs at the region boundaries
					res->band_region[bandno].grow(4);

				}
			}
			res->num_bands = tcd_res->numbands;
			new_buffer->resolutions.push_back(res);
			prev_res = res;
		}
	}
	delete buf;
	buf = new_buffer;

	return true;
}

}

