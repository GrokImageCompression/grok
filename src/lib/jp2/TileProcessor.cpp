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
#include <exception>
using namespace std;

namespace grk {


TileProcessor::TileProcessor(bool isDecoder) : m_tile_ind_to_dec(-1),
									  m_current_tile_number(0),
									  tp_pos(0),
									  m_current_poc_tile_part_number(0),
									  m_current_tile_part_number(0),
									  m_nb_tile_parts_correction_checked(0),
									  m_nb_tile_parts_correction(0),
									  tile_part_data_length(0),
									  m_tlm_start(0),
									  m_tlm_sot_offsets_buffer(nullptr),
									  m_tlm_sot_offsets_current(nullptr),
									  cur_totnum_tp(0),
									  cur_pino(0),
									  tile(nullptr),
									  image(nullptr),
									  current_plugin_tile(nullptr),
									  whole_tile_decoding(true),
									  m_marker_scratch(nullptr),
									  m_marker_scratch_size(0),
									  plt_marker(nullptr),
									  m_cp(nullptr),
									  m_tcp(nullptr),
									  m_tileno(0),
									  m_is_decoder(isDecoder)
{
	if (isDecoder){
		m_marker_scratch = (uint8_t*) grk_calloc(1,	default_header_size);
		if (!m_marker_scratch)
			throw std::runtime_error("Out of memory");
		m_marker_scratch_size = default_header_size;
	}
}

TileProcessor::~TileProcessor(){
	free_tile();
	grok_free(m_tlm_sot_offsets_buffer);
	grok_free(m_marker_scratch);
	delete plt_marker;
}

bool TileProcessor::set_decode_area(grk_j2k *p_j2k,
									grk_image *output_image,
									uint32_t start_x,
									uint32_t start_y,
									uint32_t end_x,
									uint32_t end_y) {

	auto cp = &(p_j2k->m_cp);
	auto image = p_j2k->m_private_image;
	auto decoder = &p_j2k->m_specific_param.m_decoder;

	/* Check if we have read the main header */
	if (decoder->m_state != J2K_DEC_STATE_TPHSOT) {
		GROK_ERROR(
				"Need to decode the main header before setting decode area");
		return false;
	}

	if (!start_x && !start_y && !end_x && !end_y) {
		//event_msg( EVT_INFO, "No decoded area parameters, set the decoded area to the whole image");

		decoder->m_start_tile_x_index = 0;
		decoder->m_start_tile_y_index = 0;
		decoder->m_end_tile_x_index = cp->tw;
		decoder->m_end_tile_y_index = cp->th;

		return true;
	}

	/* ----- */
	/* Check if the positions provided by the user are correct */

	/* Left */
	if (start_x > image->x1) {
		GROK_ERROR("Left position of the decoded area (region_x0=%d)"
				" is outside the image area (Xsiz=%d).\n",
				start_x, image->x1);
		return false;
	} else if (start_x < image->x0) {
		GROK_WARN(
				"Left position of the decoded area (region_x0=%d)"
				" is outside the image area (XOsiz=%d).\n",
				start_x, image->x0);
		decoder->m_start_tile_x_index = 0;
		output_image->x0 = image->x0;
	} else {
		decoder->m_start_tile_x_index = (start_x
				- cp->tx0) / cp->tdx;
		output_image->x0 = start_x;
	}

	/* Up */
	if (start_y > image->y1) {
		GROK_ERROR(
				"Up position of the decoded area (region_y0=%d)"
				" is outside the image area (Ysiz=%d).\n",
				start_y, image->y1);
		return false;
	} else if (start_y < image->y0) {
		GROK_WARN(
				"Up position of the decoded area (region_y0=%d)"
				" is outside the image area (YOsiz=%d).\n",
				start_y, image->y0);
		decoder->m_start_tile_y_index = 0;
		output_image->y0 = image->y0;
	} else {
		decoder->m_start_tile_y_index = (start_y
				- cp->ty0) / cp->tdy;
		output_image->y0 = start_y;
	}

	/* Right */
	assert(end_x > 0);
	assert(end_y > 0);
	if (end_x < image->x0) {
		GROK_ERROR(
				"Right position of the decoded area (region_x1=%d)"
				" is outside the image area (XOsiz=%d).\n",
				end_x, image->x0);
		return false;
	} else if (end_x > image->x1) {
		GROK_WARN(
				"Right position of the decoded area (region_x1=%d)"
				" is outside the image area (Xsiz=%d).\n",
				end_x, image->x1);
		decoder->m_end_tile_x_index = cp->tw;
		output_image->x1 = image->x1;
	} else {
		// avoid divide by zero
		if (cp->tdx == 0) {
			return false;
		}
		decoder->m_end_tile_x_index = ceildiv<uint32_t>(
				end_x - cp->tx0, cp->tdx);
		output_image->x1 = end_x;
	}

	/* Bottom */
	if (end_y < image->y0) {
		GROK_ERROR(
				"Bottom position of the decoded area (region_y1=%d)"
				" is outside the image area (YOsiz=%d).\n",
				end_y, image->y0);
		return false;
	}
	if (end_y > image->y1) {
		GROK_WARN(
				"Bottom position of the decoded area (region_y1=%d)"
				" is outside the image area (Ysiz=%d).\n",
				end_y, image->y1);
		decoder->m_end_tile_y_index = cp->th;
		output_image->y1 = image->y1;
	} else {
		// avoid divide by zero
		if (cp->tdy == 0) {
			return false;
		}
		decoder->m_end_tile_y_index = ceildiv<uint32_t>(
				end_y - cp->ty0, cp->tdy);
		output_image->y1 = end_y;
	}
	/* ----- */

	decoder->m_discard_tiles = 1;
	whole_tile_decoding = false;
	if (!update_image_dimensions(output_image, cp->m_coding_param.m_dec.m_reduce))
		return false;

	GROK_INFO("Setting decoding area to %d,%d,%d,%d\n",
	               output_image->x0, output_image->y0, output_image->x1, output_image->y1);
	return true;
}


/*
 if
 - r xx, yy, zz, 0   (disto_alloc == 1 and rates == 0)
 or
 - q xx, yy, zz, 0   (fixed_quality == 1 and distoratio == 0)

 then don't try to find an optimal threshold but rather take everything not included yet.

 It is possible to have some lossy layers and the last layer for sure lossless

 */
bool TileProcessor::layer_needs_rate_control(uint32_t layno) {

	auto enc_params = &m_cp->m_coding_param.m_enc;
	return ((enc_params->m_disto_alloc == 1) && (m_tcp->rates[layno] > 0.0))
			|| ((enc_params->m_fixed_quality == 1)
					&& (m_tcp->distoratio[layno] > 0.0f));
}

bool TileProcessor::needs_rate_control() {
	for (uint32_t i = 0; i < m_tcp->numlayers; ++i) {
		if (layer_needs_rate_control(i))
			return true;
	}
	return false;
}

// lossless in the sense that no code passes are removed; it mays still be a lossless layer
// due to irreversible DWT and quantization
bool TileProcessor::make_single_lossless_layer() {
	if (m_tcp->numlayers == 1
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
	auto tcd_tcp = m_tcp;

	tcd_tile->numpix = 0;
	uint32_t state = grk_plugin_get_debug_state();

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
						if (!(state & GRK_PLUGIN_STATE_PRE_TR1)) {
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
			auto t2 = new T2(image, m_cp);
			double distotarget = tcd_tile->distotile
					- ((K * maxSE)
							/ pow(10.0, tcd_tcp->distoratio[layno] / 10.0));

			for (uint32_t i = 0; i < 128; ++i) {
				uint32_t thresh = (lowerBound + upperBound) >> 1;
				if (prevthresh != 0 && prevthresh == thresh)
					break;
				makelayer_feasible(layno, (uint16_t) thresh, false);
				prevthresh = thresh;
				if (m_cp->m_coding_param.m_enc.m_fixed_quality) {
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
					if (!t2->encode_packets_simulate(m_tileno,
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

	double min_slope = DBL_MAX;
	double max_slope = -1;

	tile->numpix = 0;
	uint32_t state = grk_plugin_get_debug_state();

	bool single_lossless = make_single_lossless_layer();

	for (compno = 0; compno < tile->numcomps; compno++) {
		auto tilec = &tile->comps[compno];
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
						if (!(state & GRK_PLUGIN_STATE_PRE_TR1)) {
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
							tile->numpix += numPix;
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
	for (layno = 0; layno < m_tcp->numlayers; layno++) {
		if (layer_needs_rate_control(layno)) {
			double lowerBound = min_slope;
			uint64_t maxlen =
					m_tcp->rates[layno] > 0.0f ?
							std::min<uint64_t>(
									((uint64_t) ceil(m_tcp->rates[layno])),
									len) :
							len;

			/* Threshold for Marcela Index */
			// start by including everything in this layer
			double goodthresh = 0;

			// thresh from previous iteration - starts off uninitialized
			// used to bail out if difference with current thresh is small enough
			double prevthresh = -1;
			double distotarget = tile->distotile
					- ((K * maxSE)
							/ pow(10.0, m_tcp->distoratio[layno] / 10.0));

			auto t2 = new T2(image, m_cp);
			double thresh;
			for (uint32_t i = 0; i < 128; ++i) {
				thresh =
						(upperBound == -1) ?
								lowerBound : (lowerBound + upperBound) / 2;
				make_layer_simple(layno, thresh, false);
				if (prevthresh != -1 && (fabs(prevthresh - thresh)) < 0.001)
					break;
				prevthresh = thresh;
				if (m_cp->m_coding_param.m_enc.m_fixed_quality) {
					double distoachieved =
							layno == 0 ?
									tile->distolayer[0] :
									cumdisto[layno - 1]
											+ tile->distolayer[layno];

					if (distoachieved < distotarget) {
						upperBound = thresh;
						continue;
					}
					lowerBound = thresh;
				} else {
					if (!t2->encode_packets_simulate(m_tileno,
							tile, layno + 1, p_data_written, maxlen,
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
							tile->distolayer[0] :
							(cumdisto[layno - 1] + tile->distolayer[layno]);

			// upper bound for next layer will equal lowerBound for previous layer, minus one
			upperBound = lowerBound - 1;
		} else {
			makelayer_final(layno);
			// this has to be the last layer, so return
			assert(layno == m_tcp->numlayers - 1);
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
	tile->distolayer[layno] = 0;
	for (compno = 0; compno < tile->numcomps; compno++) {
		auto tilec = tile->comps + compno;
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

						tile->distolayer[layno] += layer->disto;
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
	tile->distolayer[layno] = 0;

	for (compno = 0; compno < tile->numcomps; compno++) {
		auto tilec = tile->comps + compno;
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
						tile->distolayer[layno] += layer->disto;
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
	image = p_image;
	m_cp = p_cp;

	tile = (grk_tcd_tile*) grk_calloc(1, sizeof(grk_tcd_tile));
	if (!tile) {
		return false;
	}

	tile->comps = new TileComponent[p_image->numcomps];
	tile->numcomps = p_image->numcomps;
	tp_pos = p_cp->m_coding_param.m_enc.m_tp_pos;
	return true;
}

/* ----------------------------------------------------------------------- */

inline bool TileProcessor::init_tile(uint16_t tile_no,
		grk_image *output_image, bool isEncoder) {
	uint32_t state = grk_plugin_get_debug_state();

	auto tcp = &(m_cp->tcps[tile_no]);
	if (tcp->m_tile_data)
		tcp->m_tile_data->rewind();

	uint32_t p = tile_no % m_cp->tw; /* tile coordinates */
	uint32_t q = tile_no / m_cp->tw;
	/*fprintf(stderr, "Tile coordinate = %d,%d\n", p, q);*/

	/* 4 borders of the tile rescale on the image if necessary */
	uint32_t tx0 = m_cp->tx0 + p * m_cp->tdx; /* can't be greater than image->x1 so won't overflow */
	tile->x0 = std::max<uint32_t>(tx0, image->x0);
	tile->x1 = std::min<uint32_t>(uint_adds(tx0, m_cp->tdx), image->x1);
	if (tile->x1 <= tile->x0) {
		GROK_ERROR( "Tile x coordinates are not valid");
		return false;
	}
	uint32_t ty0 = m_cp->ty0 + q * m_cp->tdy; /* can't be greater than image->y1 so won't overflow */
	tile->y0 = std::max<uint32_t>(ty0, image->y0);
	tile->y1 = std::min<uint32_t>(uint_adds(ty0, m_cp->tdy), image->y1);
	if (tile->y1 <= tile->y0) {
		GROK_ERROR( "Tile y coordinates are not valid");
		return false;
	}
	/*fprintf(stderr, "Tile border = %d,%d,%d,%d\n", tile->x0, tile->y0,tile->x1,tile->y1);*/

	/* testcase 1888.pdf.asan.35.988 */
	if (tcp->tccps->numresolutions == 0) {
		GROK_ERROR(
				"tiles require at least one resolution");
		return false;
	}

	for (uint32_t compno = 0; compno < tile->numcomps; ++compno) {
		auto image_comp = image->comps + compno;

		/*fprintf(stderr, "compno = %d/%d\n", compno, tile->numcomps);*/
		if (image_comp->dx == 0 || image_comp->dy == 0)
			return false;
		image_comp->resno_decoded = 0;

		auto tilec = tile->comps + compno;
		if (!tilec->init(isEncoder,
						whole_tile_decoding,
						output_image,
						m_cp,
						tcp,
						tile,
						image_comp,
						tcp->tccps + compno,
						current_plugin_tile)){
			return false;
		}
	} /* compno */

	// decoder plugin debug sanity check on tile struct
	if (!isEncoder) {
		if (state & GRK_PLUGIN_STATE_DEBUG) {
			if (!tile_equals(current_plugin_tile, tile)) {
				GROK_WARN("plugin tile differs from grok tile",
						nullptr);
			}
		}
	}
	tile->packno = 0;
	return true;
}

bool TileProcessor::init_encode_tile(uint16_t tile_no) {
	return init_tile(tile_no, nullptr, true);
}

bool TileProcessor::init_decode_tile(grk_image *output_image,
		uint16_t tile_no) {
	return init_tile(tile_no, output_image, false);

}

bool TileProcessor::encode_tile(uint16_t tile_no, BufferedStream *p_stream,
		uint64_t *p_data_written, uint64_t max_length,
		 grk_codestream_info  *p_cstr_info) {
	uint32_t state = grk_plugin_get_debug_state();
	if (m_current_tile_part_number == 0) {
		m_tileno = tile_no;
		m_tcp = &m_cp->tcps[tile_no];
		if (p_cstr_info) {
			uint32_t num_packs = 0;
			uint32_t i;
			auto tilec_idx = &tile->comps[0]; /* based on component 0 */
			auto tccp = m_tcp->tccps; /* based on component 0 */

			for (i = 0; i < tilec_idx->numresolutions; i++) {
				auto res_idx = &tilec_idx->resolutions[i];

				p_cstr_info->tile[tile_no].pw[i] = res_idx->pw;
				p_cstr_info->tile[tile_no].ph[i] = res_idx->ph;

				num_packs += res_idx->pw * res_idx->ph;
				p_cstr_info->tile[tile_no].pdx[i] = tccp->prcw[i];
				p_cstr_info->tile[tile_no].pdy[i] = tccp->prch[i];
			}
			p_cstr_info->tile[tile_no].packet =
					( grk_packet_info  * ) grk_calloc(
							(size_t) p_cstr_info->numcomps
									* (size_t) p_cstr_info->numlayers
									* num_packs, sizeof( grk_packet_info) );
			if (!p_cstr_info->tile[tile_no].packet) {
				GROK_ERROR(
						"tcd_encode_tile: Out of memory error when allocating packet memory");
				return false;
			}
		}
		if (state & GRK_PLUGIN_STATE_DEBUG) {
			set_context_stream(this);
		}

		// When debugging the encoder, we do all of T1 up to and including DWT in the plugin, and pass this in as image data.
		// This way, both Grok and plugin start with same inputs for context formation and MQ coding.
		bool debugEncode = state & GRK_PLUGIN_STATE_DEBUG;
		bool debugMCT = (state & GRK_PLUGIN_STATE_MCT_ONLY) ? true : false;

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
	m_tcp = m_cp->tcps + tile_no;

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
		  auto tilec = tile->comps + compno;

		  /* Compute the intersection of the area of interest, expressed in tile coordinates */
		  /* with the tile coordinates */
		  auto dims = tilec->buf->reduced_image_dim;
		  uint32_t win_x0 = max<uint32_t>(
							  tilec->x0,
							  (uint32_t)dims.x0);
		  uint32_t win_y0 = max<uint32_t>(
							  tilec->y0,
							  (uint32_t)dims.y0);
		  uint32_t win_x1 = min<uint32_t>(
							  tilec->x1,
							  (uint32_t)dims.x1);
		  uint32_t win_y1 = min<uint32_t>(
							  tilec->y1,
							  (uint32_t)dims.y1);
		  if (win_x1 < win_x0 ||  win_y1 < win_y0) {
			  /* We should not normally go there. The circumstance is when */
			  /* the tile coordinates do not intersect the area of interest */
			  /* Upper level logic should not even try to decode that tile */
			  GROK_ERROR("Invalid tilec->win_xxx values\n");
			  return false;
		  }

		  for (uint32_t resno = 0; resno < tilec->minimum_num_resolutions; ++resno) {
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
			|| (current_plugin_tile->decode_flags & GRK_DECODE_T2);
	bool doT1 = !current_plugin_tile
			|| (current_plugin_tile->decode_flags & GRK_DECODE_T1);
	bool doPostT1 = !current_plugin_tile
			|| (current_plugin_tile->decode_flags & GRK_DECODE_POST_T1);

	if (doT2) {
		uint64_t l_data_read = 0;
		if (!t2_decode(tile_no, src_buf, &l_data_read))
			return false;
		// synch plugin with T2 data
		decode_synch_plugin_with_host(this);
	}


	if (doT1) {
		for (uint32_t compno = 0; compno < tile->numcomps; ++compno) {
		   auto tilec = tile->comps + compno;
		   auto img_comp = image->comps + compno;
		   auto tccp = m_tcp->tccps + compno;

		   if (!whole_tile_decoding) {
			  try {
				 tilec->alloc_sparse_array(img_comp->resno_decoded + 1);
			  } catch (runtime_error &ex){
				  return false;
			  }
		   }
		    std::vector<decodeBlockInfo*> blocks;
			auto t1_wrap = std::unique_ptr<Tier1>(new Tier1());
			if (!t1_wrap->prepareDecodeCodeblocks(tilec, tccp, &blocks))
				return false;
			// !!! assume that code block dimensions do not change over components
			if (!t1_wrap->decodeCodeblocks(m_tcp,
					(uint16_t) m_tcp->tccps->cblkw,
					(uint16_t) m_tcp->tccps->cblkh, &blocks))
				return false;

		  if (doPostT1)
			  if (!Wavelet::decode(this, tilec, img_comp->resno_decoded + 1,tccp->qmfbid))
				  return false;

		  tilec->release_mem();
		}
	}

	if (doPostT1) {
		if (!mct_decode())
			return false;
		if (!dc_level_shift_decode())
			return false;
	}
	return true;
}

/*

 ///////////////////////////////////////////////////////////////////////////////////////
 ToDo: does the code below assume consistent resolution dimensions across components?
 Because of subsampling, I don't think
 we can assume this in the code, but need to check that code is aware of this.
 ////////////////////////////////////////////////////////////////////////////////////


 For each component, copy decoded resolutions from the tile data buffer
 into p_dest buffer.

 So, p_dest stores a sub-region of the tcd data, based on the number
 of resolutions decoded.

 Note: p_dest stores data in the actual precision of the decompressed image,
 vs. tile data buffer which is always 32 bits.


 */
bool TileProcessor::update_tile_data(uint8_t *p_dest,
		uint64_t dest_length) {
	uint64_t l_data_size = get_tile_size(true);
	if (l_data_size > dest_length)
		return false;

	for (uint32_t i = 0; i < image->numcomps; ++i) {
		auto tilec = tile->comps + i;
		auto img_comp = image->comps + i;
		uint32_t size_comp = (img_comp->prec + 7) >> 3;
		uint32_t stride = 0;
		uint32_t width = (uint32_t)tilec->buf->reduced_image_dim.width();
		uint32_t height = (uint32_t)tilec->buf->reduced_image_dim.height();
		const int32_t *src_ptr = tilec->buf->get_ptr( 0, 0, 0,0);

		switch (size_comp) {
		case 1: {
			auto dest_ptr = (int8_t*) p_dest;
			if (img_comp->sgnd) {
				for (uint32_t j = 0; j < height; ++j) {
					for (uint32_t k = 0; k < width; ++k)
						*(dest_ptr++) = (int8_t) (*(src_ptr++));
					src_ptr += stride;
				}
			} else {
				for (uint32_t j = 0; j < height; ++j) {
					for (uint32_t k = 0; k < width; ++k)
						*(dest_ptr++) = (int8_t) ((*(src_ptr++)) & 0xff);
					src_ptr += stride;
				}
			}
			p_dest = (uint8_t*) dest_ptr;
		}
			break;
		case 2: {
			auto dest_ptr = (int16_t*) p_dest;
			if (img_comp->sgnd) {
				for (uint32_t j = 0; j < height; ++j) {
					for (uint32_t k = 0; k < width; ++k)
						*(dest_ptr++) = (int16_t) (*(src_ptr++));
					src_ptr += stride;
				}
			} else {
				for (uint32_t j = 0; j < height; ++j) {
					for (uint32_t k = 0; k < width; ++k)
						//cast and mask to avoid sign extension
						*(dest_ptr++) = (int16_t) ((*(src_ptr++)) & 0xffff);
					src_ptr += stride;
				}
			}
			p_dest = (uint8_t*) dest_ptr;
		}
			break;
		}
	}

	return true;
}

void TileProcessor::free_tile() {
	if (!tile)
		return;
	delete[] tile->comps;
	tile->comps = nullptr;
	grok_free(tile);
	tile = nullptr;
}

void TileProcessor::copy_image_to_tile() {
	for (uint32_t i = 0; i < image->numcomps; ++i) {
		auto tilec = tile->comps + i;
		auto img_comp = image->comps + i;
		uint32_t size_comp, width, height, offset_x, offset_y,
				image_width, stride;
		uint64_t tile_offset;

		tilec->get_dimensions(image, img_comp, &size_comp,
				&width, &height, &offset_x, &offset_y, &image_width,
				&stride, &tile_offset);
		auto src_ptr = img_comp->data + tile_offset;
		auto dest_ptr = tilec->buf->data;

		for (uint32_t j = 0; j < height; ++j) {
			memcpy(dest_ptr,src_ptr,width*sizeof(int32_t));
			src_ptr += stride + width;
			dest_ptr += width;
		}
	}
}
bool TileProcessor::t2_decode(uint16_t tile_no, ChunkBuffer *src_buf,
		uint64_t *p_data_read) {
	auto t2 = new T2(image, m_cp);

	if (!t2->decode_packets(tile_no, tile, src_buf, p_data_read)) {
		delete t2;
		return false;
	}
	delete t2;

	return true;
}

bool TileProcessor::mct_decode() {
	auto tile_comp = tile->comps;
	uint64_t i;

	if (!m_tcp->mct) {
		return true;
	}

	uint64_t image_samples  = (uint64_t)tile_comp->buf->reduced_image_dim.area();
	uint64_t samples = image_samples;

	if (tile->numcomps >= 3) {
		/* testcase 1336.pdf.asan.47.376 */
		if ((uint64_t)tile->comps[0].buf->reduced_image_dim.area() < image_samples
				|| (uint64_t)tile->comps[1].buf->reduced_image_dim.area()
						< image_samples
				|| (uint64_t)tile->comps[2].buf->reduced_image_dim.area()< image_samples) {
			GROK_ERROR(
					"Tiles don't all have the same dimension. Skip the MCT step.");
			return false;
		} else if (m_tcp->mct == 2) {
			if (!m_tcp->m_mct_decoding_matrix)
				return true;
			auto data = (uint8_t**) grk_malloc(
					tile->numcomps * sizeof(uint8_t*));
			if (!data)
				return false;

			for (i = 0; i < tile->numcomps; ++i) {
				data[i] = (uint8_t*) tile_comp->buf->get_ptr( 0, 0,
						0, 0);
				++tile_comp;
			}

			if (!mct::decode_custom(/* MCT data */
									(uint8_t*) m_tcp->m_mct_decoding_matrix,
									/* size of components */
									samples,
									/* components */
									data,
									/* nb of components (i.e. size of pData) */
									tile->numcomps,
									/* tells if the data is signed */
									image->comps->sgnd)) {
				grok_free(data);
				return false;
			}

			grok_free(data);
		} else {
			if (m_tcp->tccps->qmfbid == 1) {
				mct::decode_rev(tile->comps[0].buf->get_ptr( 0, 0, 0, 0),
						tile->comps[1].buf->get_ptr(0, 0, 0, 0),
						tile->comps[2].buf->get_ptr(0, 0, 0, 0),
						samples);
			} else {
				mct::decode_irrev(
						(float*) tile->comps[0].buf->get_ptr( 0, 0, 0,
								0),
						(float*) tile->comps[1].buf->get_ptr(0, 0, 0,
								0),
						(float*) tile->comps[2].buf->get_ptr( 0, 0, 0,
								0), samples);
			}
		}
	} else {
		GROK_ERROR(
				"Number of components (%d) is inconsistent with a MCT. Skip the MCT step.\n",
				tile->numcomps);
	}

	return true;
}

bool TileProcessor::dc_level_shift_decode() {
	uint32_t compno = 0;
	for (compno = 0; compno < tile->numcomps; compno++) {
		int32_t min = INT32_MAX, max = INT32_MIN;
		uint32_t x0 ;
		uint32_t y0;
		uint32_t x1;
		uint32_t y1 ;
		auto tile_comp = tile->comps + compno;
		auto tccp = m_tcp->tccps + compno;
		auto img_comp = image->comps + compno;
		uint32_t stride = 0;
		auto current_ptr = tile_comp->buf->get_ptr( 0, 0, 0, 0);

		x0 = 0;
		y0 = 0;
		x1 = (uint32_t)(tile_comp->buf->reduced_image_dim.width());
		y1 = (uint32_t)(tile_comp->buf->reduced_image_dim.height());

		assert(x1 >= x0);
		assert(tile_comp->width() >= (x1 - x0));


		if (img_comp->sgnd) {
			min = -(1 << (img_comp->prec - 1));
			max = (1 << (img_comp->prec - 1)) - 1;
		} else {
			min = 0;
			max = (1 << img_comp->prec) - 1;
		}

		if (tccp->qmfbid == 1) {
			for (uint32_t j = y0; j < y1; ++j) {
				for (uint32_t i = x0; i < x1; ++i) {
					*current_ptr = int_clamp(
							*current_ptr + tccp->m_dc_level_shift, min,
							max);
					current_ptr++;
				}
				current_ptr += stride;
			}
		} else {
			for (uint32_t j = y0; j < y1; ++j) {
				for (uint32_t i = x0; i < x1; ++i) {
					float value = *((float*) current_ptr);
					*current_ptr = int_clamp(
							(int32_t) grok_lrintf(value)
									+ tccp->m_dc_level_shift, min, max);
					current_ptr++;
				}
				current_ptr += stride;
			}
		}
	}
	return true;
}


uint64_t TileProcessor::get_tile_size(bool reduced) {
	uint64_t data_size = 0;
	for (uint32_t i = 0; i < image->numcomps; ++i) {
		auto tilec = tile->comps + i;
		auto img_comp = image->comps + i;
		uint32_t size_comp = (img_comp->prec + 7) >> 3; /*(/ 8)*/

		data_size += size_comp *
				(reduced ? (uint64_t)tilec->buf->reduced_image_dim.area() : tilec->area());
	}

	return data_size;
}

bool TileProcessor::dc_level_shift_encode() {
	for (uint32_t compno = 0; compno < tile->numcomps; compno++) {
		auto tile_comp = tile->comps + compno;
		auto tccp = m_tcp->tccps + compno;
		auto current_ptr = tile_comp->buf->get_ptr( 0, 0, 0, 0);
		uint64_t nb_elem = tile_comp->area();

		if (tccp->qmfbid == 1) {
			for (uint64_t i = 0; i < nb_elem; ++i) {
				*current_ptr -= tccp->m_dc_level_shift;
				++current_ptr;
			}
		} else {
			for (uint64_t i = 0; i < nb_elem; ++i) {
				*current_ptr = (*current_ptr - tccp->m_dc_level_shift)
						* (1 << 11);
				++current_ptr;
			}
		}
	}

	return true;
}

bool TileProcessor::mct_encode() {
	auto tile_comp = tile->comps;
	uint64_t samples = tile_comp->area();
	uint32_t i;

	if (!m_tcp->mct)
		return true;
	if (m_tcp->mct == 2) {
		if (!m_tcp->m_mct_coding_matrix)
			return true;
		auto data = (uint8_t**) grk_malloc(tile->numcomps * sizeof(uint8_t*));
		if (!data)
			return false;
		for (i = 0; i < tile->numcomps; ++i) {
			data[i] = (uint8_t*) tile_comp->buf->get_ptr( 0, 0, 0,
					0);
			++tile_comp;
		}

		if (!mct::encode_custom(/* MCT data */
		(uint8_t*) m_tcp->m_mct_coding_matrix,
		/* size of components */
		samples,
		/* components */
		data,
		/* nb of components (i.e. size of pData) */
		tile->numcomps,
		/* tells if the data is signed */
		image->comps->sgnd)) {
			grok_free(data);
			return false;
		}

		grok_free(data);
	} else if (m_tcp->tccps->qmfbid == 0) {
		mct::encode_irrev(tile->comps[0].buf->get_ptr( 0, 0, 0, 0),
				tile->comps[1].buf->get_ptr( 0, 0, 0, 0),
				tile->comps[2].buf->get_ptr( 0, 0, 0, 0), samples);
	} else {
		mct::encode_rev(tile->comps[0].buf->get_ptr( 0, 0, 0, 0),
				tile->comps[1].buf->get_ptr(0, 0, 0, 0),
				tile->comps[2].buf->get_ptr(0, 0, 0, 0), samples);
	}

	return true;
}

bool TileProcessor::dwt_encode() {
	uint32_t compno = 0;
	bool rc = true;
	for (compno = 0; compno < (int64_t) tile->numcomps; ++compno) {
		auto tile_comp = tile->comps + compno;
		auto tccp = m_tcp->tccps + compno;
		if (!Wavelet::encode(tile_comp, tccp->qmfbid)) {
			rc = false;
			continue;

		}
	}
	return rc;
}

bool TileProcessor::t1_encode() {
	const double *l_mct_norms;
	uint32_t l_mct_numcomps = 0U;
	auto l_tcp = m_tcp;

	if (l_tcp->mct == 1) {
		l_mct_numcomps = 3U;
		/* irreversible encoding */
		if (l_tcp->tccps->qmfbid == 0) {
			l_mct_norms = mct::get_norms_irrev();
		} else {
			l_mct_norms = mct::get_norms_rev();
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

	auto l_t2 = new T2(image, m_cp);
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

	if (!l_t2->encode_packets(m_tileno, tile,
			m_tcp->numlayers, p_stream, p_data_written, max_dest_size,
			p_cstr_info, m_current_poc_tile_part_number, tp_pos, cur_pino)) {
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
	auto l_cp = m_cp;
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
		auto reduce = m_cp->m_coding_param.m_dec.m_reduce;
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
			offset_x0_dest = (uint32_t)(src_dim.x0 - x0_dest);
			offset_x0_src = 0;

			if (x1_dest >= src_dim.x1) {
				width_dest = width_src;
				offset_x1_src = 0;
			} else {
				width_dest = (uint32_t)(x1_dest - src_dim.x0);
				offset_x1_src = (width_src - width_dest);
			}
		} else {
			offset_x0_dest = 0U;
			offset_x0_src = (uint32_t)(x0_dest - src_dim.x0);

			if (x1_dest >= src_dim.x1) {
				width_dest = (uint32_t)(width_src - offset_x0_src);
				offset_x1_src = 0;
			} else {
				width_dest = img_comp_dest->w;
				offset_x1_src = (uint32_t)(src_dim.x1 - x1_dest);
			}
		}

		if (y0_dest < src_dim.y0) {
			offset_y0_dest = (uint32_t)(src_dim.y0 - y0_dest);
			offset_y0_src = 0;

			if (y1_dest >= src_dim.y1) {
				height_dest = height_src;
				offset_y1_src = 0;
			} else {
				height_dest = (uint32_t)(y1_dest - src_dim.y0);
				offset_y1_src = (height_src - height_dest);
			}
		} else {
			offset_y0_dest = 0U;
			offset_y0_src = (uint32_t)(y0_dest - src_dim.y0);

			if (y1_dest >= src_dim.y1) {
				height_dest = (uint32_t)(height_src - offset_y0_src);
				offset_y1_src = 0;
			} else {
				height_dest = img_comp_dest->h;
				offset_y1_src = (uint32_t)(src_dim.y1 - y1_dest);
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
			int8_t *src_ptr = (int8_t*) p_data;
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
		}
	}

	return true;
}

bool TileProcessor::copy_image_data_to_tile(uint8_t *p_src, uint64_t src_length) {
	uint64_t i, j;
	uint32_t size_comp;
	uint64_t nb_elem;
	uint64_t data_size = get_tile_size(false);
	if (!p_src || (data_size != src_length)) {
		return false;
	}

	for (i = 0; i < image->numcomps; ++i) {
		auto tilec = tile->comps + i;
		auto img_comp = image->comps + i;

		size_comp = (img_comp->prec + 7) >> 3; /*(/ 8)*/
		nb_elem = tilec->area();
		switch (size_comp) {
		case 1: {
			auto dest_ptr = tilec->buf->data;
			if (img_comp->sgnd) {
				auto src_ptr = (int8_t*) p_src;
				for (j = 0; j < nb_elem; ++j)
					*dest_ptr++ = *src_ptr++;
				p_src = (uint8_t*) src_ptr;
			} else {
				auto src_ptr = (uint8_t*) p_src;
				for (j = 0; j < nb_elem; ++j)
					*dest_ptr++ = *src_ptr++;
				p_src = src_ptr;
			}
		}
			break;
		case 2: {
			int32_t *dest_ptr = tilec->buf->data;
			if (img_comp->sgnd) {
				auto src_ptr = (int16_t*) p_src;
				for (j = 0; j < nb_elem; ++j)
					*dest_ptr++ = *src_ptr++;
				p_src = (uint8_t*) src_ptr;
			} else {
				auto src_ptr = (uint16_t*) p_src;
				for (j = 0; j < nb_elem; ++j)
					*dest_ptr++ = *src_ptr++;
				p_src = (uint8_t*) src_ptr;
			}
		}
			break;
		}
	}
	return true;
}

grk_tcd_cblk_enc::~grk_tcd_cblk_enc() {
	cleanup();
}
bool grk_tcd_cblk_enc::alloc() {
	if (!layers) {
		/* no memset since data */
		layers = (grk_tcd_layer*) grk_calloc(100, sizeof(grk_tcd_layer));
		if (!layers) {
			return false;
		}
	}
	if (!passes) {
		passes = (grk_tcd_pass*) grk_calloc(100, sizeof(grk_tcd_pass));
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

}

