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


namespace grk {


/* ----------------------------------------------------------------------- */


/**
 * Initializes tile coding/decoding
 */
static inline bool tcd_init_tile(tcd_t *p_tcd,
                                     uint32_t p_tile_no,
                                     opj_image_t* output_image,
                                     bool isEncoder,
                                     float fraction,
                                     size_t sizeof_block,
                                     event_mgr_t* manager);



/**
 * Deallocates the decoding data of the given precinct.
 */
static void tcd_code_block_dec_deallocate (tcd_precinct_t * p_precinct);


/**
 * Deallocates the encoding data of the given precinct.
 */
static void tcd_code_block_enc_deallocate (tcd_precinct_t * p_precinct);


/**
Free the memory allocated for encoding
@param tcd TCD handle
*/
static void tcd_free_tile(tcd_t *tcd);


static bool tcd_t2_decode ( tcd_t *p_tcd,
								uint32_t p_tile_no,
                                seg_buf_t* src_buf,
                                uint64_t * p_data_read,
                                event_mgr_t *p_manager);

static bool tcd_t1_decode (tcd_t *p_tcd, event_mgr_t *p_manager);

static bool tcd_dwt_decode (tcd_t *p_tcd);

static bool tcd_mct_decode (tcd_t *p_tcd, event_mgr_t *p_manager);

static bool tcd_dc_level_shift_decode (tcd_t *p_tcd);


static bool tcd_dc_level_shift_encode ( tcd_t *p_tcd );

static bool tcd_mct_encode ( tcd_t *p_tcd );

static bool tcd_dwt_encode ( tcd_t *p_tcd );

static bool tcd_t1_encode ( tcd_t *p_tcd );

static bool tcd_t2_encode (     tcd_t *p_tcd,
									GrokStream* p_stream,
									uint64_t * p_data_written,
                                    uint64_t p_max_dest_size,
                                    opj_codestream_info_t *p_cstr_info,
									event_mgr_t * p_manager);

static bool tcd_rate_allocate_encode(   tcd_t *p_tcd,
											uint64_t p_max_dest_size,
											opj_codestream_info_t *p_cstr_info,
											event_mgr_t * p_manager);

static bool tcd_layer_needs_rate_control(uint32_t layno, tcp_t *tcd_tcp, encoding_param_t* enc_params);

static bool tcd_make_single_lossless_layer(tcd_t *tcd);

static void tcd_makelayer_final(tcd_t *tcd,
	uint32_t layno);


static bool tcd_pcrd_bisect_simple(tcd_t *tcd,
	uint64_t * p_data_written,
	uint64_t len,
	event_mgr_t * p_manager);

static void tcd_make_layer_simple(tcd_t *tcd,
	uint32_t layno,
	double thresh,
	bool final);

static bool tcd_pcrd_bisect_feasible(tcd_t *tcd,
	uint64_t * p_data_written,
	uint64_t len,
	event_mgr_t * p_manager);


static void tcd_makelayer_feasible(tcd_t *tcd,
	uint32_t layno,
	uint16_t thresh,
	bool final);

/* ----------------------------------------------------------------------- */

/**
Create a new TCD handle
*/
tcd_t* tcd_create(bool p_is_decoder)
{
    tcd_t *l_tcd = nullptr;
    /* create the tcd structure */
    l_tcd = (tcd_t*) grok_calloc(1,sizeof(tcd_t));
    if (!l_tcd) {
        return nullptr;
    }
    l_tcd->m_is_decoder = p_is_decoder ? 1 : 0;
    return l_tcd;
}

/*
if
- r xx, yy, zz, 0   (disto_alloc == 1 and rates == 0)
or
- q xx, yy, zz, 0   (fixed_quality == 1 and distoratio == 0)

then don't try to find an optimal threshold but rather take everything not included yet.

It is possible to have some lossy layers and the last layer for sure lossless

*/
bool tcd_layer_needs_rate_control(uint32_t layno, tcp_t *tcd_tcp, encoding_param_t* enc_params) {

	return ((enc_params->m_disto_alloc == 1) && (tcd_tcp->rates[layno] > 0.0)) ||
		((enc_params->m_fixed_quality == 1) && (tcd_tcp->distoratio[layno] > 0.0f));
}

bool tcd_needs_rate_control(tcp_t *tcd_tcp, encoding_param_t* enc_params) {
	for (uint32_t i = 0; i < tcd_tcp->numlayers; ++i) {
		if (tcd_layer_needs_rate_control(i, tcd_tcp, enc_params))
			return true;
	}
	return false;
}

// lossless in the sense that no code passes are removed; it mays still be a lossless layer
// due to irreversible DWT and quantization
bool tcd_make_single_lossless_layer(tcd_t *tcd) {
	if (tcd->tcp->numlayers == 1 && !tcd_layer_needs_rate_control(0, tcd->tcp, &tcd->cp->m_specific_param.m_enc)) {
		tcd_makelayer_final(tcd, 0);
		return true;
	}
	return false;
}

void tcd_makelayer_feasible(tcd_t *tcd,
								uint32_t layno,
								uint16_t thresh,
								bool final)
{
	uint32_t compno, resno, bandno, precno, cblkno;
	uint32_t passno;
	tcd_tile_t *tcd_tile = tcd->tile;
	tcd_tile->distolayer[layno] = 0;
	for (compno = 0; compno < tcd_tile->numcomps; compno++) {
		tcd_tilecomp_t *tilec = tcd_tile->comps + compno;
		for (resno = 0; resno < tilec->numresolutions; resno++) {
			tcd_resolution_t *res = tilec->resolutions + resno;
			for (bandno = 0; bandno < res->numbands; bandno++) {
				tcd_band_t *band = res->bands + bandno;

				for (precno = 0; precno < res->pw * res->ph; precno++) {
					tcd_precinct_t *prc = band->precincts + precno;

					for (cblkno = 0; cblkno < prc->cw * prc->ch; cblkno++) {
						tcd_cblk_enc_t *cblk = prc->cblks.enc + cblkno;
						tcd_layer_t *layer = cblk->layers + layno;
						uint32_t cumulative_included_passes_in_block;

						if (layno == 0) {
							cblk->num_passes_included_in_previous_layers = 0;
						}

						cumulative_included_passes_in_block =
							cblk->num_passes_included_in_previous_layers;

						for (passno = cblk->num_passes_included_in_previous_layers;
							passno < cblk->num_passes_encoded; passno++) {
							tcd_pass_t *pass = &cblk->passes[passno];

							//truncate or include feasible, otherwise ignore
							if (pass->slope) {
								if (pass->slope <= thresh)
									break;
								cumulative_included_passes_in_block = passno + 1;
							}
						}

						layer->numpasses =
							cumulative_included_passes_in_block - cblk->num_passes_included_in_previous_layers;

						if (!layer->numpasses) {
							layer->disto = 0;
							continue;
						}

						// update layer
						if (cblk->num_passes_included_in_previous_layers == 0) {
							layer->len = cblk->passes[cumulative_included_passes_in_block - 1].rate;
							layer->data = cblk->data;
							layer->disto =	cblk->passes[cumulative_included_passes_in_block - 1].distortiondec;
						}
						else {
							layer->len =
								cblk->passes[cumulative_included_passes_in_block - 1].rate - cblk->passes[cblk->num_passes_included_in_previous_layers - 1].rate;
							layer->data =	cblk->data + cblk->passes[cblk->num_passes_included_in_previous_layers - 1].rate;
							layer->disto =	
								cblk->passes[cumulative_included_passes_in_block - 1].distortiondec - cblk->passes[cblk->num_passes_included_in_previous_layers - 1].distortiondec;
						}

						tcd_tile->distolayer[layno] += layer->disto;
						if (final)
							cblk->num_passes_included_in_previous_layers = cumulative_included_passes_in_block;
					}
				}
			}
		}
	}
}

/*
Hybrid rate control using bisect algorithm with optimal truncation points
*/
bool tcd_pcrd_bisect_feasible(tcd_t *tcd,
	uint64_t * p_data_written,
	uint64_t len,
	event_mgr_t * p_manager)
{

	bool single_lossless = tcd_make_single_lossless_layer(tcd);
	double cumdisto[100];
	const double K = 1;
	double maxSE = 0;

	cp_t *cp = tcd->cp;
	tcd_tile_t *tcd_tile = tcd->tile;
	tcp_t *tcd_tcp = tcd->tcp;

	tcd_tile->numpix = 0;
	uint32_t state = grok_plugin_get_debug_state();

	RateInfo rateInfo;
	for (uint32_t compno = 0; compno < tcd_tile->numcomps; compno++) {
		tcd_tilecomp_t *tilec = &tcd_tile->comps[compno];
		tilec->numpix = 0;
		for (uint32_t resno = 0; resno < tilec->numresolutions; resno++) {
			tcd_resolution_t *res = &tilec->resolutions[resno];

			for (uint32_t bandno = 0; bandno < res->numbands; bandno++) {
				tcd_band_t *band = &res->bands[bandno];

				for (uint32_t precno = 0; precno < res->pw * res->ph; precno++) {
					tcd_precinct_t *prc = &band->precincts[precno];

					for (uint32_t cblkno = 0; cblkno < prc->cw * prc->ch; cblkno++) {
						tcd_cblk_enc_t *cblk = &prc->cblks.enc[cblkno];

						uint32_t numPix = ((cblk->x1 - cblk->x0) * (cblk->y1 - cblk->y0));
						if (!(state &GROK_PLUGIN_STATE_PRE_TR1)) {
							encode_synch_with_plugin(tcd,
								compno,
								resno,
								bandno,
								precno,
								cblkno,
								band,
								cblk,
								&numPix);
						}

						if (!single_lossless) {
							RateControl::convexHull(cblk->passes, cblk->num_passes_encoded);
							rateInfo.synch(cblk);

							tcd_tile->numpix += numPix;
							tilec->numpix += numPix;
						}
					} /* cbklno */
				} /* precno */
			} /* bandno */
		} /* resno */

		if (!single_lossless) {
			maxSE += (double)(((uint64_t)1 << tcd->image->comps[compno].prec) - 1)
				* (double)(((uint64_t)1 << tcd->image->comps[compno].prec) - 1)
				* (double)tilec->numpix;
		}
	} /* compno */

	if (single_lossless) {
		tcd_makelayer_final(tcd, 0);
		return true;
	}

	uint32_t min_slope = rateInfo.getMinimumThresh();
	uint32_t max_slope = USHRT_MAX;

	uint32_t upperBound = max_slope;
	for (uint32_t layno = 0; layno < tcd_tcp->numlayers; layno++) {
		uint32_t lowerBound = min_slope;
		uint64_t maxlen = tcd_tcp->rates[layno] > 0.0f ? std::min<uint64_t>(((uint64_t)ceil(tcd_tcp->rates[layno])), len) : len;

		/* Threshold for Marcela Index */
		// start by including everything in this layer
		uint32_t goodthresh = 0;
		// thresh from previous iteration - starts off uninitialized
		// used to bail out if difference with current thresh is small enough
		uint32_t prevthresh = 0;
		if (tcd_layer_needs_rate_control(layno, tcd_tcp, &cp->m_specific_param.m_enc)) {
			t2_t*t2 = t2_create(tcd->image, cp);
			if (t2 == nullptr) {
				return false;
			}
			double distotarget =
				tcd_tile->distotile - ((K * maxSE) / pow(10.0, tcd_tcp->distoratio[layno] / 10.0));

			uint32_t thresh;
			for (uint32_t i = 0; i < 128; ++i) {
				thresh = (lowerBound + upperBound) >> 1;
				if (prevthresh != 0 && prevthresh ==thresh)
					break;
				tcd_makelayer_feasible(tcd, layno, (uint16_t)thresh, false);
				prevthresh = thresh;
				if (cp->m_specific_param.m_enc.m_fixed_quality) {
					double distoachieved =
						layno == 0 ?
						tcd_tile->distolayer[0] :
						cumdisto[layno - 1] + tcd_tile->distolayer[layno];

					if (distoachieved < distotarget) {
						upperBound = thresh;
						continue;
					}
					lowerBound = thresh;
				}
				else {
					if (!t2_encode_packets_simulate(t2,
						tcd->tcd_tileno,
						tcd_tile,
						layno + 1,
						p_data_written,
						maxlen,
						tcd->tp_pos, 
						p_manager)) {
						lowerBound = thresh;
						continue;
					}
					upperBound = thresh;
				}
			}
			// choose conservative value for goodthresh
			goodthresh = upperBound;
			t2_destroy(t2);

			tcd_makelayer_feasible(tcd, layno, (uint16_t)goodthresh, true);
			cumdisto[layno] =
				(layno == 0) ?
				tcd_tile->distolayer[0] :
				(cumdisto[layno - 1] + tcd_tile->distolayer[layno]);
			// upper bound for next layer is initialized to lowerBound for current layer, minus one
			upperBound = lowerBound - 1;;
		}
		else {
			tcd_makelayer_final(tcd, layno);
		}
	}
	return true;
}

/*
Simple bisect algorithm to calculate optimal layer truncation points
*/
bool tcd_pcrd_bisect_simple(  tcd_t *tcd,
                            uint64_t * p_data_written,
                            uint64_t len,
							event_mgr_t * p_manager)
{
    uint32_t compno, resno, bandno, precno, cblkno, layno;
    uint32_t passno;
    double cumdisto[100];     
    const double K = 1;              
    double maxSE = 0;

    cp_t *cp = tcd->cp;
    tcd_tile_t *tcd_tile = tcd->tile;
    tcp_t *tcd_tcp = tcd->tcp;

	double min_slope = DBL_MAX;
	double max_slope = -1;

    tcd_tile->numpix = 0;         
	uint32_t state = grok_plugin_get_debug_state();

	bool single_lossless = tcd_make_single_lossless_layer(tcd);

    for (compno = 0; compno < tcd_tile->numcomps; compno++) {
        tcd_tilecomp_t *tilec = &tcd_tile->comps[compno];
        tilec->numpix = 0;
        for (resno = 0; resno < tilec->numresolutions; resno++) {
            tcd_resolution_t *res = &tilec->resolutions[resno];
            for (bandno = 0; bandno < res->numbands; bandno++) {
                tcd_band_t *band = &res->bands[bandno];
                for (precno = 0; precno < res->pw * res->ph; precno++) {
                    tcd_precinct_t *prc = &band->precincts[precno];
                    for (cblkno = 0; cblkno < prc->cw * prc->ch; cblkno++) {
                        tcd_cblk_enc_t *cblk = &prc->cblks.enc[cblkno];
						uint32_t numPix = ((cblk->x1 - cblk->x0) * (cblk->y1 - cblk->y0));
						if (!(state &GROK_PLUGIN_STATE_PRE_TR1)) {
							encode_synch_with_plugin(tcd,
								compno,
								resno,
								bandno,
								precno,
								cblkno,
								band,
								cblk,
								&numPix);
						}

						if (!single_lossless) {
							for (passno = 0; passno < cblk->num_passes_encoded; passno++) {
								tcd_pass_t *pass = &cblk->passes[passno];
								int32_t dr;
								double dd, rdslope;

								if (passno == 0) {
									dr = (int32_t)pass->rate;
									dd = pass->distortiondec;
								}
								else {
									dr = (int32_t)(pass->rate - cblk->passes[passno - 1].rate);
									dd = pass->distortiondec - cblk->passes[passno - 1].distortiondec;
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
			maxSE += (double)(((uint64_t)1 << tcd->image->comps[compno].prec) - 1)
				* (double)(((uint64_t)1 << tcd->image->comps[compno].prec) - 1)
				* (double)tilec->numpix;

    } /* compno */

	if (single_lossless) {
		return true;
	}

	double upperBound = max_slope;
	for (layno = 0; layno < tcd_tcp->numlayers; layno++) {
		if (tcd_layer_needs_rate_control(layno, tcd_tcp, &cp->m_specific_param.m_enc)) {
			double lowerBound = min_slope;
			uint64_t maxlen = tcd_tcp->rates[layno] > 0.0f ? std::min<uint64_t>(((uint64_t)ceil(tcd_tcp->rates[layno])), len) : len;

			/* Threshold for Marcela Index */
			// start by including everything in this layer
			double goodthresh = 0;

			// thresh from previous iteration - starts off uninitialized
			// used to bail out if difference with current thresh is small enough
			double prevthresh = -1;
			double distotarget =
				tcd_tile->distotile - ((K * maxSE) / pow(10.0, tcd_tcp->distoratio[layno] / 10.0));

			t2_t*t2 = t2_create(tcd->image, cp);
			if (t2 == nullptr) {
				return false;
			}
			double thresh;
			for (uint32_t i = 0; i < 128; ++i) {
				thresh = (upperBound == -1) ? lowerBound : (lowerBound + upperBound) / 2;
				tcd_make_layer_simple(tcd, layno, thresh, false);
				if (prevthresh != -1 && (fabs(prevthresh - thresh)) < 0.001)
					break;
				prevthresh = thresh;
				if (cp->m_specific_param.m_enc.m_fixed_quality) {
					double distoachieved =	layno == 0 ?
												tcd_tile->distolayer[0] :
													cumdisto[layno - 1] + tcd_tile->distolayer[layno];

					if (distoachieved < distotarget) {
						upperBound = thresh;
						continue;
					}
					lowerBound = thresh;
				}
				else {
					if (!t2_encode_packets_simulate(t2,
														tcd->tcd_tileno,
														tcd_tile,
														layno + 1,
														p_data_written,
														maxlen,
														tcd->tp_pos,
														p_manager)) {
						lowerBound = thresh;
						continue;
					}
					upperBound = thresh;
				}
			}
			// choose conservative value for goodthresh
			goodthresh = (upperBound == -1) ? thresh : upperBound;
			t2_destroy(t2);

			tcd_make_layer_simple(tcd, layno, goodthresh, true);
			cumdisto[layno] =	(layno == 0) ?
									tcd_tile->distolayer[0] :
										(cumdisto[layno - 1] + tcd_tile->distolayer[layno]);

			// upper bound for next layer will equal lowerBound for previous layer, minus one
			upperBound = lowerBound - 1;
		}
		else {
			//todo: shouldn't need rate-distortion slope calculations to make this last layer
			tcd_make_layer_simple(tcd, layno, 0, true);
			assert(layno == tcd_tcp->numlayers - 1);
			// this has to be the last layer, so return 
			return true;
		}
    }
    return true;
}
static void prepareBlockForFirstLayer(tcd_cblk_enc_t* cblk) {
	cblk->num_passes_included_in_previous_layers = 0;
	cblk->num_passes_included_in_current_layer = 0;
	cblk->numlenbits = 0;
}

/*
Form layer for bisect rate control algorithm
*/
void tcd_make_layer_simple(tcd_t *tcd,
								uint32_t layno,
								double thresh,
								bool final)
{
	uint32_t compno, resno, bandno, precno, cblkno;
	uint32_t passno;
	tcd_tile_t *tcd_tile = tcd->tile;
	tcd_tile->distolayer[layno] = 0;
	for (compno = 0; compno < tcd_tile->numcomps; compno++) {
		tcd_tilecomp_t *tilec = tcd_tile->comps + compno;
		for (resno = 0; resno < tilec->numresolutions; resno++) {
			tcd_resolution_t *res = tilec->resolutions + resno;
			for (bandno = 0; bandno < res->numbands; bandno++) {
				tcd_band_t *band = res->bands + bandno;
				for (precno = 0; precno < res->pw * res->ph; precno++) {
					tcd_precinct_t *prc = band->precincts + precno;
					for (cblkno = 0; cblkno < prc->cw * prc->ch; cblkno++) {
						tcd_cblk_enc_t *cblk = prc->cblks.enc + cblkno;
						tcd_layer_t *layer = cblk->layers + layno;
						uint32_t cumulative_included_passes_in_block;
						if (layno == 0) {
							prepareBlockForFirstLayer(cblk);
						}
						if (thresh == 0) {
							cumulative_included_passes_in_block = cblk->num_passes_encoded;
						}
						else {
							cumulative_included_passes_in_block =	cblk->num_passes_included_in_previous_layers;
							for (passno = cblk->num_passes_included_in_previous_layers; passno < cblk->num_passes_encoded; passno++) {
								uint32_t dr;
								double dd;
								tcd_pass_t *pass = &cblk->passes[passno];
								if (cumulative_included_passes_in_block == 0) {
									dr = pass->rate;
									dd = pass->distortiondec;
								}
								else {
									dr = pass->rate - cblk->passes[cumulative_included_passes_in_block - 1].rate;
									dd = pass->distortiondec - cblk->passes[cumulative_included_passes_in_block - 1].distortiondec;
								}

								if (!dr) {
									if (dd != 0)
										cumulative_included_passes_in_block = passno + 1;
									continue;
								}
								auto slope = dd / dr;
								/* do not rely on float equality, check with DBL_EPSILON margin */
								if (thresh - slope < DBL_EPSILON)
									cumulative_included_passes_in_block = passno + 1;
							}
						}

						layer->numpasses = 	cumulative_included_passes_in_block - cblk->num_passes_included_in_previous_layers;
						if (!layer->numpasses) {
							layer->disto = 0;
							continue;
						}

						// update layer
						if (cblk->num_passes_included_in_previous_layers == 0) {
							layer->len = cblk->passes[cumulative_included_passes_in_block - 1].rate;
							layer->data = cblk->data;
							layer->disto =
								cblk->passes[cumulative_included_passes_in_block - 1].distortiondec;
						}
						else {
							layer->len =
								cblk->passes[cumulative_included_passes_in_block - 1].rate - cblk->passes[cblk->num_passes_included_in_previous_layers - 1].rate;
							layer->data =
								cblk->data + cblk->passes[cblk->num_passes_included_in_previous_layers - 1].rate;
							layer->disto =
								cblk->passes[cumulative_included_passes_in_block - 1].distortiondec - cblk->passes[cblk->num_passes_included_in_previous_layers - 1].distortiondec;
						}

						tcd_tile->distolayer[layno] += layer->disto;
						if (final)
							cblk->num_passes_included_in_previous_layers = cumulative_included_passes_in_block;
					}
				}
			}
		}
	}
}


// Add all remaining passes to this layer
void tcd_makelayer_final(tcd_t *tcd, uint32_t layno)
{
	uint32_t compno, resno, bandno, precno, cblkno;
	tcd_tile_t *tcd_tile = tcd->tile;
	tcd_tile->distolayer[layno] = 0;

	for (compno = 0; compno < tcd_tile->numcomps; compno++) {
		tcd_tilecomp_t *tilec = tcd_tile->comps + compno;
		for (resno = 0; resno < tilec->numresolutions; resno++) {
			tcd_resolution_t *res = tilec->resolutions + resno;
			for (bandno = 0; bandno < res->numbands; bandno++) {
				tcd_band_t *band = res->bands + bandno;
				for (precno = 0; precno < res->pw * res->ph; precno++) {
					tcd_precinct_t *prc = band->precincts + precno;
					for (cblkno = 0; cblkno < prc->cw * prc->ch; cblkno++) {
						tcd_cblk_enc_t *cblk = prc->cblks.enc + cblkno;
						tcd_layer_t *layer = cblk->layers + layno;
						if (layno == 0) {
							prepareBlockForFirstLayer(cblk);
						}
						uint32_t cumulative_included_passes_in_block =	cblk->num_passes_included_in_previous_layers;
						if (cblk->num_passes_encoded > cblk->num_passes_included_in_previous_layers)
							cumulative_included_passes_in_block = cblk->num_passes_encoded;

						layer->numpasses =
							cumulative_included_passes_in_block - cblk->num_passes_included_in_previous_layers;

						if (!layer->numpasses) {
							layer->disto = 0;
							continue;
						}

						// update layer
						if (cblk->num_passes_included_in_previous_layers == 0) {
							layer->len = cblk->passes[cumulative_included_passes_in_block - 1].rate;
							layer->data = cblk->data;
							layer->disto =
								cblk->passes[cumulative_included_passes_in_block - 1].distortiondec;
						}
						else {
							layer->len =
								cblk->passes[cumulative_included_passes_in_block - 1].rate - cblk->passes[cblk->num_passes_included_in_previous_layers - 1].rate;
							layer->data =
								cblk->data + cblk->passes[cblk->num_passes_included_in_previous_layers - 1].rate;
							layer->disto =
								cblk->passes[cumulative_included_passes_in_block - 1].distortiondec - cblk->passes[cblk->num_passes_included_in_previous_layers - 1].distortiondec;
						}
						tcd_tile->distolayer[layno] += layer->disto;
						cblk->num_passes_included_in_previous_layers = cumulative_included_passes_in_block;
						assert(cblk->num_passes_included_in_previous_layers == cblk->num_passes_encoded);
					}
				}
			}
		}
	}
}
bool tcd_init( tcd_t *p_tcd,
                   opj_image_t * p_image,
                   cp_t * p_cp,
				   uint32_t numThreads){
    p_tcd->image = p_image;
    p_tcd->cp = p_cp;

    p_tcd->tile = (tcd_tile_t *) grok_calloc(1,sizeof(tcd_tile_t));
    if (! p_tcd->tile) {
        return false;
    }

    p_tcd->tile->comps = (tcd_tilecomp_t *) grok_calloc(p_image->numcomps,sizeof(tcd_tilecomp_t));
    if (! p_tcd->tile->comps ) {
        return false;
    }

    p_tcd->tile->numcomps = p_image->numcomps;
    p_tcd->tp_pos = p_cp->m_specific_param.m_enc.m_tp_pos;
	p_tcd->numThreads = numThreads;
    return true;
}

/**
Destroy a previously created TCD handle
*/
void tcd_destroy(tcd_t *tcd){
    if (tcd) {
        tcd_free_tile(tcd);
        grok_free(tcd);
    }
}

/* ----------------------------------------------------------------------- */

static inline bool tcd_init_tile(tcd_t *p_tcd,
                                     uint32_t p_tile_no,
                                     opj_image_t* output_image,
                                     bool isEncoder,
                                     float fraction,
                                     size_t sizeof_block,
                                     event_mgr_t* manager)
{
    uint32_t (*l_gain_ptr)(uint8_t) = nullptr;
    uint32_t compno, resno, bandno, precno, cblkno;
    tcp_t * l_tcp = nullptr;
    cp_t * l_cp = nullptr;
    tcd_tile_t * l_tile = nullptr;
    tccp_t *l_tccp = nullptr;
    tcd_tilecomp_t *l_tilec = nullptr;
    opj_image_comp_t * l_image_comp = nullptr;
    tcd_resolution_t *l_res = nullptr;
    tcd_band_t *l_band = nullptr;
    stepsize_t * l_step_size = nullptr;
    tcd_precinct_t *l_current_precinct = nullptr;
    opj_image_t *l_image = nullptr;
    uint32_t p,q;
    uint32_t l_level_no;
    uint32_t l_pdx, l_pdy;
    uint32_t l_gain;
    uint32_t l_x0b, l_y0b;
    uint32_t l_tx0, l_ty0;
    /* extent of precincts , top left, bottom right**/
    uint32_t l_tl_prc_x_start, l_tl_prc_y_start, l_br_prc_x_end, l_br_prc_y_end;
    /* number of precinct for a resolution */
    uint32_t l_nb_precincts;
    /* room needed to store l_nb_precinct precinct for a resolution */
    uint32_t l_nb_precinct_size;
    /* number of code blocks for a precinct*/
    uint32_t l_nb_code_blocks;
    /* room needed to store l_nb_code_blocks code blocks for a precinct*/
    uint32_t l_nb_code_blocks_size;

	uint32_t state = grok_plugin_get_debug_state();

    l_cp = p_tcd->cp;
    l_tcp = &(l_cp->tcps[p_tile_no]);
    l_tile = p_tcd->tile;
    l_tccp = l_tcp->tccps;
    l_tilec = l_tile->comps;
    l_image = p_tcd->image;
    l_image_comp = p_tcd->image->comps;

	if (l_tcp->m_tile_data)
		l_tcp->m_tile_data->rewind();

    p = p_tile_no % l_cp->tw;       /* tile coordinates */
    q = p_tile_no / l_cp->tw;
    /*fprintf(stderr, "Tile coordinate = %d,%d\n", p, q);*/

    /* 4 borders of the tile rescale on the image if necessary */
    l_tx0 = l_cp->tx0 + p * l_cp->tdx; /* can't be greater than l_image->x1 so won't overflow */
    l_tile->x0 = std::max<uint32_t>(l_tx0, l_image->x0);
    l_tile->x1 = std::min<uint32_t>(uint_adds(l_tx0, l_cp->tdx), l_image->x1);
	if (l_tile->x1 <= l_tile->x0) {
		event_msg(manager, EVT_ERROR, "Tile x coordinates are not valid\n");
		return false;
	}
    l_ty0 = l_cp->ty0 + q * l_cp->tdy; /* can't be greater than l_image->y1 so won't overflow */
    l_tile->y0 = std::max<uint32_t>(l_ty0, l_image->y0);
    l_tile->y1 = std::min<uint32_t>(uint_adds(l_ty0, l_cp->tdy), l_image->y1);
	if (l_tile->y1 <= l_tile->y0) {
		event_msg(manager, EVT_ERROR, "Tile y coordinates are not valid\n");
		return false;
	}

    /* testcase 1888.pdf.asan.35.988 */
    if (l_tccp->numresolutions == 0) {
        event_msg(manager, EVT_ERROR, "tiles require at least one resolution\n");
        return false;
    }
    /*fprintf(stderr, "Tile border = %d,%d,%d,%d\n", l_tile->x0, l_tile->y0,l_tile->x1,l_tile->y1);*/

    /*tile->numcomps = image->numcomps; */
    for (compno = 0; compno < l_tile->numcomps; ++compno) {
        uint64_t l_tile_data_size=0;
        uint32_t l_res_data_size=0;
        /*fprintf(stderr, "compno = %d/%d\n", compno, l_tile->numcomps);*/
		if (l_image_comp->dx == 0 || l_image_comp->dy == 0) {
			return false;
		}
		l_image_comp->resno_decoded = 0;
        /* border of each l_tile component (global) */
        l_tilec->x0 = ceildiv<uint32_t>(l_tile->x0, l_image_comp->dx);
        l_tilec->y0 = ceildiv<uint32_t>(l_tile->y0, l_image_comp->dy);
        l_tilec->x1 = ceildiv<uint32_t>(l_tile->x1, l_image_comp->dx);
        l_tilec->y1 = ceildiv<uint32_t>(l_tile->y1, l_image_comp->dy);
        /*fprintf(stderr, "\tTile compo border = %d,%d,%d,%d\n", l_tilec->x0, l_tilec->y0,l_tilec->x1,l_tilec->y1);*/

        /* compute l_data_size with overflow check */
        l_tile_data_size = (uint64_t)(l_tilec->x1 - l_tilec->x0) * (uint64_t)(l_tilec->y1 - l_tilec->y0) * sizeof(uint32_t);
        l_tilec->numresolutions = l_tccp->numresolutions;
        if (l_tccp->numresolutions < l_cp->m_specific_param.m_dec.m_reduce) {
            l_tilec->minimum_num_resolutions = 1;
        } else {
            l_tilec->minimum_num_resolutions = l_tccp->numresolutions - l_cp->m_specific_param.m_dec.m_reduce;
        }

        l_res_data_size = l_tilec->numresolutions * (uint32_t)sizeof(tcd_resolution_t);

        if (l_tilec->resolutions == nullptr) {
            l_tilec->resolutions = (tcd_resolution_t *) grok_malloc(l_res_data_size);
            if (! l_tilec->resolutions ) {
                return false;
            }
            /*fprintf(stderr, "\tAllocate resolutions of tilec (tcd_resolution_t): %d\n",l_data_size);*/
            l_tilec->resolutions_size = l_res_data_size;
            memset(l_tilec->resolutions,0, l_res_data_size);
        } else if (l_res_data_size > l_tilec->resolutions_size) {
            tcd_resolution_t* new_resolutions = (tcd_resolution_t *) grok_realloc(l_tilec->resolutions, l_res_data_size);
            if (! new_resolutions) {
                event_msg(manager, EVT_ERROR, "Not enough memory for tile resolutions\n");
                grok_free(l_tilec->resolutions);
                l_tilec->resolutions = nullptr;
                l_tilec->resolutions_size = 0;
                return false;
            }
            l_tilec->resolutions = new_resolutions;
            /*fprintf(stderr, "\tReallocate data of tilec (int): from %d to %d x uint32_t\n", l_tilec->resolutions_size, l_data_size);*/
            memset(((uint8_t*) l_tilec->resolutions)+l_tilec->resolutions_size,0, l_res_data_size - l_tilec->resolutions_size);
            l_tilec->resolutions_size = l_res_data_size;
        }

        l_level_no = l_tilec->numresolutions;
        l_res = l_tilec->resolutions;
        l_step_size = l_tccp->stepsizes;
        if (l_tccp->qmfbid == 0) {
            l_gain_ptr = &dwt_getgain_real;
        } else {
            l_gain_ptr  = &dwt_getgain;
        }
        /*fprintf(stderr, "\tlevel_no=%d\n",l_level_no);*/

        for (resno = 0; resno < l_tilec->numresolutions; ++resno) {
            /*fprintf(stderr, "\t\tresno = %d/%d\n", resno, l_tilec->numresolutions);*/
            uint32_t tlcbgxstart, tlcbgystart /*, brcbgxend, brcbgyend*/;
            uint32_t cbgwidthexpn, cbgheightexpn;
            uint32_t cblkwidthexpn, cblkheightexpn;

			--l_level_no;

            /* border for each resolution level (global) */
            l_res->x0 = uint_ceildivpow2(l_tilec->x0, l_level_no);
            l_res->y0 = uint_ceildivpow2(l_tilec->y0, l_level_no);
            l_res->x1 = uint_ceildivpow2(l_tilec->x1, l_level_no);
            l_res->y1 = uint_ceildivpow2(l_tilec->y1, l_level_no);
            /*fprintf(stderr, "\t\t\tres_x0= %d, res_y0 =%d, res_x1=%d, res_y1=%d\n", l_res->x0, l_res->y0, l_res->x1, l_res->y1);*/
            /* p. 35, table A-23, ISO/IEC FDIS154444-1 : 2000 (18 august 2000) */
            l_pdx = l_tccp->prcw[resno];
            l_pdy = l_tccp->prch[resno];
            /*fprintf(stderr, "\t\t\tpdx=%d, pdy=%d\n", l_pdx, l_pdy);*/
            /* p. 64, B.6, ISO/IEC FDIS15444-1 : 2000 (18 august 2000)  */
            l_tl_prc_x_start = uint_floordivpow2(l_res->x0, l_pdx) << l_pdx;
            l_tl_prc_y_start = uint_floordivpow2(l_res->y0, l_pdy) << l_pdy;
            l_br_prc_x_end = uint_ceildivpow2(l_res->x1, l_pdx) << l_pdx;
            l_br_prc_y_end = uint_ceildivpow2(l_res->y1, l_pdy) << l_pdy;
            /*fprintf(stderr, "\t\t\tprc_x_start=%d, prc_y_start=%d, br_prc_x_end=%d, br_prc_y_end=%d \n", l_tl_prc_x_start, l_tl_prc_y_start, l_br_prc_x_end ,l_br_prc_y_end );*/

            l_res->pw = (l_res->x0 == l_res->x1) ? 0 : ((l_br_prc_x_end - l_tl_prc_x_start) >> l_pdx);
            l_res->ph = (l_res->y0 == l_res->y1) ? 0 : ((l_br_prc_y_end - l_tl_prc_y_start) >> l_pdy);
            /*fprintf(stderr, "\t\t\tres_pw=%d, res_ph=%d\n", l_res->pw, l_res->ph );*/

			if (mult_will_overflow(l_res->pw, l_res->ph)) {
				event_msg(manager, EVT_ERROR, "l_nb_precincts calculation would overflow \n");
				return false;
			}
            l_nb_precincts = l_res->pw * l_res->ph;

			if (mult_will_overflow(l_nb_precincts, (uint32_t)sizeof(tcd_precinct_t))) {
				event_msg(manager, EVT_ERROR, "l_nb_precinct_size calculation would overflow \n");
				return false;
			}
            l_nb_precinct_size = l_nb_precincts * (uint32_t)sizeof(tcd_precinct_t);
            if (resno == 0) {
                tlcbgxstart = l_tl_prc_x_start;
                tlcbgystart = l_tl_prc_y_start;
                /*brcbgxend = l_br_prc_x_end;*/
                /* brcbgyend = l_br_prc_y_end;*/
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
			size_t nominalBlockSize = (1 << cblkwidthexpn)*(1 << cblkheightexpn);
            l_band = l_res->bands;

            for (bandno = 0; bandno < l_res->numbands; ++bandno) {
                uint32_t numbps;
                /*fprintf(stderr, "\t\t\tband_no=%d/%d\n", bandno, l_res->numbands );*/

                if (resno == 0) {
                    l_band->bandno = 0 ;
                    l_band->x0 = uint_ceildivpow2(l_tilec->x0, l_level_no);
                    l_band->y0 = uint_ceildivpow2(l_tilec->y0, l_level_no);
                    l_band->x1 = uint_ceildivpow2(l_tilec->x1, l_level_no);
                    l_band->y1 = uint_ceildivpow2(l_tilec->y1, l_level_no);
                } else {
                    l_band->bandno = bandno + 1;
                    /* x0b = 1 if bandno = 1 or 3 */
                    l_x0b = l_band->bandno&1;
                    /* y0b = 1 if bandno = 2 or 3 */
                    l_y0b = (int32_t)((l_band->bandno)>>1);
                    /* l_band border (global) */
                    l_band->x0 = uint64_ceildivpow2(l_tilec->x0 - ((uint64_t)l_x0b << l_level_no), l_level_no + 1);
                    l_band->y0 = uint64_ceildivpow2(l_tilec->y0 - ((uint64_t)l_y0b << l_level_no), l_level_no + 1);
                    l_band->x1 = uint64_ceildivpow2(l_tilec->x1 - ((uint64_t)l_x0b << l_level_no), l_level_no + 1);
                    l_band->y1 = uint64_ceildivpow2(l_tilec->y1 - ((uint64_t)l_y0b << l_level_no), l_level_no + 1);
                }

                /** avoid an if with storing function pointer */
                l_gain = (*l_gain_ptr) ((uint8_t)l_band->bandno);
                numbps = l_image_comp->prec + l_gain;
                l_band->stepsize = (float)(((1.0 + l_step_size->mant / 2048.0) * pow(2.0, (int32_t) (numbps - l_step_size->expn)))) * fraction;
                l_band->numbps = l_step_size->expn + l_tccp->numgbits - 1;      /* WHY -1 ? */

                if (!l_band->precincts && (l_nb_precincts > 0U)) {
                    l_band->precincts = (tcd_precinct_t *) grok_malloc( /*3 * */ l_nb_precinct_size);
                    if (! l_band->precincts) {
						event_msg(manager, EVT_ERROR, "Not enough memory for band precints\n");
                        return false;
                    }
                    /*fprintf(stderr, "\t\t\t\tAllocate precincts of a band (tcd_precinct_t): %d\n",l_nb_precinct_size);     */
                    memset(l_band->precincts,0,l_nb_precinct_size);
                    l_band->precincts_data_size = l_nb_precinct_size;
                } else if (l_band->precincts_data_size < l_nb_precinct_size) {

                    tcd_precinct_t * new_precincts = (tcd_precinct_t *) grok_realloc(l_band->precincts,/*3 * */ l_nb_precinct_size);
                    if (! new_precincts) {
                        event_msg(manager, EVT_ERROR, "Not enough memory to handle band precints\n");
                        grok_free(l_band->precincts);
                        l_band->precincts = nullptr;
                        l_band->precincts_data_size = 0;
						l_band->numPrecincts = 0;
                        return false;
                    }
                    l_band->precincts = new_precincts;
                    /*fprintf(stderr, "\t\t\t\tReallocate precincts of a band (tcd_precinct_t): from %d to %d\n",l_band->precincts_data_size, l_nb_precinct_size);*/
                    memset(((uint8_t *) l_band->precincts) + l_band->precincts_data_size,0,l_nb_precinct_size - l_band->precincts_data_size);
                    l_band->precincts_data_size = l_nb_precinct_size;
                }
				l_band->numPrecincts = l_nb_precincts;
                l_current_precinct = l_band->precincts;
                for (precno = 0; precno < l_nb_precincts; ++precno) {
                    uint32_t tlcblkxstart, tlcblkystart, brcblkxend, brcblkyend;
                    uint32_t cbgxstart = tlcbgxstart + (precno % l_res->pw) * (1 << cbgwidthexpn);
                    uint32_t cbgystart = tlcbgystart + (precno / l_res->pw) * (1 << cbgheightexpn);
                    uint32_t cbgxend = cbgxstart + (1 << cbgwidthexpn);
                    uint32_t cbgyend = cbgystart + (1 << cbgheightexpn);
                    /*fprintf(stderr, "\t precno=%d; bandno=%d, resno=%d; compno=%d\n", precno, bandno , resno, compno);*/
                    /*fprintf(stderr, "\t tlcbgxstart(=%d) + (precno(=%d) percent res->pw(=%d)) * (1 << cbgwidthexpn(=%d)) \n",tlcbgxstart,precno,l_res->pw,cbgwidthexpn);*/

                    /* precinct size (global) */
                    /*fprintf(stderr, "\t cbgxstart=%d, l_band->x0 = %d \n",cbgxstart, l_band->x0);*/

                    l_current_precinct->x0 = std::max<uint32_t>(cbgxstart, l_band->x0);
                    l_current_precinct->y0 = std::max<uint32_t>(cbgystart, l_band->y0);
                    l_current_precinct->x1 = std::min<uint32_t>(cbgxend, l_band->x1);
                    l_current_precinct->y1 = std::min<uint32_t>(cbgyend, l_band->y1);
                    /*fprintf(stderr, "\t prc_x0=%d; prc_y0=%d, prc_x1=%d; prc_y1=%d\n",l_current_precinct->x0, l_current_precinct->y0 ,l_current_precinct->x1, l_current_precinct->y1);*/

                    tlcblkxstart = uint_floordivpow2(l_current_precinct->x0, cblkwidthexpn) << cblkwidthexpn;
                    /*fprintf(stderr, "\t tlcblkxstart =%d\n",tlcblkxstart );*/
                    tlcblkystart = uint_floordivpow2(l_current_precinct->y0, cblkheightexpn) << cblkheightexpn;
                    /*fprintf(stderr, "\t tlcblkystart =%d\n",tlcblkystart );*/
                    brcblkxend = uint_ceildivpow2(l_current_precinct->x1, cblkwidthexpn) << cblkwidthexpn;
                    /*fprintf(stderr, "\t brcblkxend =%d\n",brcblkxend );*/
                    brcblkyend = uint_ceildivpow2(l_current_precinct->y1, cblkheightexpn) << cblkheightexpn;
                    /*fprintf(stderr, "\t brcblkyend =%d\n",brcblkyend );*/
                    l_current_precinct->cw = ((brcblkxend - tlcblkxstart) >> cblkwidthexpn);
                    l_current_precinct->ch = ((brcblkyend - tlcblkystart) >> cblkheightexpn);

					if (mult_will_overflow(l_current_precinct->cw, l_current_precinct->ch)) {
						event_msg(manager, EVT_ERROR, "l_nb_code_blocks calculation would overflow \n");
						return false;
					}
                    l_nb_code_blocks = l_current_precinct->cw * l_current_precinct->ch;
                    /*fprintf(stderr, "\t\t\t\t precinct_cw = %d x recinct_ch = %d\n",l_current_precinct->cw, l_current_precinct->ch);      */

					if (mult_will_overflow(l_nb_code_blocks, (uint32_t)sizeof_block)) {
						event_msg(manager, EVT_ERROR, "l_nb_code_blocks_size calculation would overflow \n");
						return false;
					}
                    l_nb_code_blocks_size = l_nb_code_blocks * (uint32_t)sizeof_block;

                    if (!l_current_precinct->cblks.blocks && (l_nb_code_blocks > 0U)) {
                        l_current_precinct->cblks.blocks = grok_malloc(l_nb_code_blocks_size);
                        if (! l_current_precinct->cblks.blocks ) {
                            return false;
                        }
                        /*fprintf(stderr, "\t\t\t\tAllocate cblks of a precinct (tcd_cblk_dec_t): %d\n",l_nb_code_blocks_size);*/
                        memset(l_current_precinct->cblks.blocks,0,l_nb_code_blocks_size);

                        l_current_precinct->block_size = l_nb_code_blocks_size;
                    } else if (l_nb_code_blocks_size > l_current_precinct->block_size) {
                        void *new_blocks = grok_realloc(l_current_precinct->cblks.blocks, l_nb_code_blocks_size);
                        if (! new_blocks) {
                            grok_free(l_current_precinct->cblks.blocks);
                            l_current_precinct->cblks.blocks = nullptr;
                            l_current_precinct->block_size = 0;
                            event_msg(manager, EVT_ERROR, "Not enough memory for current precinct codeblock element\n");
                            return false;
                        }
                        l_current_precinct->cblks.blocks = new_blocks;
                        /*fprintf(stderr, "\t\t\t\tReallocate cblks of a precinct (tcd_cblk_dec_t): from %d to %d\n",l_current_precinct->block_size, l_nb_code_blocks_size);     */

                        memset(((uint8_t *) l_current_precinct->cblks.blocks) + l_current_precinct->block_size,0,l_nb_code_blocks_size - l_current_precinct->block_size);
                        l_current_precinct->block_size = l_nb_code_blocks_size;
                    }

					l_current_precinct->initTagTrees(manager);
					
                    for (cblkno = 0; cblkno < l_nb_code_blocks; ++cblkno) {
                        uint32_t cblkxstart = tlcblkxstart + (cblkno % l_current_precinct->cw) * (1 << cblkwidthexpn);
                        uint32_t cblkystart = tlcblkystart + (cblkno / l_current_precinct->cw) * (1 << cblkheightexpn);
                        uint32_t cblkxend = cblkxstart + (1 << cblkwidthexpn);
                        uint32_t cblkyend = cblkystart + (1 << cblkheightexpn);

                        if (isEncoder) {
                            tcd_cblk_enc_t* l_code_block = l_current_precinct->cblks.enc + cblkno;

                            if (! l_code_block->alloc()) {
                                return false;
                            }
                            /* code-block size (global) */
                            l_code_block->x0 = std::max<uint32_t>(cblkxstart, l_current_precinct->x0);
                            l_code_block->y0 = std::max<uint32_t>(cblkystart, l_current_precinct->y0);
                            l_code_block->x1 = std::min<uint32_t>(cblkxend, l_current_precinct->x1);
                            l_code_block->y1 = std::min<uint32_t>(cblkyend, l_current_precinct->y1);

							if (!p_tcd->current_plugin_tile || (state & GROK_PLUGIN_STATE_DEBUG)) {
								if (!l_code_block->alloc_data(nominalBlockSize)) {
									return false;
								}
							}
                        } else {
                            tcd_cblk_dec_t* l_code_block = l_current_precinct->cblks.dec + cblkno;
							if (!p_tcd->current_plugin_tile || (state & GROK_PLUGIN_STATE_DEBUG)) {
								if (!l_code_block->alloc()) {
									return false;
								}
							}

                            /* code-block size (global) */
                            l_code_block->x0 = std::max<uint32_t>(cblkxstart, l_current_precinct->x0);
                            l_code_block->y0 = std::max<uint32_t>(cblkystart, l_current_precinct->y0);
                            l_code_block->x1 = std::min<uint32_t>(cblkxend, l_current_precinct->x1);
                            l_code_block->y1 = std::min<uint32_t>(cblkyend, l_current_precinct->y1);
                        }
                    }
                    ++l_current_precinct;
                } /* precno */
                ++l_band;
                ++l_step_size;
            } /* bandno */
            ++l_res;
        } /* resno */
        if (!tile_buf_create_component(l_tilec, 
											isEncoder,
                                           l_tccp->qmfbid ? false : true,
                                           1 << l_tccp->cblkw,
                                           1 << l_tccp->cblkh,
                                           output_image,
                                           l_image_comp->dx,
                                           l_image_comp->dy)) {
            return false;
        }
        l_tilec->buf->data_size_needed = l_tile_data_size;

        ++l_tccp;
        ++l_tilec;
        ++l_image_comp;
    } /* compno */


	// decoder plugin debug sanity check on tile struct
	if (!isEncoder) {
		if (state & GROK_PLUGIN_STATE_DEBUG) {
			if (!tile_equals(p_tcd->current_plugin_tile, l_tile)) {
				manager->warning_handler("plugin tile differs from opj tile", nullptr);
			}
		}
	}
    return true;
}

bool tcd_init_encode_tile (tcd_t *p_tcd, uint32_t p_tile_no, event_mgr_t* p_manager)
{
    return tcd_init_tile(p_tcd, p_tile_no, nullptr, true, 1.0F, sizeof(tcd_cblk_enc_t), p_manager);
}

bool tcd_init_decode_tile (tcd_t *p_tcd,
                               opj_image_t* output_image,
                               uint32_t p_tile_no,
                               event_mgr_t* p_manager)
{
    return  tcd_init_tile(p_tcd,
                              p_tile_no,
                              output_image,
                              false,
                              0.5F,
                              sizeof(tcd_cblk_dec_t),
                              p_manager);

}

/*
Get size of tile data, summed over all components, reflecting actual precision of data.
opj_image_t always stores data in 32 bit format.
*/
uint64_t tcd_get_decoded_tile_size ( tcd_t *p_tcd )
{
    uint32_t i;
    uint64_t l_data_size = 0;
    opj_image_comp_t * l_img_comp = nullptr;
    tcd_tilecomp_t * l_tile_comp = nullptr;
    tcd_resolution_t * l_res = nullptr;
    uint32_t l_size_comp;

    l_tile_comp = p_tcd->tile->comps;
    l_img_comp = p_tcd->image->comps;

    for (i=0; i<p_tcd->image->numcomps; ++i) {
        l_size_comp = (l_img_comp->prec + 7) >> 3;

        if (l_size_comp == 3) {
            l_size_comp = 4;
        }

        l_res = l_tile_comp->resolutions + l_tile_comp->minimum_num_resolutions - 1;
        l_data_size += l_size_comp * (uint64_t)(l_res->x1 - l_res->x0) * (l_res->y1 - l_res->y0);
        ++l_img_comp;
        ++l_tile_comp;
    }

    return l_data_size;
}

bool tcd_encode_tile(   tcd_t *p_tcd,
                            uint32_t p_tile_no,
							GrokStream* p_stream,
                            uint64_t * p_data_written,
                            uint64_t p_max_length,
                            opj_codestream_info_t *p_cstr_info,
							event_mgr_t * p_manager)
{
	uint32_t state = grok_plugin_get_debug_state();
    if (p_tcd->cur_tp_num == 0) {

        p_tcd->tcd_tileno = p_tile_no;
        p_tcd->tcp = &p_tcd->cp->tcps[p_tile_no];

        if(p_cstr_info)  {
            uint32_t l_num_packs = 0;
            uint32_t i;
            tcd_tilecomp_t *l_tilec_idx = &p_tcd->tile->comps[0];        /* based on component 0 */
            tccp_t *l_tccp = p_tcd->tcp->tccps; /* based on component 0 */

            for (i = 0; i < l_tilec_idx->numresolutions; i++) {
                tcd_resolution_t *l_res_idx = &l_tilec_idx->resolutions[i];

                p_cstr_info->tile[p_tile_no].pw[i] = (int)l_res_idx->pw;
                p_cstr_info->tile[p_tile_no].ph[i] = (int)l_res_idx->ph;

                l_num_packs += l_res_idx->pw * l_res_idx->ph;
                p_cstr_info->tile[p_tile_no].pdx[i] = (int)l_tccp->prcw[i];
                p_cstr_info->tile[p_tile_no].pdy[i] = (int)l_tccp->prch[i];
            }
            p_cstr_info->tile[p_tile_no].packet = (opj_packet_info_t*) grok_calloc((size_t)p_cstr_info->numcomps * (size_t)p_cstr_info->numlayers * l_num_packs, sizeof(opj_packet_info_t));
            if (!p_cstr_info->tile[p_tile_no].packet) {
				event_msg(p_manager, EVT_ERROR, "tcd_encode_tile: Out of memory error when allocating packet memory\n");
                return false;
            }
        }
		if (state & GROK_PLUGIN_STATE_DEBUG) {
			set_context_stream(p_tcd);
		}

		// When debugging the encoder, we do all of T1 up to and including DWT in the plugin, and pass this in as image data.
		// This way, both OPJ and plugin start with same inputs for context formation and MQ coding.
		bool debugEncode = state & GROK_PLUGIN_STATE_DEBUG;
		bool debugMCT = (state & GROK_PLUGIN_STATE_MCT_ONLY) ? true : false ;

		if (!p_tcd->current_plugin_tile || debugEncode) {

			if (!debugEncode) {
				if (!tcd_dc_level_shift_encode(p_tcd)) {
					return false;
				}
				if (!tcd_mct_encode(p_tcd)) {
					return false;
				}
			}
			if (!debugEncode || debugMCT) {
				if (!tcd_dwt_encode(p_tcd)) {
					return false;
				}
			}
			if (!tcd_t1_encode(p_tcd)) {
				return false;
			}
		}
		if (!tcd_rate_allocate_encode(p_tcd, p_max_length, p_cstr_info, p_manager)) {
			return false;
		}
    }
    if (p_cstr_info) {
        p_cstr_info->index_write = 1;
    }
    if (! tcd_t2_encode(p_tcd,
							p_stream,
							p_data_written,
							p_max_length,
							p_cstr_info,
							p_manager)) {
        return false;
    }
    return true;
}

bool tcd_decode_tile(tcd_t *p_tcd,
	seg_buf_t* src_buf,
	uint32_t p_tile_no,
	event_mgr_t *p_manager) {
	p_tcd->tcp = p_tcd->cp->tcps + p_tile_no;

	bool doT2 = !p_tcd->current_plugin_tile ||
		(p_tcd->current_plugin_tile->decode_flags& GROK_DECODE_T2);

	bool doT1 = !p_tcd->current_plugin_tile ||
		(p_tcd->current_plugin_tile->decode_flags & GROK_DECODE_T1);

	bool doPostT1 = !p_tcd->current_plugin_tile ||
		(p_tcd->current_plugin_tile->decode_flags & GROK_DECODE_POST_T1);

	if (doT2) {
		uint64_t l_data_read = 0;
		if (!tcd_t2_decode(p_tcd, p_tile_no, src_buf, &l_data_read, p_manager)) {
			return false;
		}
		// synch plugin with T2 data
		decode_synch_plugin_with_host(p_tcd, p_manager);
	}

	if (doT1) {
		if (!tcd_t1_decode(p_tcd, p_manager)) {
			return false;
		}
	}

	if (doPostT1) {

		if (!tcd_dwt_decode(p_tcd)) {
			return false;
		}
		if (!tcd_mct_decode(p_tcd, p_manager)) {
			return false;
		}
		if (!tcd_dc_level_shift_decode(p_tcd)) {
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
bool tcd_update_tile_data ( tcd_t *p_tcd,
                                uint8_t * p_dest,
                                uint64_t p_dest_length
                              )
{
    uint32_t i,j,k;
    opj_image_comp_t * l_img_comp = nullptr;
    tcd_tilecomp_t * l_tilec = nullptr;
    tcd_resolution_t * l_res;
    uint32_t l_size_comp;
    uint32_t l_stride, l_width,l_height;
	uint64_t l_data_size = tcd_get_decoded_tile_size(p_tcd);
    if (l_data_size > p_dest_length) {
        return false;
    }

    l_tilec = p_tcd->tile->comps;
    l_img_comp = p_tcd->image->comps;

    for (i=0; i<p_tcd->image->numcomps; ++i) {
        l_size_comp = (l_img_comp->prec + 7) >> 3;
        l_res = l_tilec->resolutions + l_img_comp->resno_decoded;
        l_width = (l_res->x1 - l_res->x0);
        l_height = (l_res->y1 - l_res->y0);
        l_stride = (l_tilec->x1 - l_tilec->x0) - l_width;

        if (l_size_comp == 3) {
            l_size_comp = 4;
        }

        switch (l_size_comp) {
        case 1: {
            char * l_dest_ptr = (char *) p_dest;
            const int32_t * l_src_ptr = tile_buf_get_ptr(l_tilec->buf, 0, 0, 0, 0);

            if (l_img_comp->sgnd) {
                for (j=0; j<l_height; ++j) {
                    for (k=0; k<l_width; ++k) {
                        *(l_dest_ptr++) = (char) (*(l_src_ptr++));
                    }
                    l_src_ptr += l_stride;
                }
            } else {
                for (j=0; j<l_height; ++j) {
                    for     (k=0; k<l_width; ++k) {
                        *(l_dest_ptr++) = (char) ((*(l_src_ptr++))&0xff);
                    }
                    l_src_ptr += l_stride;
                }
            }

            p_dest = (uint8_t *)l_dest_ptr;
        }
        break;
        case 2: {
            const int32_t * l_src_ptr = tile_buf_get_ptr(l_tilec->buf, 0, 0, 0, 0);
            int16_t * l_dest_ptr = (int16_t *) p_dest;

            if (l_img_comp->sgnd) {
                for (j=0; j<l_height; ++j) {
                    for (k=0; k<l_width; ++k) {
                        *(l_dest_ptr++) = (int16_t) (*(l_src_ptr++));
                    }
                    l_src_ptr += l_stride;
                }
            } else {
                for (j=0; j<l_height; ++j) {
                    for (k=0; k<l_width; ++k) {
						//cast and mask to avoid sign extension
                        *(l_dest_ptr++) = (int16_t) ((*(l_src_ptr++))&0xffff);
                    }
                    l_src_ptr += l_stride;
                }
            }

            p_dest = (uint8_t*) l_dest_ptr;
        }
        break;
        case 4: {
            int32_t * l_dest_ptr = (int32_t *) p_dest;
            int32_t * l_src_ptr = tile_buf_get_ptr(l_tilec->buf, 0, 0, 0, 0);

            for (j=0; j<l_height; ++j) {
                for (k=0; k<l_width; ++k) {
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




static void tcd_free_tile(tcd_t *p_tcd)
{
    uint32_t compno, resno, bandno, precno;
    tcd_tile_t *l_tile = nullptr;
    tcd_tilecomp_t *l_tile_comp = nullptr;
    tcd_resolution_t *l_res = nullptr;
    tcd_band_t *l_band = nullptr;
    tcd_precinct_t *l_precinct = nullptr;
    size_t l_nb_resolutions, l_nb_precincts;
    void (* l_tcd_code_block_deallocate) (tcd_precinct_t *) = nullptr;

    if (! p_tcd) {
        return;
    }

    if (! p_tcd->tile) {
        return;
    }

    if (p_tcd->m_is_decoder) {
        l_tcd_code_block_deallocate = tcd_code_block_dec_deallocate;
    } else {
        l_tcd_code_block_deallocate = tcd_code_block_enc_deallocate;
    }

    l_tile = p_tcd->tile;
    if (! l_tile) {
        return;
    }

    l_tile_comp = l_tile->comps;

    for (compno = 0; compno < l_tile->numcomps; ++compno) {
        l_res = l_tile_comp->resolutions;
        if (l_res) {

            l_nb_resolutions = l_tile_comp->resolutions_size / sizeof(tcd_resolution_t);
            for (resno = 0; resno < l_nb_resolutions; ++resno) {
                l_band = l_res->bands;
                for     (bandno = 0; bandno < 3; ++bandno) {
                    l_precinct = l_band->precincts;
                    if (l_precinct) {

						l_nb_precincts = l_band->precincts_data_size / sizeof(tcd_precinct_t);
                        for (precno = 0; precno < l_nb_precincts; ++precno) {
							delete l_precinct->incltree;
                            l_precinct->incltree = nullptr;
							delete l_precinct->imsbtree;
                            l_precinct->imsbtree = nullptr;
                            (*l_tcd_code_block_deallocate) (l_precinct);
                            ++l_precinct;
                        }

                        grok_free(l_band->precincts);
                        l_band->precincts = nullptr;
                    }
                    ++l_band;
                } /* for (resno */
                ++l_res;
            }

            grok_free(l_tile_comp->resolutions);
            l_tile_comp->resolutions = nullptr;
        }

        tile_buf_destroy_component(l_tile_comp->buf);
        l_tile_comp->buf = nullptr;
        ++l_tile_comp;
    }

    grok_free(l_tile->comps);
    l_tile->comps = nullptr;
    grok_free(p_tcd->tile);
    p_tcd->tile = nullptr;
}


static bool tcd_t2_decode (tcd_t *p_tcd,
								uint32_t p_tile_no,
                               seg_buf_t* src_buf,
                               uint64_t * p_data_read,
                               event_mgr_t *p_manager
                              )
{
    t2_t * l_t2;

    l_t2 = t2_create(p_tcd->image, p_tcd->cp);
    if (l_t2 == nullptr) {
        return false;
    }

    if (! t2_decode_packets(
                l_t2,
				p_tile_no,
                p_tcd->tile,
                src_buf,
                p_data_read,
                p_manager)) {
        t2_destroy(l_t2);
        return false;
    }

    t2_destroy(l_t2);

    return true;
}

static bool tcd_t1_decode ( tcd_t *p_tcd, event_mgr_t * p_manager)
{
    uint32_t compno;
    tcd_tile_t * l_tile = p_tcd->tile;
    tcd_tilecomp_t* l_tile_comp = l_tile->comps;
    tccp_t * l_tccp = p_tcd->tcp->tccps;
	std::vector<decodeBlockInfo*> blocks;
	auto t1_wrap = std::unique_ptr<Tier1>(new Tier1());
    for (compno = 0; compno < l_tile->numcomps; ++compno) {
        if (!t1_wrap->prepareDecodeCodeblocks(l_tile_comp, l_tccp,&blocks, p_manager)) {
            return false;
        }
        ++l_tile_comp;
        ++l_tccp;
    }
	// !!! assume that code block dimensions do not change over components
	return t1_wrap->decodeCodeblocks(p_tcd->tcp, (uint16_t)p_tcd->tcp->tccps->cblkw, (uint16_t)p_tcd->tcp->tccps->cblkh, &blocks, p_tcd->numThreads);
}


static bool tcd_dwt_decode ( tcd_t *p_tcd )
{
    tcd_tile_t * l_tile = p_tcd->tile;
    int64_t compno=0;
    bool rc = true;
#ifdef _OPENMP
    #pragma omp parallel default(none) private(compno) shared(p_tcd, l_tile, rc)
    {
        #pragma omp for
#endif
        for (compno = 0; compno < (int64_t)l_tile->numcomps; compno++) {
            tcd_tilecomp_t * l_tile_comp = l_tile->comps + compno;
            tccp_t * l_tccp = p_tcd->tcp->tccps + compno;
            opj_image_comp_t * l_img_comp = p_tcd->image->comps + compno;
            if (l_tccp->qmfbid == 1) {
				dwt53 dwt;
                if (! dwt.decode(l_tile_comp,
									l_img_comp->resno_decoded+1,
									p_tcd->numThreads)) {
                    rc = false;
                    continue;
                }
            } else {
				dwt97 dwt;
                if (! dwt.decode(l_tile_comp, 
											l_img_comp->resno_decoded+1,
											p_tcd->numThreads)) {
                    rc = false;
                    continue;
                }
            }
#ifdef _OPENMP
        }
#endif

    }

    return rc;
}
static bool tcd_mct_decode ( tcd_t *p_tcd, event_mgr_t *p_manager)
{
    tcd_tile_t * l_tile = p_tcd->tile;
    tcp_t * l_tcp = p_tcd->tcp;
    tcd_tilecomp_t * l_tile_comp = l_tile->comps;
    uint64_t l_samples,i;

    if (! l_tcp->mct) {
        return true;
    }

    l_samples = (uint64_t)(l_tile_comp->x1 - l_tile_comp->x0) * (l_tile_comp->y1 - l_tile_comp->y0);

    if (l_tile->numcomps >= 3 ) {
        /* testcase 1336.pdf.asan.47.376 */
        if ((l_tile->comps[0].x1 - l_tile->comps[0].x0) * (l_tile->comps[0].y1 - l_tile->comps[0].y0) < l_samples ||
                (l_tile->comps[1].x1 - l_tile->comps[1].x0) * (l_tile->comps[1].y1 - l_tile->comps[1].y0) < l_samples ||
                (l_tile->comps[2].x1 - l_tile->comps[2].x0) * (l_tile->comps[2].y1 - l_tile->comps[2].y0) < l_samples) {
            event_msg(p_manager, EVT_ERROR, "Tiles don't all have the same dimension. Skip the MCT step.\n");
            return false;
        } else if (l_tcp->mct == 2) {
            uint8_t ** l_data;

            if (! l_tcp->m_mct_decoding_matrix) {
                return true;
            }

            l_data = (uint8_t **) grok_malloc(l_tile->numcomps*sizeof(uint8_t*));
            if (! l_data) {
                return false;
            }

            for (i=0; i<l_tile->numcomps; ++i) {
                l_data[i] = (uint8_t*)tile_buf_get_ptr(l_tile_comp->buf, 0, 0, 0, 0);
                ++l_tile_comp;
            }

            if (! mct_decode_custom(/* MCT data */
                        (uint8_t*) l_tcp->m_mct_decoding_matrix,
                        /* size of components */
                        l_samples,
                        /* components */
                        l_data,
                        /* nb of components (i.e. size of pData) */
                        l_tile->numcomps,
                        /* tells if the data is signed */
                        p_tcd->image->comps->sgnd)) {
                grok_free(l_data);
                return false;
            }

            grok_free(l_data);
        } else {
            if (l_tcp->tccps->qmfbid == 1) {
                mct_decode(tile_buf_get_ptr(l_tile->comps[0].buf, 0, 0, 0, 0) ,
                               tile_buf_get_ptr(l_tile->comps[1].buf, 0, 0, 0, 0),
                               tile_buf_get_ptr(l_tile->comps[2].buf, 0, 0, 0, 0),
                               l_samples);
            } else {
                mct_decode_real((float*)tile_buf_get_ptr(l_tile->comps[0].buf, 0, 0, 0, 0),
                                    (float*)tile_buf_get_ptr(l_tile->comps[1].buf, 0, 0, 0, 0),
                                    (float*)tile_buf_get_ptr(l_tile->comps[2].buf, 0, 0, 0, 0),
                                    l_samples);
            }
        }
    } else {
        event_msg(p_manager, EVT_ERROR, "Number of components (%d) is inconsistent with a MCT. Skip the MCT step.\n",l_tile->numcomps);
    }

    return true;
}


static bool tcd_dc_level_shift_decode ( tcd_t *p_tcd )
{
    uint32_t compno=0;

#ifdef _OPENMP
#pragma omp parallel default(none) private(compno) shared(p_tcd)
	{
#pragma omp for
#endif

		for (compno = 0; compno < p_tcd->tile->numcomps; compno++) {
			int32_t l_min = INT32_MAX, l_max = INT32_MIN;

			tcd_tilecomp_t *l_tile_comp		= p_tcd->tile->comps + compno;
			tccp_t * l_tccp					= p_tcd->tcp->tccps + compno;
			opj_image_comp_t * l_img_comp		= p_tcd->image->comps + compno;

			uint32_t scaledTileX0 = uint_ceildivpow2((uint32_t)l_tile_comp->buf->tile_dim.x0, l_img_comp->decodeScaleFactor);
			uint32_t scaledTileY0 = uint_ceildivpow2((uint32_t)l_tile_comp->buf->tile_dim.y0, l_img_comp->decodeScaleFactor);

			uint32_t x0 = (uint_ceildivpow2((uint32_t)l_tile_comp->buf->dim.x0, l_img_comp->decodeScaleFactor) - scaledTileX0);
			uint32_t y0 = (uint_ceildivpow2((uint32_t)l_tile_comp->buf->dim.y0, l_img_comp->decodeScaleFactor) - scaledTileY0);
			uint32_t x1 = (uint_ceildivpow2((uint32_t)l_tile_comp->buf->dim.x1, l_img_comp->decodeScaleFactor) - scaledTileX0);
			uint32_t y1 = (uint_ceildivpow2((uint32_t)l_tile_comp->buf->dim.y1, l_img_comp->decodeScaleFactor) - scaledTileY0);

			uint32_t l_stride					= (l_tile_comp->x1 - l_tile_comp->x0) - (x1-x0);

			if (l_img_comp->sgnd) {
				l_min = -(1 << (l_img_comp->prec - 1));
				l_max = (1 << (l_img_comp->prec - 1)) - 1;
			}
			else {
				l_min = 0;
				l_max = (1 << l_img_comp->prec) - 1;
			}

			int32_t* l_current_ptr = tile_buf_get_ptr(l_tile_comp->buf, 0, 0, 0, 0);
			l_current_ptr += x0 + y0 * (l_tile_comp->x1 - l_tile_comp->x0);

			if (l_tccp->qmfbid == 1) {
				for (uint32_t j = y0; j < y1; ++j) {
					for (uint32_t i = x0; i < x1; ++i) {
						*l_current_ptr = int_clamp(*l_current_ptr + l_tccp->m_dc_level_shift, l_min, l_max);
						l_current_ptr++;
					}
					l_current_ptr += l_stride;
				}
			}
			else {
				for (uint32_t j = y0; j < y1; ++j) {
					for (uint32_t i = x0; i < x1; ++i) {
						float l_value	= *((float *)l_current_ptr);
						*l_current_ptr	= int_clamp((int32_t)grok_lrintf(l_value) + l_tccp->m_dc_level_shift, l_min, l_max); 
						l_current_ptr++;
					}
					l_current_ptr += l_stride;
				}
			}
		}

#ifdef _OPENMP
	}
#endif

    return true;
}


/**
 * Deallocates the encoding data of the given precinct.
 */
static void tcd_code_block_dec_deallocate (tcd_precinct_t * p_precinct)
{
    size_t cblkno , l_nb_code_blocks;
    tcd_cblk_dec_t * l_code_block = p_precinct->cblks.dec;
    if (l_code_block) {
        /*fprintf(stderr,"deallocate codeblock:{\n");*/
        /*fprintf(stderr,"\t x0=%d, y0=%d, x1=%d, y1=%d\n",l_code_block->x0, l_code_block->y0, l_code_block->x1, l_code_block->y1);*/
        /*fprintf(stderr,"\t numbps=%d, numlenbits=%d, len=%d, numPassesInPacket=%d, real_num_segs=%d, numSegmentsAllocated=%d\n ",
                        l_code_block->numbps, l_code_block->numlenbits, l_code_block->len, l_code_block->numPassesInPacket, l_code_block->numSegments, l_code_block->numSegmentsAllocated );*/


        l_nb_code_blocks = p_precinct->block_size / sizeof(tcd_cblk_dec_t);
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
static void tcd_code_block_enc_deallocate (tcd_precinct_t * p_precinct)
{
    size_t cblkno , l_nb_code_blocks;
    tcd_cblk_enc_t * l_code_block = p_precinct->cblks.enc;
    if (l_code_block) {
        l_nb_code_blocks = p_precinct->block_size / sizeof(tcd_cblk_enc_t);
        for     (cblkno = 0; cblkno < l_nb_code_blocks; ++cblkno)  {
			l_code_block->cleanup();
            ++l_code_block;
        }
        grok_free(p_precinct->cblks.enc);
        p_precinct->cblks.enc = nullptr;
    }
}

uint64_t tcd_get_encoded_tile_size ( tcd_t *p_tcd )
{
    uint32_t i = 0;
    opj_image_comp_t * l_img_comp = nullptr;
    tcd_tilecomp_t * l_tilec = nullptr;
	uint32_t l_size_comp, l_remaining;
	uint64_t l_data_size = 0;

    l_tilec = p_tcd->tile->comps;
    l_img_comp = p_tcd->image->comps;
    for (i=0; i<p_tcd->image->numcomps; ++i) {
        l_size_comp = l_img_comp->prec >> 3; /*(/ 8)*/
        l_remaining = l_img_comp->prec & 7;  /* (%8) */

        if (l_remaining) {
            ++l_size_comp;
        }

        if (l_size_comp == 3) {
            l_size_comp = 4;
        }

        l_data_size += l_size_comp * (uint64_t)(l_tilec->x1 - l_tilec->x0) * (l_tilec->y1 - l_tilec->y0);
        ++l_img_comp;
        ++l_tilec;
    }

    return l_data_size;
}

static bool tcd_dc_level_shift_encode ( tcd_t *p_tcd )
{
    uint32_t compno;
    tcd_tilecomp_t * l_tile_comp = nullptr;
    tccp_t * l_tccp = nullptr;
    opj_image_comp_t * l_img_comp = nullptr;
    tcd_tile_t * l_tile;
    uint64_t l_nb_elem,i;
    int32_t * l_current_ptr;

    l_tile = p_tcd->tile;
    l_tile_comp = l_tile->comps;
    l_tccp = p_tcd->tcp->tccps;
    l_img_comp = p_tcd->image->comps;

    for (compno = 0; compno < l_tile->numcomps; compno++) {
        l_current_ptr = tile_buf_get_ptr(l_tile_comp->buf, 0, 0, 0, 0);
        l_nb_elem = (uint64_t)(l_tile_comp->x1 - l_tile_comp->x0) * (l_tile_comp->y1 - l_tile_comp->y0);

        if (l_tccp->qmfbid == 1) {
            for (i = 0; i < l_nb_elem; ++i) {
                *l_current_ptr -= l_tccp->m_dc_level_shift ;
                ++l_current_ptr;
            }
        } else {
            for (i = 0; i < l_nb_elem; ++i) {
                *l_current_ptr = (*l_current_ptr - l_tccp->m_dc_level_shift) * (1 << 11) ;
                ++l_current_ptr;
            }
        }

        ++l_img_comp;
        ++l_tccp;
        ++l_tile_comp;
    }

    return true;
}

static bool tcd_mct_encode ( tcd_t *p_tcd )
{
    tcd_tile_t * l_tile = p_tcd->tile;
    tcd_tilecomp_t * l_tile_comp = p_tcd->tile->comps;
    uint64_t samples = (uint64_t)(l_tile_comp->x1 - l_tile_comp->x0) * (l_tile_comp->y1 - l_tile_comp->y0);
    uint32_t i;
    uint8_t ** l_data = nullptr;
    tcp_t * l_tcp = p_tcd->tcp;

    if(!p_tcd->tcp->mct) {
        return true;
    }

    if (p_tcd->tcp->mct == 2) {
        if (! p_tcd->tcp->m_mct_coding_matrix) {
            return true;
        }

        l_data = (uint8_t **) grok_malloc(l_tile->numcomps*sizeof(uint8_t*));
        if (! l_data) {
            return false;
        }

        for (i=0; i<l_tile->numcomps; ++i) {
            l_data[i] = (uint8_t*)tile_buf_get_ptr(l_tile_comp->buf, 0, 0, 0, 0);
            ++l_tile_comp;
        }

        if (! mct_encode_custom(/* MCT data */
                    (uint8_t*) p_tcd->tcp->m_mct_coding_matrix,
                    /* size of components */
                    samples,
                    /* components */
                    l_data,
                    /* nb of components (i.e. size of pData) */
                    l_tile->numcomps,
                    /* tells if the data is signed */
                    p_tcd->image->comps->sgnd) ) {
            grok_free(l_data);
            return false;
        }

        grok_free(l_data);
    } else if (l_tcp->tccps->qmfbid == 0) {
        mct_encode_real(tile_buf_get_ptr(l_tile->comps[0].buf, 0, 0, 0, 0),
                            tile_buf_get_ptr(l_tile->comps[1].buf, 0, 0, 0, 0),
                            tile_buf_get_ptr(l_tile->comps[2].buf, 0, 0, 0, 0),
                            samples);
    } else {
        mct_encode(tile_buf_get_ptr(l_tile->comps[0].buf, 0, 0, 0, 0),
                       tile_buf_get_ptr(l_tile->comps[1].buf, 0, 0, 0, 0),
                       tile_buf_get_ptr(l_tile->comps[2].buf, 0, 0, 0, 0),
                       samples);
    }

    return true;
}


bool tcd_dwt_encode ( tcd_t *p_tcd )
{
    tcd_tile_t * l_tile = p_tcd->tile;
    int64_t compno=0;
    bool rc = true;
#ifdef _OPENMP
    #pragma omp parallel default(none) private(compno) shared(p_tcd, l_tile, rc)
    {
        #pragma omp for
#endif
        for (compno = 0; compno < (int64_t)l_tile->numcomps; ++compno) {
            tcd_tilecomp_t * tile_comp = p_tcd->tile->comps + compno;
            tccp_t * l_tccp = p_tcd->tcp->tccps + compno;
            if (l_tccp->qmfbid == 1) {
				dwt53 dwt;
                if (! dwt.encode(tile_comp)) {
                    rc = false;
                    continue;
                }
            } else if (l_tccp->qmfbid == 0) {
				dwt97 dwt;
                if (! dwt.encode(tile_comp)) {
                    rc = false;
                    continue;
                }
            }
        }
#ifdef _OPENMP
    }
#endif

    return rc;
}

static bool tcd_t1_encode ( tcd_t *p_tcd )
{
    const double * l_mct_norms;
    uint32_t l_mct_numcomps = 0U;
    tcp_t * l_tcp = p_tcd->tcp;

    if (l_tcp->mct == 1) {
        l_mct_numcomps = 3U;
        /* irreversible encoding */
        if (l_tcp->tccps->qmfbid == 0) {
            l_mct_norms = mct_get_mct_norms_real();
        } else {
            l_mct_norms = mct_get_mct_norms();
        }
    } else {
        l_mct_numcomps = p_tcd->image->numcomps;
        l_mct_norms = (const double *) (l_tcp->mct_norms);
    }

	auto t1_wrap = std::unique_ptr<Tier1>(new Tier1());

    return t1_wrap->encodeCodeblocks(l_tcp,
									p_tcd->tile,
									l_mct_norms,
									l_mct_numcomps,
									p_tcd->numThreads, 
		tcd_needs_rate_control(p_tcd->tcp,  &p_tcd->cp->m_specific_param.m_enc));
}

static bool tcd_t2_encode (tcd_t *p_tcd,
								GrokStream* p_stream,
                               uint64_t * p_data_written,
                               uint64_t p_max_dest_size,
                               opj_codestream_info_t *p_cstr_info,
								event_mgr_t * p_manager)
{
    t2_t * l_t2;

    l_t2 = t2_create(p_tcd->image, p_tcd->cp);
    if (l_t2 == nullptr) {
        return false;
    }
#ifdef DEBUG_LOSSLESS_T2
	for (uint32_t compno = 0; compno < p_tcd->image->numcomps; ++compno) {
		tcd_tilecomp_t *tilec = &p_tcd->tile->comps[compno];
		tilec->round_trip_resolutions = new tcd_resolution_t[tilec->numresolutions];
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
				decodeBand->precincts = new tcd_precinct_t[band->numPrecincts];
				decodeBand->precincts_data_size = (uint32_t)(band->numPrecincts * sizeof(tcd_precinct_t));
				for (size_t precno = 0; precno < band->numPrecincts; ++precno) {
					auto prec = band->precincts + precno;
					auto decodePrec = decodeBand->precincts + precno;
					decodePrec->cw = prec->cw;
					decodePrec->ch = prec->ch;
					if (prec->cblks.enc && prec->cw && prec->ch) {
						decodePrec->initTagTrees(p_manager);
						decodePrec->cblks.dec = new tcd_cblk_dec_t[decodePrec->cw * decodePrec->ch];
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

    if (! t2_encode_packets(
                l_t2,
                p_tcd->tcd_tileno,
                p_tcd->tile,
                p_tcd->tcp->numlayers,
				p_stream,
                p_data_written,
                p_max_dest_size,
                p_cstr_info,
                p_tcd->tp_num,
                p_tcd->tp_pos,
                p_tcd->cur_pino,
				p_manager)) {
        t2_destroy(l_t2);
        return false;
    }

    t2_destroy(l_t2);

#ifdef DEBUG_LOSSLESS_T2
	for (uint32_t compno = 0; compno < p_tcd->image->numcomps; ++compno) {
		tcd_tilecomp_t *tilec = &p_tcd->tile->comps[compno];
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


static bool tcd_rate_allocate_encode(  tcd_t *p_tcd,
											uint64_t p_max_dest_size,
											opj_codestream_info_t *p_cstr_info,
											event_mgr_t * p_manager)
{
    cp_t * l_cp = p_tcd->cp;
    uint64_t l_nb_written = 0;

    if (p_cstr_info)  {
        p_cstr_info->index_write = 0;
    }
	
    if (l_cp->m_specific_param.m_enc.m_disto_alloc|| l_cp->m_specific_param.m_enc.m_fixed_quality)  {
        // rate control by rate/distortion or fixed quality 
		switch (l_cp->m_specific_param.m_enc.rateControlAlgorithm) {
		case 0:
			if (!tcd_pcrd_bisect_simple(p_tcd, &l_nb_written, p_max_dest_size, p_manager)) {
				return false;
			}
			break;
		case 1:
			if (!tcd_pcrd_bisect_feasible(p_tcd, &l_nb_written, p_max_dest_size, p_manager)) {
				return false;
			}
			break;
		default:
			if (!tcd_pcrd_bisect_feasible(p_tcd, &l_nb_written, p_max_dest_size, p_manager)) {
				return false;
			}
			break;

		}

    } 

    return true;
}


bool tcd_copy_tile_data (       tcd_t *p_tcd,
                                    uint8_t * p_src,
                                    uint64_t p_src_length )
{
    uint64_t i,j;
    opj_image_comp_t * l_img_comp = nullptr;
    tcd_tilecomp_t * l_tilec = nullptr;
    uint32_t l_size_comp, l_remaining;
    uint64_t l_nb_elem;
	uint64_t l_data_size = tcd_get_encoded_tile_size(p_tcd);
    if (l_data_size != p_src_length) {
        return false;
    }

    l_tilec = p_tcd->tile->comps;
    l_img_comp = p_tcd->image->comps;
    for (i=0; i<p_tcd->image->numcomps; ++i) {
        l_size_comp = l_img_comp->prec >> 3; /*(/ 8)*/
        l_remaining = l_img_comp->prec & 7;  /* (%8) */
        l_nb_elem = (uint64_t)(l_tilec->x1 - l_tilec->x0) * (l_tilec->y1 - l_tilec->y0);

        if (l_remaining) {
            ++l_size_comp;
        }

        if (l_size_comp == 3) {
            l_size_comp = 4;
        }

        switch (l_size_comp) {
        case 1: {
            char * l_src_ptr = (char *) p_src;
            int32_t * l_dest_ptr = l_tilec->buf->data;

            if (l_img_comp->sgnd) {
                for (j=0; j<l_nb_elem; ++j) {
                    *(l_dest_ptr++) = (int32_t) (*(l_src_ptr++));
                }
            } else {
                for (j=0; j<l_nb_elem; ++j) {
                    *(l_dest_ptr++) = (*(l_src_ptr++))&0xff;
                }
            }

            p_src = (uint8_t*) l_src_ptr;
        }
        break;
        case 2: {
            int32_t * l_dest_ptr = l_tilec->buf->data;
            int16_t * l_src_ptr = (int16_t *) p_src;

            if (l_img_comp->sgnd) {
                for (j=0; j<l_nb_elem; ++j) {
                    *(l_dest_ptr++) = (int32_t) (*(l_src_ptr++));
                }
            } else {
                for (j=0; j<l_nb_elem; ++j) {
                    *(l_dest_ptr++) = (*(l_src_ptr++))&0xffff;
                }
            }

            p_src = (uint8_t*) l_src_ptr;
        }
        break;
        case 4: {
            int32_t * l_src_ptr = (int32_t *) p_src;
            int32_t * l_dest_ptr = l_tilec->buf->data;

            for (j=0; j<l_nb_elem; ++j) {
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


tcd_cblk_enc_t::~tcd_cblk_enc_t() {
	cleanup();
}
bool tcd_cblk_enc_t::alloc() {
	if (!layers) {
		/* no memset since data */
		layers = (tcd_layer_t*)grok_calloc(100, sizeof(tcd_layer_t));
		if (!layers) {
			return false;
		}
	}
	if (!passes) {
		passes = (tcd_pass_t*)grok_calloc(100, sizeof(tcd_pass_t));
		if (!passes) {
			return false;
		}
	}
#ifdef DEBUG_LOSSLESS_T2
	packet_length_info = new std::vector<packet_length_info_t>();
#endif
	return true;
}


/**
* Allocates data memory for an encoding code block.
* We actually allocate 1 more bytes than specified, and then offset data by +1.
* This is done so that we can safely initialize the MQ coder pointer to data-1,
* without risk of accessing uninitialized memory.
*/
bool tcd_cblk_enc_t::alloc_data(size_t nominalBlockSize){
	uint32_t l_data_size = (uint32_t)(nominalBlockSize * sizeof(uint32_t));
	if (l_data_size > data_size) {
		if (owns_data && actualData) {
			grok_free(actualData);
		}
		actualData = new uint8_t[l_data_size + cblk_compressed_data_pad_left];
		data = actualData+ cblk_compressed_data_pad_left;
		data_size = l_data_size;
		owns_data = true;
	}
	return true;
}

void tcd_cblk_enc_t::cleanup() {
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

bool tcd_cblk_dec_t::alloc() {
	if (!segs) {
		segs = (tcd_seg_t *)grok_calloc(default_numbers_segments, sizeof(tcd_seg_t));
		if (!segs) {
			return false;
		}
		/*fprintf(stderr, "Allocate %d elements of code_block->data\n", default_numbers_segments * sizeof(tcd_seg_t));*/

		numSegmentsAllocated = default_numbers_segments;

		/*fprintf(stderr, "Allocate 8192 elements of code_block->data\n");*/
		/*fprintf(stderr, "numSegmentsAllocated of code_block->data = %d\n", p_code_block->numSegmentsAllocated);*/

#ifdef DEBUG_LOSSLESS_T2
		packet_length_info = new std::vector<packet_length_info_t>();
#endif
	}
	else {
		/* sanitize */
		tcd_seg_t * l_segs = segs;
		uint32_t l_current_max_segs = numSegmentsAllocated;

		/* Note: since seg_buffers simply holds references to another data buffer,
		we do not need to copy it  to the sanitized block  */
		seg_buffers.cleanup();

		memset(this, 0, sizeof(tcd_cblk_dec_t));
		segs = l_segs;
		numSegmentsAllocated = l_current_max_segs;
	}
	return true;
}

void tcd_cblk_dec_t::cleanup() {
	seg_buffers.cleanup();
	if (segs) {
		grok_free(segs);
		segs = nullptr;
	}
#ifdef DEBUG_LOSSLESS_T2
	delete packet_length_info;
	packet_length_info = nullptr;
#endif
}
void tcd_precinct_t::initTagTrees(event_mgr_t* manager) {
	
	// if l_current_precinct->cw == 0 or l_current_precinct->ch == 0, then the precinct has no code blocks, therefore 
	// no need for inclusion and msb tag trees
	if (cw > 0 && ch > 0) {
		if (!incltree) {
			try {
				incltree = new TagTree(cw, ch, manager);
			}
			catch (std::exception e) {
				event_msg(manager, EVT_WARNING, "No incltree created.\n");
			}
		}
		else {
			if (!incltree->init(cw, ch, manager)) {
				event_msg(manager, EVT_WARNING, "Failed to re-initialize incltree.\n");
				delete incltree;
				incltree = nullptr;
			}
		}

		if (!imsbtree) {
			try {
				imsbtree = new TagTree(cw, ch, manager);
			}
			catch (std::exception e) {
				event_msg(manager, EVT_WARNING, "No imsbtree created.\n");
			}
		}
		else {
			if (!imsbtree->init(cw, ch, manager)) {
				event_msg(manager, EVT_WARNING, "Failed to re-initialize imsbtree.\n");
				delete imsbtree;
				imsbtree = nullptr;
			}
		}
	}
}
}

