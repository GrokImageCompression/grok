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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "grk_includes.h"
#include "Tier1.h"
#include <memory>
#include "WaveletForward.h"
#include <algorithm>
#include <exception>
#include "t1_common.h"
#include <numeric>

using namespace std;

namespace grk {

TileProcessor::TileProcessor(CodeStream *codeStream, BufferedStream *stream) :
				 m_tile_index(0),
				 m_poc_tile_part_index(0),
				 m_tile_part_index(0),
				 tile_part_data_length(0),
				totnum_tp(0),
				pino(0),
				tile(nullptr),
				image(codeStream->m_input_image),
				current_plugin_tile(codeStream->current_plugin_tile),
				whole_tile_decoding(codeStream->whole_tile_decoding),
				plt_markers(nullptr),
				m_cp(&codeStream->m_cp),
				m_resno_decoded_per_component(nullptr),
				m_stream(stream),
				tp_pos(0),
				m_tcp(nullptr),
				m_corrupt_packet(false)
{
	assert(stream);
	tile = (grk_tile*) grk_calloc(1, sizeof(grk_tile));
	if (!tile)
		throw new std::runtime_error("out of memory");

	tile->comps = new TileComponent[image->numcomps];
	m_resno_decoded_per_component = new uint32_t[image->numcomps];
	memset(m_resno_decoded_per_component,0, image->numcomps * sizeof(uint32_t));
	tile->numcomps = image->numcomps;

	tp_pos = m_cp->m_coding_params.m_enc.m_tp_pos;
}

TileProcessor::~TileProcessor() {
	if (tile) {
		delete[] tile->comps;
		grk_free(tile);
	}
	delete plt_markers;
	delete[] m_resno_decoded_per_component;
}

/*
 if
 - r xx, yy, zz, 0   (disto_alloc == 1 and rates == 0)
 or
 - q xx, yy, zz, 0   (fixed_quality == 1 and distoratio == 0)

 then don't try to find an optimal threshold but rather take everything not included yet.

 It is possible to have some lossy layers and the last layer always lossless

 */
bool TileProcessor::layer_needs_rate_control(uint32_t layno) {

	auto enc_params = &m_cp->m_coding_params.m_enc;
	return (enc_params->m_disto_alloc && (m_tcp->rates[layno] > 0.0))
			|| (enc_params->m_fixed_quality
					&& (m_tcp->distoratio[layno] > 0.0f));
}

bool TileProcessor::needs_rate_control() {
	for (uint16_t i = 0; i < m_tcp->numlayers; ++i) {
		if (layer_needs_rate_control(i))
			return true;
	}
	return false;
}

// lossless in the sense that no code passes are removed; it mays still be a lossless layer
// due to irreversible DWT and quantization
bool TileProcessor::make_single_lossless_layer() {
	if (m_tcp->numlayers == 1 && !layer_needs_rate_control(0)) {
		makelayer_final(0);
		return true;
	}
	return false;
}

void TileProcessor::makelayer_feasible(uint32_t layno, uint16_t thresh,
		bool final) {
	uint32_t compno, resno, bandno;
	uint64_t precno, cblkno;
	uint32_t passno;
	tile->distolayer[layno] = 0;
	for (compno = 0; compno < tile->numcomps; compno++) {
		auto tilec = tile->comps + compno;
		for (resno = 0; resno < tilec->numresolutions; resno++) {
			auto res = tilec->resolutions + resno;
			for (bandno = 0; bandno < res->numbands; bandno++) {
				auto band = res->bands + bandno;
				for (precno = 0; precno < (uint64_t)res->pw * res->ph; precno++) {
					auto prc = band->precincts + precno;
					for (cblkno = 0; (uint64_t)cblkno < prc->cw * prc->ch; cblkno++) {
						auto cblk = prc->enc + cblkno;
						auto layer = cblk->layers + layno;
						uint32_t cumulative_included_passes_in_block;

						if (layno == 0)
							cblk->numPassesInPreviousPackets = 0;

						cumulative_included_passes_in_block =
								cblk->numPassesInPreviousPackets;

						for (passno =
								cblk->numPassesInPreviousPackets;
								passno < cblk->numPassesTotal; passno++) {
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
								- cblk->numPassesInPreviousPackets;

						if (!layer->numpasses) {
							layer->disto = 0;
							continue;
						}

						// update layer
						if (cblk->numPassesInPreviousPackets == 0) {
							layer->len =
									cblk->passes[cumulative_included_passes_in_block
											- 1].rate;
							layer->data = cblk->paddedCompressedData;
							layer->disto =
									cblk->passes[cumulative_included_passes_in_block
											- 1].distortiondec;
						} else {
							layer->len =
									cblk->passes[cumulative_included_passes_in_block
											- 1].rate
											- cblk->passes[cblk->numPassesInPreviousPackets
													- 1].rate;
							layer->data =
									cblk->paddedCompressedData
											+ cblk->passes[cblk->numPassesInPreviousPackets
													- 1].rate;
							layer->disto =
									cblk->passes[cumulative_included_passes_in_block
											- 1].distortiondec
											- cblk->passes[cblk->numPassesInPreviousPackets
													- 1].distortiondec;
						}

						tile->distolayer[layno] += layer->disto;
						if (final)
							cblk->numPassesInPreviousPackets =
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
bool TileProcessor::pcrd_bisect_feasible(uint32_t *all_packets_len) {

	bool single_lossless = make_single_lossless_layer();
	double cumdisto[100];
	const double K = 1;
	double maxSE = 0;

	auto tcp = m_tcp;

	tile->numpix = 0;
	uint32_t state = grk_plugin_get_debug_state();

	RateInfo rateInfo;
	for (uint32_t compno = 0; compno < tile->numcomps; compno++) {
		auto tilec = &tile->comps[compno];
		tilec->numpix = 0;
		for (uint32_t resno = 0; resno < tilec->numresolutions; resno++) {
			auto res = &tilec->resolutions[resno];
			for (uint32_t bandno = 0; bandno < res->numbands; bandno++) {
				auto band = &res->bands[bandno];
				for (uint64_t precno = 0; (uint64_t)precno < res->pw * res->ph;
						precno++) {
					auto prc = &band->precincts[precno];
					for (uint64_t cblkno = 0; cblkno < (uint64_t)prc->cw * prc->ch;
							cblkno++) {
						auto cblk = &prc->enc[cblkno];
						uint32_t numPix = cblk->area();
						if (!(state & GRK_PLUGIN_STATE_PRE_TR1)) {
							encode_synch_with_plugin(this, compno, resno,
									bandno, precno, cblkno, band, cblk,
									&numPix);
						}

						if (!single_lossless) {
							RateControl::convexHull(cblk->passes,
									cblk->numPassesTotal);
							rateInfo.synch(cblk);

							tile->numpix += numPix;
							tilec->numpix += numPix;
						}
					} /* cbklno */
				} /* precno */
			} /* bandno */
		} /* resno */

		if (!single_lossless) {
			maxSE += (double) (((uint64_t) 1 << image->comps[compno].prec) - 1)
					* (double) (((uint64_t) 1 << image->comps[compno].prec) - 1)
					* (double) tilec->numpix;
		}
	} /* compno */

	if (single_lossless) {
		makelayer_final(0);
		if (plt_markers) {
			auto t2 = new T2Encode(this);
			uint32_t sim_all_packets_len = 0;
			t2->encode_packets_simulate(m_tile_index,
										0 + 1, &sim_all_packets_len, UINT_MAX,
										tp_pos, plt_markers);
			delete t2;
		}
		return true;
	}

	uint32_t min_slope = rateInfo.getMinimumThresh();
	uint32_t max_slope = USHRT_MAX;

	uint32_t upperBound = max_slope;
	for (uint16_t layno = 0; layno < tcp->numlayers; layno++) {
		uint32_t lowerBound = min_slope;
		uint32_t maxlen =
				tcp->rates[layno] > 0.0f ?
						((uint32_t) ceil(tcp->rates[layno])) : UINT_MAX;


		if (layer_needs_rate_control(layno)) {
			auto t2 = new T2Encode(this);
			// thresh from previous iteration - starts off uninitialized
			// used to bail out if difference with current thresh is small enough
			uint32_t prevthresh = 0;
			double distotarget = tile->distotile
					- ((K * maxSE)
							/ pow(10.0, tcp->distoratio[layno] / 10.0));

			for (uint32_t i = 0; i < 128; ++i) {
				uint32_t thresh = (lowerBound + upperBound) >> 1;
				if (prevthresh != 0 && prevthresh == thresh)
					break;
				makelayer_feasible(layno, (uint16_t) thresh, false);
				prevthresh = thresh;
				if (m_cp->m_coding_params.m_enc.m_fixed_quality) {
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
					if (!t2->encode_packets_simulate(m_tile_index,
							layno + 1, all_packets_len, maxlen,
							tp_pos, nullptr)) {
						lowerBound = thresh;
						continue;
					}
					upperBound = thresh;
				}
			}
			// choose conservative value for goodthresh
			/* Threshold for Marcela Index */
			// start by including everything in this layer
			uint32_t goodthresh = upperBound;
			delete t2;

			makelayer_feasible(layno, (uint16_t) goodthresh, true);
			cumdisto[layno] =
					(layno == 0) ?
							tile->distolayer[0] :
							(cumdisto[layno - 1] + tile->distolayer[layno]);
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
bool TileProcessor::pcrd_bisect_simple(uint32_t *all_packets_len) {
	uint32_t compno, resno, bandno;
	uint16_t layno;
	uint64_t precno, cblkno;
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
				for (precno = 0; precno < (uint64_t)res->pw * res->ph; precno++) {
					auto prc = &band->precincts[precno];
					for (cblkno = 0; cblkno < (uint64_t)prc->cw * prc->ch; cblkno++) {
						auto cblk = &prc->enc[cblkno];
						uint32_t numPix = cblk->area();
						if (!(state & GRK_PLUGIN_STATE_PRE_TR1)) {
							encode_synch_with_plugin(this, compno, resno,
									bandno, precno, cblkno, band, cblk,
									&numPix);
						}
						if (!single_lossless) {
							for (passno = 0; passno < cblk->numPassesTotal;
									passno++) {
								grk_pass *pass = &cblk->passes[passno];
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

								if (dr == 0)
									continue;

								rdslope = dd / dr;
								if (rdslope < min_slope)
									min_slope = rdslope;
								if (rdslope > max_slope)
									max_slope = rdslope;
							} /* passno */
							tile->numpix += numPix;
							tilec->numpix += numPix;
						}

					} /* cbklno */
				} /* precno */
			} /* bandno */
		} /* resno */

		if (!single_lossless)
			maxSE += (double) (((uint64_t) 1 << image->comps[compno].prec) - 1)
					* (double) (((uint64_t) 1 << image->comps[compno].prec) - 1)
					* (double) tilec->numpix;

	} /* compno */
	if (single_lossless){
		if (plt_markers) {
			auto t2 = new T2Encode(this);
			uint32_t sim_all_packets_len = 0;
			t2->encode_packets_simulate(m_tile_index,
										0 + 1, &sim_all_packets_len, UINT_MAX,
										tp_pos, plt_markers);
			delete t2;
		}

		return true;
	}


	double upperBound = max_slope;
	for (layno = 0; layno < m_tcp->numlayers; layno++) {
		if (layer_needs_rate_control(layno)) {
			double lowerBound = min_slope;
			uint32_t maxlen =
					m_tcp->rates[layno] > 0.0f ?
							(uint32_t) ceil(m_tcp->rates[layno]) :	UINT_MAX;

			/* Threshold for Marcela Index */
			// start by including everything in this layer
			double goodthresh = 0;

			// thresh from previous iteration - starts off uninitialized
			// used to bail out if difference with current thresh is small enough
			double prevthresh = -1;
			double distotarget =
					tile->distotile
							- ((K * maxSE)
									/ pow(10.0, m_tcp->distoratio[layno] / 10.0));

			auto t2 = new T2Encode(this);
			double thresh;
			for (uint32_t i = 0; i < 128; ++i) {
				thresh =
						(upperBound == -1) ?
								lowerBound : (lowerBound + upperBound) / 2;
				make_layer_simple(layno, thresh, false);
				if (prevthresh != -1 && (fabs(prevthresh - thresh)) < 0.001)
					break;
				prevthresh = thresh;
				if (m_cp->m_coding_params.m_enc.m_fixed_quality) {
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
					if (!t2->encode_packets_simulate(m_tile_index, layno + 1,
							all_packets_len, maxlen,
							tp_pos, nullptr)) {
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
static void prepareBlockForFirstLayer(grk_cblk_enc *cblk) {
	cblk->numPassesInPreviousPackets = 0;
	cblk->numPassesInPacket = 0;
	cblk->numlenbits = 0;
}

/*
 Form layer for bisect rate control algorithm
 */
void TileProcessor::make_layer_simple(uint32_t layno, double thresh,
		bool final) {
	tile->distolayer[layno] = 0;
	for (uint32_t compno = 0; compno < tile->numcomps; compno++) {
		auto tilec = tile->comps + compno;
		for (uint32_t resno = 0; resno < tilec->numresolutions; resno++) {
			auto res = tilec->resolutions + resno;
			for (uint32_t bandno = 0; bandno < res->numbands; bandno++) {
				auto band = res->bands + bandno;
				for (uint64_t precno = 0; precno < (uint64_t)res->pw * res->ph; precno++) {
					auto prc = band->precincts + precno;
					for (uint64_t cblkno = 0; cblkno < (uint64_t)prc->cw * prc->ch; cblkno++) {
						auto cblk = prc->enc + cblkno;
						auto layer = cblk->layers + layno;
						uint32_t cumulative_included_passes_in_block;
						if (layno == 0)
							prepareBlockForFirstLayer(cblk);
						if (thresh == 0) {
							cumulative_included_passes_in_block =
									cblk->numPassesTotal;
						} else {
							cumulative_included_passes_in_block =
									cblk->numPassesInPreviousPackets;
							for (uint32_t passno =
									cblk->numPassesInPreviousPackets;
									passno < cblk->numPassesTotal;
									passno++) {
								uint32_t dr;
								double dd;
								grk_pass *pass = &cblk->passes[passno];
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
								- cblk->numPassesInPreviousPackets;
						if (!layer->numpasses) {
							layer->disto = 0;
							continue;
						}

						// update layer
						if (cblk->numPassesInPreviousPackets == 0) {
							layer->len =
									cblk->passes[cumulative_included_passes_in_block
											- 1].rate;
							layer->data = cblk->paddedCompressedData;
							layer->disto =
									cblk->passes[cumulative_included_passes_in_block
											- 1].distortiondec;
						} else {
							layer->len =
									cblk->passes[cumulative_included_passes_in_block
											- 1].rate
											- cblk->passes[cblk->numPassesInPreviousPackets
													- 1].rate;
							layer->data =
									cblk->paddedCompressedData
											+ cblk->passes[cblk->numPassesInPreviousPackets
													- 1].rate;
							layer->disto =
									cblk->passes[cumulative_included_passes_in_block
											- 1].distortiondec
											- cblk->passes[cblk->numPassesInPreviousPackets
													- 1].distortiondec;
						}

						tile->distolayer[layno] += layer->disto;
						if (final)
							cblk->numPassesInPreviousPackets =
									cumulative_included_passes_in_block;
					}
				}
			}
		}
	}
}

// Add all remaining passes to this layer
void TileProcessor::makelayer_final(uint32_t layno) {
	tile->distolayer[layno] = 0;
	for (uint32_t compno = 0; compno < tile->numcomps; compno++) {
		auto tilec = tile->comps + compno;
		for (uint32_t resno = 0; resno < tilec->numresolutions; resno++) {
			auto res = tilec->resolutions + resno;
			for (uint32_t bandno = 0; bandno < res->numbands; bandno++) {
				auto band = res->bands + bandno;
				for (uint64_t precno = 0; precno < (uint64_t)res->pw * res->ph; precno++) {
					auto prc = band->precincts + precno;
					for (uint64_t cblkno = 0; cblkno < (uint64_t)prc->cw * prc->ch; cblkno++) {
						auto cblk = prc->enc + cblkno;
						auto layer = cblk->layers + layno;
						if (layno == 0)
							prepareBlockForFirstLayer(cblk);
						uint32_t cumulative_included_passes_in_block =
								cblk->numPassesInPreviousPackets;
						if (cblk->numPassesTotal
								> cblk->numPassesInPreviousPackets)
							cumulative_included_passes_in_block =
									cblk->numPassesTotal;

						layer->numpasses = cumulative_included_passes_in_block
								- cblk->numPassesInPreviousPackets;

						if (!layer->numpasses) {
							layer->disto = 0;
							continue;
						}
						// update layer
						if (cblk->numPassesInPreviousPackets == 0) {
							layer->len =
									cblk->passes[cumulative_included_passes_in_block
											- 1].rate;
							layer->data = cblk->paddedCompressedData;
							layer->disto =
									cblk->passes[cumulative_included_passes_in_block
											- 1].distortiondec;
						} else {
							layer->len =
									cblk->passes[cumulative_included_passes_in_block
											- 1].rate
											- cblk->passes[cblk->numPassesInPreviousPackets
													- 1].rate;
							layer->data =
									cblk->paddedCompressedData
											+ cblk->passes[cblk->numPassesInPreviousPackets
													- 1].rate;
							layer->disto =
									cblk->passes[cumulative_included_passes_in_block
											- 1].distortiondec
											- cblk->passes[cblk->numPassesInPreviousPackets
													- 1].distortiondec;
						}
						tile->distolayer[layno] += layer->disto;
						cblk->numPassesInPreviousPackets =
								cumulative_included_passes_in_block;
						assert(cblk->numPassesInPreviousPackets
										== cblk->numPassesTotal);
					}
				}
			}
		}
	}
}

bool TileProcessor::init_tile(grk_image *output_image,
		bool isEncoder) {
	uint32_t state = grk_plugin_get_debug_state();
	auto tcp = &(m_cp->tcps[m_tile_index]);

	if (tcp->m_tile_data)
		tcp->m_tile_data->rewind();

	uint32_t p = m_tile_index % m_cp->t_grid_width; /* tile coordinates */
	uint32_t q = m_tile_index / m_cp->t_grid_width;
	/*fprintf(stderr, "Tile coordinate = %u,%u\n", p, q);*/

	/* 4 borders of the tile rescale on the image if necessary */
	uint32_t tx0 = m_cp->tx0 + p * m_cp->t_width; /* can't be greater than image->x1 so won't overflow */
	tile->x0 = std::max<uint32_t>(tx0, image->x0);
	tile->x1 = std::min<uint32_t>(uint_adds(tx0, m_cp->t_width), image->x1);
	if (tile->x1 <= tile->x0) {
		GRK_ERROR("Tile x0 coordinate %u must be "
				"<= tile x1 coordinate %u", tile->x0, tile->x1);
		return false;
	}
	uint32_t ty0 = m_cp->ty0 + q * m_cp->t_height; /* can't be greater than image->y1 so won't overflow */
	tile->y0 = std::max<uint32_t>(ty0, image->y0);
	tile->y1 = std::min<uint32_t>(uint_adds(ty0, m_cp->t_height), image->y1);
	if (tile->y1 <= tile->y0) {
		GRK_ERROR("Tile y0 coordinate %u must be "
				"<= tile y1 coordinate %u", tile->y0, tile->y1);
		return false;
	}
	/*fprintf(stderr, "Tile border = %u,%u,%u,%u\n", tile->x0, tile->y0,tile->x1,tile->y1);*/

	/* testcase 1888.pdf.asan.35.988 */
	if (tcp->tccps->numresolutions == 0) {
		GRK_ERROR("tiles require at least one resolution");
		return false;
	}

	for (uint32_t compno = 0; compno < tile->numcomps; ++compno) {
		auto image_comp = image->comps + compno;
		/*fprintf(stderr, "compno = %u/%u\n", compno, tile->numcomps);*/
		if (image_comp->dx == 0 || image_comp->dy == 0)
			return false;
		auto tilec = tile->comps + compno;
		if (!tilec->init(isEncoder, whole_tile_decoding, output_image, m_cp,
				tcp, tile, image_comp, tcp->tccps + compno,
				current_plugin_tile)) {
			return false;
		}
	} /* compno */

	// decoder plugin debug sanity check on tile struct
	if (!isEncoder) {
		if (state & GRK_PLUGIN_STATE_DEBUG) {
			if (!tile_equals(current_plugin_tile, tile))
				GRK_WARN("plugin tile differs from grok tile", nullptr);
		}
	}
	tile->packno = 0;

	if (isEncoder) {
        uint64_t max_precincts=0;
		for (uint32_t compno = 0; compno < image->numcomps; ++compno) {
			TileComponent *tilec = &tile->comps[compno];
			for (uint32_t resno = 0; resno < tilec->numresolutions; ++resno) {
				auto res = tilec->resolutions + resno;
				for (uint32_t bandno = 0; bandno < res->numbands; ++bandno) {
					auto band = res->bands + bandno;
					max_precincts = max<uint64_t>(max_precincts, band->numPrecincts);
				}
			}
		}
		m_packetTracker.init(tile->numcomps,
						tile->comps->numresolutions,
						max_precincts,
						tcp->numlayers);
	}
	return true;
}

bool TileProcessor::do_encode(void){
	uint32_t state = grk_plugin_get_debug_state();
	if (state & GRK_PLUGIN_STATE_DEBUG)
		set_context_stream(this);

	m_tcp = &m_cp->tcps[m_tile_index];

	// When debugging the encoder, we do all of T1 up to and including DWT
	// in the plugin, and pass this in as image data.
	// This way, both Grok and plugin start with same inputs for
	// context formation and MQ coding.
	bool debugEncode = state & GRK_PLUGIN_STATE_DEBUG;
	bool debugMCT = (state & GRK_PLUGIN_STATE_MCT_ONLY) ? true : false;

	if (!current_plugin_tile || debugEncode) {
		if (!debugEncode) {
			if (!dc_level_shift_encode())
				return false;
			if (!mct_encode())
				return false;
		}
		if (!debugEncode || debugMCT) {
			if (!dwt_encode())
				return false;
		}
		t1_encode();
	}

	if (!pre_compress_first_tile_part()) {
		GRK_ERROR("Cannot compress tile");
		return false;
	}

	return true;
}

bool TileProcessor::pre_compress_first_tile_part(void) {
	if (m_tile_part_index == 0) {

		// 1. create PLT marker if required
		delete plt_markers;
		if (m_cp->m_coding_params.m_enc.writePLT){
			if (!needs_rate_control())
				plt_markers = new PacketLengthMarkers(m_stream);
			else
				GRK_WARN("PLT marker generation disabled due to rate control.");
		}
		// 2. rate control
		if (!rate_allocate())
			return false;
		m_packetTracker.clear();
	}

	return true;
}

bool TileProcessor::compress_tile_part(	uint32_t *tile_bytes_written) {

	//4 write PLT for first tile part
	if (m_tile_part_index == 0 && plt_markers){
		uint32_t written = plt_markers->write();
		*tile_bytes_written += written;
	}

	//3 write SOD
	if (!m_stream->write_short(J2K_MS_SOD))
		return false;

	*tile_bytes_written += 2;

	return t2_encode(tile_bytes_written);
}

/** Returns whether a tile component should be fully decoded,
 * taking into account win_* members.
 *
 * @param compno Component number
 * @return true if the tile component should be fully decoded
 */
bool TileProcessor::is_whole_tilecomp_decoding(uint32_t compno) {
	auto tilec = tile->comps + compno;
	/* Compute the intersection of the area of interest, expressed in tile component coordinates */
	/* with the tile coordinates */

	auto dims = tilec->buf->bounds();
	uint32_t tcx0 = (uint32_t)dims.x0;
	uint32_t tcy0 = (uint32_t)dims.y0;
	uint32_t tcx1 = (uint32_t)dims.x1;
	uint32_t tcy1 = (uint32_t)dims.y1;

	uint32_t shift = tilec->numresolutions - tilec->resolutions_to_decompress;
	/* Tolerate small margin within the reduced resolution factor to consider if */
	/* the whole tile path must be taken */
	return (tcx0 >= (uint32_t) tilec->x0 && tcy0 >= (uint32_t) tilec->y0
			&& tcx1 <= (uint32_t) tilec->x1 && tcy1 <= (uint32_t) tilec->y1
			&& (shift >= 32
					|| (((tcx0 - (uint32_t) tilec->x0) >> shift) == 0
							&& ((tcy0 - (uint32_t) tilec->y0) >> shift) == 0
							&& (((uint32_t) tilec->x1 - tcx1) >> shift) == 0
							&& (((uint32_t) tilec->y1 - tcy1) >> shift) == 0)));

}

bool TileProcessor::decompress_tile_t2(ChunkBuffer *src_buf) {
	m_tcp = m_cp->tcps + m_tile_index;

	// optimization for regions that are close to largest decoded resolution
	// (currently breaks tests, so disabled)
	for (uint32_t compno = 0; compno < image->numcomps; compno++) {
		if (!is_whole_tilecomp_decoding(compno)) {
			whole_tile_decoding = false;
			break;
		}
	}

	if (!whole_tile_decoding) {
		/* Compute restricted tile-component and tile-resolution coordinates */
		/* of the window of interest */
		for (uint32_t compno = 0; compno < image->numcomps; compno++) {
			auto tilec = tile->comps + compno;

			/* Compute the intersection of the area of interest, expressed in tile coordinates */
			/* with the tile coordinates */
			auto dims = tilec->buf->bounds();
			uint32_t win_x0 = max<uint32_t>(tilec->x0, (uint32_t) dims.x0);
			uint32_t win_y0 = max<uint32_t>(tilec->y0, (uint32_t) dims.y0);
			uint32_t win_x1 = min<uint32_t>(tilec->x1, (uint32_t) dims.x1);
			uint32_t win_y1 = min<uint32_t>(tilec->y1, (uint32_t) dims.y1);
			if (win_x1 < win_x0 || win_y1 < win_y0) {
				/* We should not normally go there. The circumstance is when */
				/* the tile coordinates do not intersect the area of interest */
				/* Upper level logic should not even try to decompress that tile */
				GRK_ERROR("Invalid tilec->win_xxx values.");
				return false;
			}

			for (uint32_t resno = 0; resno < tilec->resolutions_to_decompress;
					++resno) {
				auto res = tilec->resolutions + resno;

				res->win_bounds =
						grk_rect_u32(
								ceildivpow2<uint32_t>(win_x0,
										tilec->resolutions_to_decompress - 1 - resno),
								ceildivpow2<uint32_t>(win_y0,
										tilec->resolutions_to_decompress - 1 - resno),
								ceildivpow2<uint32_t>(win_x1,
										tilec->resolutions_to_decompress - 1 - resno),
								ceildivpow2<uint32_t>(win_y1,
										tilec->resolutions_to_decompress - 1 - resno));
			}
		}
	}

	bool doT2 = !current_plugin_tile
			|| (current_plugin_tile->decode_flags & GRK_DECODE_T2);

	if (doT2) {
		uint64_t l_data_read = 0;

		if (!t2_decode(src_buf, &l_data_read))
			return false;
		// synch plugin with T2 data
		decode_synch_plugin_with_host(this);
	}

	return true;
}


bool TileProcessor::decompress_tile_t1(void) {
	bool doT1 = !current_plugin_tile
			|| (current_plugin_tile->decode_flags & GRK_DECODE_T1);
	bool doPostT1 = !current_plugin_tile
			|| (current_plugin_tile->decode_flags & GRK_DECODE_POST_T1);
	if (doT1) {
		for (uint32_t compno = 0; compno < tile->numcomps; ++compno) {
			auto tilec = tile->comps + compno;
			auto tccp = m_tcp->tccps + compno;

			if (!whole_tile_decoding) {
				try {
					tilec->alloc_sparse_array(m_resno_decoded_per_component[compno] + 1);
				} catch (runtime_error &ex) {
					GRK_ERROR("decompress_tile_t1: %s", ex.what());
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
				if (!Wavelet::decompress(this, tilec,
						m_resno_decoded_per_component[compno] + 1, tccp->qmfbid))
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


void TileProcessor::copy_image_to_tile() {
	for (uint32_t i = 0; i < image->numcomps; ++i) {
		auto tilec = tile->comps + i;
		auto img_comp = image->comps + i;

		uint32_t offset_x = ceildiv<uint32_t>(image->x0, img_comp->dx);
		uint32_t offset_y = ceildiv<uint32_t>(image->y0, img_comp->dy);
		uint64_t image_offset = (tilec->x0 - offset_x)
				+ (uint64_t) (tilec->y0 - offset_y) * img_comp->stride;
		auto src = img_comp->data + image_offset;
		auto dest = tilec->buf->ptr();

		for (uint32_t j = 0; j < tilec->height(); ++j) {
			memcpy(dest, src, tilec->width() * sizeof(int32_t));
			src += img_comp->stride;
			dest += tilec->buf->stride();
		}
	}
}

bool TileProcessor::t2_decode(ChunkBuffer *src_buf,
		uint64_t *p_data_read) {
	auto t2 = new T2Decode(this);
	bool rc = t2->decode_packets(m_tile_index, src_buf, p_data_read);
	delete t2;

	return rc;
}

bool TileProcessor::need_mct_decode(uint32_t compno){
	if (!m_tcp->mct)
		return false;
	if (tile->numcomps < 3){
		GRK_WARN("Number of components (%u) is inconsistent with a MCT. Skip the MCT step.",
				tile->numcomps);
		return false;
	}
	/* testcase 1336.pdf.asan.47.376 */
	uint64_t samples = tile->comps->buf->strided_area();
	if (tile->comps[1].buf->strided_area()	!= samples
			|| tile->comps[2].buf->strided_area()	!= samples) {
		GRK_WARN("Not all tiles components have the same dimension: skipping MCT.");
		return false;
	}
	if (compno > 2)
		return false;
	if (m_tcp->mct == 2 && !m_tcp->m_mct_decoding_matrix)
		return false;

	return true;
}

bool TileProcessor::mct_decode() {

	if (!need_mct_decode(0))
		return true;
	if (m_tcp->mct == 2) {
		auto data = new uint8_t*[tile->numcomps];
		for (uint32_t i = 0; i < tile->numcomps; ++i) {
			auto tile_comp = tile->comps + i;
			data[i] = (uint8_t*) tile_comp->buf->ptr();
		}
		uint64_t samples = tile->comps->buf->strided_area();
		bool rc = mct::decode_custom((uint8_t*) m_tcp->m_mct_decoding_matrix,
									samples,
									data,
									tile->numcomps,
									image->comps->sgnd);
		delete[] data;
		return rc;
	} else {
		if (m_tcp->tccps->qmfbid == 1) {
			mct::decode_rev(tile,image,m_tcp->tccps);
		} else {
			mct::decode_irrev(tile,	image,m_tcp->tccps);
		}
	}

	return true;
}

bool TileProcessor::dc_level_shift_decode() {
	for (uint32_t compno = 0; compno < tile->numcomps; compno++) {
		if (!need_mct_decode(compno) || m_tcp->mct == 2 ) {
			auto tccp = m_tcp->tccps + compno;
			if (tccp->qmfbid == 1)
				mct::decode_rev(tile,image,m_tcp->tccps,compno);
			else
				mct::decode_irrev(tile,image,m_tcp->tccps,compno);
		}
	}
	return true;
}

bool TileProcessor::dc_level_shift_encode() {
	for (uint32_t compno = 0; compno < tile->numcomps; compno++) {
		auto tile_comp = tile->comps + compno;
		auto tccp = m_tcp->tccps + compno;
		auto current_ptr = tile_comp->buf->ptr();
		uint64_t samples = tile_comp->buf->strided_area();
		if (tccp->m_dc_level_shift == 0)
			continue;
		for (uint64_t i = 0; i < samples; ++i) {
			*current_ptr -= tccp->m_dc_level_shift;
			++current_ptr;
		}
	}

	return true;
}


bool TileProcessor::mct_encode() {
	uint64_t samples = tile->comps->buf->strided_area();

	if (!m_tcp->mct)
		return true;
	if (m_tcp->mct == 2) {
		if (!m_tcp->m_mct_coding_matrix)
			return true;
		auto data = new uint8_t*[tile->numcomps];
		for (uint32_t i = 0; i < tile->numcomps; ++i) {
			auto tile_comp = tile->comps + i;
			data[i] = (uint8_t*) tile_comp->buf->ptr();
		}
		bool rc = mct::encode_custom((uint8_t*) m_tcp->m_mct_coding_matrix,
								samples,
								data,
								tile->numcomps,
								image->comps->sgnd);
		delete[] data;
		return rc;
	} else if (m_tcp->tccps->qmfbid == 0) {
		mct::encode_irrev(tile->comps[0].buf->ptr(),
				tile->comps[1].buf->ptr(),
				tile->comps[2].buf->ptr(), samples);
	} else {
		mct::encode_rev(tile->comps[0].buf->ptr(),
				tile->comps[1].buf->ptr(),
				tile->comps[2].buf->ptr(), samples);
	}

	return true;
}

bool TileProcessor::dwt_encode() {
	uint32_t compno = 0;
	bool rc = true;
	for (compno = 0; compno < (int64_t) tile->numcomps; ++compno) {
		auto tile_comp = tile->comps + compno;
		auto tccp = m_tcp->tccps + compno;
		if (!Wavelet::compress(tile_comp, tccp->qmfbid)) {
			rc = false;
			continue;

		}
	}
	return rc;
}

void TileProcessor::t1_encode() {
	const double *mct_norms;
	uint32_t mct_numcomps = 0U;
	auto tcp = m_tcp;

	if (tcp->mct == 1) {
		mct_numcomps = 3U;
		/* irreversible encoding */
		if (tcp->tccps->qmfbid == 0)
			mct_norms = mct::get_norms_irrev();
		else
			mct_norms = mct::get_norms_rev();
	} else {
		mct_numcomps = image->numcomps;
		mct_norms = (const double*) (tcp->mct_norms);
	}

	auto t1_wrap = std::unique_ptr<Tier1>(new Tier1());

	t1_wrap->encodeCodeblocks(tcp, tile, mct_norms, mct_numcomps,
			needs_rate_control());
}

bool TileProcessor::t2_encode(uint32_t *all_packet_bytes_written) {

	auto l_t2 = new T2Encode(this);
#ifdef DEBUG_LOSSLESS_T2
	for (uint32_t compno = 0; compno < p_image->m_numcomps; ++compno) {
		TileComponent *tilec = &p_tile->comps[compno];
		tilec->round_trip_resolutions = new grk_resolution[tilec->numresolutions];
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
				decodeBand->precincts = new grk_precinct[band->numPrecincts];
				decodeBand->precincts_data_size = (uint32_t)(band->numPrecincts * sizeof(grk_precinct));
				for (uint64_t precno = 0; precno < band->numPrecincts; ++precno) {
					auto prec = band->precincts + precno;
					auto decodePrec = decodeBand->precincts + precno;
					decodePrec->cw = prec->cw;
					decodePrec->ch = prec->ch;
					if (prec->enc && prec->cw && prec->ch) {
						decodePrec->initTagTrees();
						decodePrec->dec = new grk_cblk_dec[(uint64_t)decodePrec->cw * decodePrec->ch];
						for (uint64_t cblkno = 0; cblkno < decodePrec->cw * decodePrec->ch; ++cblkno) {
							auto cblk = prec->enc + cblkno;
							auto decodeCblk = decodePrec->dec + cblkno;
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

	if (!l_t2->encode_packets(m_tile_index, m_tcp->numlayers, m_stream,
			all_packet_bytes_written, m_poc_tile_part_index, tp_pos, pino)) {
		delete l_t2;
		return false;
	}

	delete l_t2;

#ifdef DEBUG_LOSSLESS_T2
	for (uint32_t compno = 0; compno < p_image->m_numcomps; ++compno) {
		TileComponent *tilec = &p_tile->comps[compno];
		for (uint32_t resno = 0; resno < tilec->numresolutions; ++resno) {
			auto roundRes = tilec->round_trip_resolutions + resno;
			for (uint32_t bandno = 0; bandno < roundRes->numbands; ++bandno) {
				auto decodeBand = roundRes->bands + bandno;
				if (decodeBand->precincts) {
					for (uint64_t precno = 0; precno < decodeBand->numPrecincts; ++precno) {
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

bool TileProcessor::rate_allocate() {
	if (m_cp->m_coding_params.m_enc.m_disto_alloc
			|| m_cp->m_coding_params.m_enc.m_fixed_quality) {
		uint32_t all_packets_len = 0;

		// rate control by rate/distortion or fixed quality
		switch (m_cp->m_coding_params.m_enc.rateControlAlgorithm) {
		case 0:
			if (!pcrd_bisect_simple(&all_packets_len))
				return false;
			break;
		case 1:
			if (!pcrd_bisect_feasible(&all_packets_len))
				return false;
			break;
		default:
			if (!pcrd_bisect_feasible(&all_packets_len))
				return false;
			break;
		}
	}

	return true;
}

/**
 * tile_data stores only the decoded resolutions, in the actual precision
 * of the decoded image. This method copies a sub-region of this region
 * into p_output_image (which stores data in 32 bit precision)
 *
 * @param p_output_image:
 *
 * @return:
 */
bool TileProcessor::copy_decompressed_tile_to_output_image(	grk_image *p_output_image) {
	auto image_src = image;
	for (uint32_t i = 0; i < image_src->numcomps; i++) {
		auto tilec = tile->comps + i;
		auto comp_src = image_src->comps + i;
		auto comp_dest = p_output_image->comps + i;

		/* Border of the current output component. (x0_dest,y0_dest)
		 * corresponds to origin of dest buffer */
		auto reduce = m_cp->m_coding_params.m_dec.m_reduce;
		uint32_t x0_dest = ceildivpow2<uint32_t>(comp_dest->x0, reduce);
		uint32_t y0_dest = ceildivpow2<uint32_t>(comp_dest->y0, reduce);
		/* can't overflow given that image->x1 is uint32 */
		uint32_t x1_dest = x0_dest + comp_dest->w;
		uint32_t y1_dest = y0_dest + comp_dest->h;

		grk_rect src_dim = tilec->buf->bounds();
		uint32_t width_src = (uint32_t) src_dim.width();
		uint32_t stride_src = tilec->buf->stride();
		uint32_t height_src = (uint32_t) src_dim.height();

		/* Compute the area (0, 0, off_x1_src, off_y1_src)
		 * of the input buffer (decoded tile component) which will be moved
		 * to the output buffer. Compute the area of the output buffer (off_x0_dest,
		 * off_y0_dest, width_dest, height_dest)  which will be modified
		 * by this input area.
		 * */
		uint32_t life_off_src = stride_src - width_src;
		uint32_t off_x0_dest = 0;
		uint32_t width_dest = 0;
		if (x0_dest < src_dim.x0) {
			off_x0_dest = (uint32_t) (src_dim.x0 - x0_dest);
			if (x1_dest >= src_dim.x1) {
				width_dest = width_src;
			} else {
				width_dest = (uint32_t) (x1_dest - src_dim.x0);
				life_off_src = stride_src - width_dest;
			}
		} else {
			off_x0_dest = 0U;
			if (x1_dest >= src_dim.x1) {
				width_dest = width_src;
			} else {
				width_dest = comp_dest->w;
				life_off_src = (uint32_t) (src_dim.x1 - x1_dest);
			}
		}

		uint32_t off_y0_dest = 0;
		uint32_t height_dest = 0;
		if (y0_dest < src_dim.y0) {
			off_y0_dest = (uint32_t) (src_dim.y0 - y0_dest);
			if (y1_dest >= src_dim.y1) {
				height_dest = height_src;
			} else {
				height_dest = (uint32_t) (y1_dest - src_dim.y0);
			}
		} else {
			off_y0_dest = 0;
			if (y1_dest >= src_dim.y1) {
				height_dest = height_src;
			} else {
				height_dest = comp_dest->h;
			}
		}
		if (width_dest > comp_dest->w || height_dest > comp_dest->h)
			return false;
		if (width_src > comp_src->w || height_src > comp_src->h)
			return false;

		size_t src_ind = 0;
		auto dest_ind = (size_t) off_x0_dest
				  	  + (size_t) off_y0_dest * comp_dest->stride;
		size_t line_off_dest =  (size_t) comp_dest->stride - (size_t) width_dest;
		auto src_ptr = tilec->buf->ptr();
		for (uint32_t j = 0; j < height_dest; ++j) {
			memcpy(comp_dest->data + dest_ind, src_ptr + src_ind,width_dest * sizeof(int32_t));
			dest_ind += width_dest + line_off_dest;
			src_ind  += width_dest + life_off_src;
		}
	}

	return true;
}

bool TileProcessor::pre_write_tile() {
	m_tile_part_index = 0;
	totnum_tp =	m_cp->tcps[m_tile_index].m_nb_tile_parts;
	m_poc_tile_part_index = 0;

	/* initialisation before tile encoding  */
	bool rc =  init_tile(nullptr, true);
	if (rc){
		uint32_t nb_tiles = (uint32_t) m_cp->t_grid_height
				* m_cp->t_grid_width;
		bool transfer_image_to_tile = (nb_tiles == 1);

		/* if we only have one tile, then simply set tile component data equal to
		 * image component data. Otherwise, allocate tile data and copy */
		for (uint32_t j = 0; j < image->numcomps; ++j) {
			auto tilec = tile->comps + j;
			auto imagec = image->comps + j;
			if (transfer_image_to_tile && imagec->data) {
				tilec->buf->attach(imagec->data, imagec->stride);
			} else {
				if (!tilec->buf->alloc()) {
					GRK_ERROR("Error allocating tile component data.");
					return false;
				}
			}
		}
		if (!transfer_image_to_tile)
			copy_image_to_tile();
	}

	return rc;
}

/**
 * Assume that source stride  == source width == destination width
 */
template<typename T> void grk_copy_strided(uint32_t w, uint32_t stride, uint32_t h, T *src, int32_t *dest){
	assert(stride >= w);
	uint32_t stride_diff = stride-w;
	size_t src_ind =0, dest_ind = 0;
	for (uint32_t j = 0; j < h; ++j) {
		for (uint32_t i = 0; i < w; ++i){
			dest[dest_ind++] = src[src_ind++];
		}
		dest_ind += stride_diff;
	}
}

bool TileProcessor::copy_uncompressed_data_to_tile(uint8_t *p_src,
		uint64_t src_length) {
	 uint64_t tile_size = 0;
	for (uint32_t i = 0; i < image->numcomps; ++i) {
		auto tilec = tile->comps + i;
		auto img_comp = image->comps + i;
		uint32_t size_comp = (img_comp->prec + 7) >> 3;
		tile_size += size_comp *tilec->area();
	}


	if (!p_src || (tile_size != src_length))
		return false;
	size_t length_per_component = src_length / image->numcomps;
	for (uint32_t i = 0; i < image->numcomps; ++i) {
		auto tilec = tile->comps + i;
		auto img_comp = image->comps + i;

		uint32_t size_comp = (img_comp->prec + 7) >> 3;
		auto dest_ptr = tilec->buf->ptr();
		uint32_t w = (uint32_t)tilec->buf->bounds().width();
		uint32_t h = (uint32_t)tilec->buf->bounds().height();
		uint32_t stride = tilec->buf->stride();
		switch (size_comp) {
		case 1:
			if (img_comp->sgnd) {
				auto src =  (int8_t*)p_src;
				grk_copy_strided<int8_t>(w, stride,h, src, dest_ptr);
				p_src = (uint8_t*)(src + length_per_component);
			}
			else {
				auto src =  (uint8_t*)p_src;
				grk_copy_strided<uint8_t>(w, stride,h, src, dest_ptr);
				p_src = (uint8_t*)(src + length_per_component);

			}
			break;
		case 2:
			if (img_comp->sgnd) {
				auto src =  (int16_t*)p_src;
				grk_copy_strided<int16_t>(w, stride,h, (int16_t*)p_src, dest_ptr);
				p_src = (uint8_t*)(src + length_per_component);
			}
			else {
				auto src =  (uint16_t*)p_src;
				grk_copy_strided<uint16_t>(w, stride,h, (uint16_t*)p_src, dest_ptr);
				p_src = (uint8_t*)(src + length_per_component);
			}
			break;
		}
	}
	return true;
}

bool TileProcessor::prepare_sod_decoding(CodeStream *codeStream) {
	assert(codeStream);

	// note: we subtract 2 to account for SOD marker
	auto tcp = codeStream->get_current_decode_tcp();
	if (codeStream->m_decoder.m_last_tile_part_in_code_stream) {
		tile_part_data_length =
				(uint32_t) (m_stream->get_number_byte_left() - 2);
	} else {
		if (tile_part_data_length >= 2)
			tile_part_data_length -= 2;
	}
	if (tile_part_data_length) {
		auto bytesLeftInStream = m_stream->get_number_byte_left();
		// check that there are enough bytes in stream to fill tile data
		if (tile_part_data_length > bytesLeftInStream) {
			GRK_WARN("Tile part length %lld greater than "
					"stream length %lld\n"
					"(tile: %u, tile part: %d). Tile may be truncated.",
					tile_part_data_length,
					m_stream->get_number_byte_left(),
					m_tile_index,
					tcp->m_tile_part_index);

			// sanitize tile_part_data_length
			tile_part_data_length =	(uint32_t) bytesLeftInStream;
		}
	}
	/* Index */
	grk_codestream_index *cstr_index = codeStream->cstr_index;
	if (cstr_index) {
		uint64_t current_pos = m_stream->tell();
		if (current_pos < 2) {
			GRK_ERROR("Stream too short");
			return false;
		}
		current_pos = (uint64_t) (current_pos - 2);

		uint32_t current_tile_part =
				cstr_index->tile_index[m_tile_index].current_tpsno;
		cstr_index->tile_index[m_tile_index].tp_index[current_tile_part].end_header =
				current_pos;
		cstr_index->tile_index[m_tile_index].tp_index[current_tile_part].end_pos =
				current_pos + tile_part_data_length + 2;

		if (!TileLengthMarkers::add_to_index(
				m_tile_index, cstr_index,
				J2K_MS_SOD, current_pos, 0)) {
			GRK_ERROR("Not enough memory to add tl marker");
			return false;
		}

		/*cstr_index->packno = 0;*/
	}
	size_t current_read_size = 0;
	if (tile_part_data_length) {
		if (!tcp->m_tile_data)
			tcp->m_tile_data = new ChunkBuffer();

		auto len = tile_part_data_length;
		uint8_t *buff = nullptr;
		auto zeroCopy = m_stream->supportsZeroCopy();
		if (!zeroCopy) {
			try {
				buff = new uint8_t[len];
			} catch (std::bad_alloc &ex) {
				GRK_ERROR("Not enough memory to allocate segment");
				return false;
			}
		} else {
			buff = m_stream->getCurrentPtr();
		}
		current_read_size = m_stream->read(zeroCopy ? nullptr : buff, len);
		tcp->m_tile_data->push_back(buff, len, !zeroCopy);

	}
	if (current_read_size != tile_part_data_length)
		codeStream->m_decoder.m_state = J2K_DEC_STATE_NO_EOC;
	else
		codeStream->m_decoder.m_state = J2K_DEC_STATE_TPH_SOT;

	return true;
}

PacketTracker::PacketTracker() : bits(nullptr),
		m_numcomps(0),
		m_numres(0),
		m_numprec(0),
		m_numlayers(0)

{}
PacketTracker::~PacketTracker(){
	delete[] bits;
}

void PacketTracker::init(uint32_t numcomps,
		uint32_t numres,
		uint64_t numprec,
		uint32_t numlayers){

	uint64_t len = get_buffer_len(numcomps,numres,numprec,numlayers);
	if (!bits)
  	   bits = new uint8_t[len];
	else {
		uint64_t currentLen =
				get_buffer_len(m_numcomps,m_numres,m_numprec,m_numlayers);
        if (len > currentLen) {
     	   delete[] bits;
     	   bits = new uint8_t[len];
        }
    }

   clear();
   m_numcomps = numcomps;
   m_numres = numres;
   m_numprec = numprec;
   m_numlayers = numlayers;
}

void PacketTracker::clear(void){
	uint64_t currentLen =
			get_buffer_len(m_numcomps,m_numres,m_numprec,m_numlayers);
	memset(bits, 0, currentLen);
}

uint64_t PacketTracker::get_buffer_len(uint32_t numcomps,
		uint32_t numres,
		uint64_t numprec,
		uint32_t numlayers){
	uint64_t len = numcomps*numres*numprec*numlayers;

	return ((len + 7)>>3) << 3;
}
void PacketTracker::packet_encoded(uint32_t comps,
		uint32_t res,
		uint64_t prec,
		uint32_t layer){

	if (comps >= m_numcomps ||
		prec >= m_numprec ||
		res >= m_numres ||
		layer >= m_numlayers){
			return;
	}

	uint64_t ind = index(comps,res,prec,layer);
	uint64_t ind_maj = ind >> 3;
	uint64_t ind_min = ind & 7;

	bits[ind_maj] = (uint8_t)(bits[ind_maj] | (1 << ind_min));

}
bool PacketTracker::is_packet_encoded(uint32_t comps,
		uint32_t res,
		uint64_t prec,
		uint32_t layer){

	if (comps >= m_numcomps ||
		prec >= m_numprec ||
		res >= m_numres ||
		layer >= m_numlayers){
			return true;
	}

	uint64_t ind = index(comps,res,prec,layer);
	uint64_t ind_maj = ind >> 3;
	uint64_t ind_min = ind & 7;

	return  ((bits[ind_maj] >> ind_min) & 1);
}

uint64_t PacketTracker::index(uint32_t comps,
		uint32_t res,
		uint64_t prec,
		uint32_t layer){

	return layer +
			prec * m_numlayers +
			res * m_numlayers * m_numprec +
			comps * m_numres * m_numprec * m_numlayers;
}

grk_seg::grk_seg() {
	clear();
}
void grk_seg::clear() {
	dataindex = 0;
	numpasses = 0;
	len = 0;
	maxpasses = 0;
	numPassesInPacket = 0;
	numBytesInPacket = 0;
}

grk_packet_length_info::grk_packet_length_info(uint32_t mylength, uint32_t bits) :
		len(mylength), len_bits(bits) {
}
grk_packet_length_info::grk_packet_length_info() :
		len(0), len_bits(0) {
}
bool grk_packet_length_info::operator==(const grk_packet_length_info &rhs) const {
	return (rhs.len == len && rhs.len_bits == len_bits);
}

grk_pass::grk_pass() :
		rate(0), distortiondec(0), len(0), term(0), slope(0) {
}
grk_layer::grk_layer() :
		numpasses(0), len(0), disto(0), data(nullptr) {
}

grk_precinct::grk_precinct() :
		cw(0), ch(0),
		enc(nullptr), dec(nullptr),
		num_code_blocks(0),
		incltree(nullptr), imsbtree(nullptr) {
}

grk_cblk::grk_cblk(): x0(0), y0(0), x1(0), y1(0),
		compressedData(nullptr),
		compressedDataSize(0),
		owns_data(false),
		numbps(0),
		numlenbits(0),
		numPassesInPacket(0)
#ifdef DEBUG_LOSSLESS_T2
		,included(false),
#endif
{
}

grk_cblk::grk_cblk(const grk_cblk &rhs): x0(rhs.x0), y0(rhs.y0), x1(rhs.x1), y1(rhs.y1),
		compressedData(rhs.compressedData), compressedDataSize(rhs.compressedDataSize), owns_data(rhs.owns_data),
		numbps(rhs.numbps), numlenbits(rhs.numlenbits), numPassesInPacket(rhs.numPassesInPacket)
#ifdef DEBUG_LOSSLESS_T2
	,included(0)
#endif
{
}
grk_cblk& grk_cblk::operator=(const grk_cblk& rhs){
	if (this != &rhs) { // self-assignment check expected
		x0 = rhs.x0;
		y0 = rhs.y0;
		x1 = rhs.x1;
		y1 = rhs.y1;
		compressedData = rhs.compressedData;
		compressedDataSize = rhs.compressedDataSize;
		owns_data = rhs.owns_data;
		numbps = rhs.numbps;
		numlenbits = rhs.numlenbits;
		numPassesInPacket = rhs.numPassesInPacket;
#ifdef DEBUG_LOSSLESS_T2
		included = rhs.included;
		packet_length_info = rhs.packet_length_info;
#endif
	}
	return *this;
}
void grk_cblk::clear(){
	compressedData = nullptr;
	owns_data = false;
}

uint32_t grk_cblk::width(){
	return (uint32_t)(x1-x0);
}
uint32_t grk_cblk::height(){
	return (uint32_t)(y1-y0);
}

uint32_t grk_cblk::area(){
	return width() * height();
}

grk_cblk_enc::grk_cblk_enc() :
				paddedCompressedData(nullptr),
				layers(	nullptr),
				passes(nullptr),
				numPassesInPreviousPackets(0),
				numPassesTotal(0),
				contextStream(nullptr)
{
}
grk_cblk_enc::grk_cblk_enc(const grk_cblk_enc &rhs) :
						grk_cblk(rhs),
						paddedCompressedData(rhs.paddedCompressedData),
						layers(	rhs.layers),
						passes(rhs.passes),
						numPassesInPreviousPackets(rhs.numPassesInPreviousPackets),
						numPassesTotal(rhs.numPassesTotal),
						contextStream(rhs.contextStream)
{}

grk_cblk_enc& grk_cblk_enc::operator=(const grk_cblk_enc& rhs){
	if (this != &rhs) { // self-assignment check expected
		grk_cblk::operator = (rhs);
		paddedCompressedData = rhs.paddedCompressedData;
		layers = rhs.layers;
		passes = rhs.passes;
		numPassesInPreviousPackets = rhs.numPassesInPreviousPackets;
		numPassesTotal = rhs.numPassesTotal;
		contextStream = rhs.contextStream;
#ifdef DEBUG_LOSSLESS_T2
		packet_length_info = rhs.packet_length_info;
#endif

	}
	return *this;
}

grk_cblk_enc::~grk_cblk_enc() {
	cleanup();
}
void grk_cblk_enc::clear(){
	grk_cblk::clear();
	layers = nullptr;
	passes = nullptr;
	contextStream = nullptr;
#ifdef DEBUG_LOSSLESS_T2
	packet_length_info.clear();
#endif
}
bool grk_cblk_enc::alloc() {
	if (!layers) {
		/* no memset since data */
		layers = (grk_layer*) grk_calloc(100, sizeof(grk_layer));
		if (!layers)
			return false;
	}
	if (!passes) {
		passes = (grk_pass*) grk_calloc(100, sizeof(grk_pass));
		if (!passes)
			return false;
	}

	return true;
}

/**
 * Allocates data memory for an encoding code block.
 * We actually allocate 2 more bytes than specified, and then offset data by +2.
 * This is done so that we can safely initialize the MQ coder pointer to data-1,
 * without risk of accessing uninitialized memory.
 */
bool grk_cblk_enc::alloc_data(size_t nominalBlockSize) {
	uint32_t desired_data_size = (uint32_t) (nominalBlockSize * sizeof(uint32_t));
	if (desired_data_size > compressedDataSize) {
		if (owns_data)
			delete[] compressedData;

		// we add two fake zero bytes at beginning of buffer, so that mq coder
		//can be initialized to data[-1] == actualData[1], and still point
		//to a valid memory location
		compressedData = new uint8_t[desired_data_size + grk_cblk_enc_compressed_data_pad_left];
		compressedData[0] = 0;
		compressedData[1] = 0;

		paddedCompressedData = compressedData + grk_cblk_enc_compressed_data_pad_left;
		compressedDataSize = desired_data_size;
		owns_data = true;
	}
	return true;
}

void grk_cblk_enc::cleanup() {
	if (owns_data) {
		delete[] compressedData;
		compressedData = nullptr;
		owns_data = false;
	}
	paddedCompressedData = nullptr;
	grk_free(layers);
	layers = nullptr;
	grk_free(passes);
	passes = nullptr;
}

grk_cblk_dec::grk_cblk_dec() {
	init();
}

grk_cblk_dec::~grk_cblk_dec(){
    cleanup();
}

void grk_cblk_dec::clear(){
	grk_cblk::clear();
	segs = nullptr;
	cleanup_seg_buffers();
}

grk_cblk_dec::grk_cblk_dec(const grk_cblk_dec &rhs) :
				grk_cblk(rhs),
				segs(rhs.segs), numSegments(rhs.numSegments),
				numSegmentsAllocated(rhs.numSegmentsAllocated)
{}

grk_cblk_dec& grk_cblk_dec::operator=(const grk_cblk_dec& rhs){
	if (this != &rhs) { // self-assignment check expected
		*this = grk_cblk_dec(rhs);
	}
	return *this;
}

bool grk_cblk_dec::alloc() {
	if (!segs) {
		segs = new grk_seg[default_numbers_segments];
		/*fprintf(stderr, "Allocate %u elements of code_block->data\n", default_numbers_segments * sizeof(grk_seg));*/

		numSegmentsAllocated = default_numbers_segments;

		/*fprintf(stderr, "Allocate 8192 elements of code_block->data\n");*/
		/*fprintf(stderr, "numSegmentsAllocated of code_block->data = %u\n", p_code_block->numSegmentsAllocated);*/

	} else {
		/* sanitize */
		auto l_segs = segs;
		uint32_t l_current_max_segs = numSegmentsAllocated;

		/* Note: since seg_buffers simply holds references to another data buffer,
		 we do not need to copy it to the sanitized block  */
		cleanup_seg_buffers();
		init();
		segs = l_segs;
		numSegmentsAllocated = l_current_max_segs;
	}
	return true;
}

void grk_cblk_dec::init() {
	compressedData = nullptr;
	compressedDataSize = 0;
	owns_data = false;
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

void grk_cblk_dec::cleanup() {
	if (owns_data) {
		delete[] compressedData;
		compressedData = nullptr;
		owns_data = false;
	}
	cleanup_seg_buffers();
	delete[] segs;
	segs = nullptr;
}

void grk_cblk_dec::cleanup_seg_buffers(){

	for (auto& b : seg_buffers)
		delete b;
	seg_buffers.clear();

}

size_t grk_cblk_dec::getSegBuffersLen(){
	return std::accumulate(seg_buffers.begin(), seg_buffers.end(), 0, [](const size_t s, grk_buf *a){
	   return s + a->len;
	});
}

bool grk_cblk_dec::copy_to_contiguous_buffer(uint8_t *buffer) {
	if (!buffer)
		return false;
	size_t offset = 0;
	for (auto& buf : seg_buffers){
		if (buf->len){
			memcpy(buffer + offset, buf->buf, buf->len);
			offset += buf->len;
		}
	}
	return true;
}

grk_band::grk_band() :
				bandno(0),
				precincts(nullptr),
				numPrecincts(0),
				numAllocatedPrecincts(0),
				numbps(0),
				stepsize(0),
				inv_step(0) {
}

//note: don't copy precinct array
grk_band::grk_band(const grk_band &rhs) : grk_rect_u32(rhs),
										bandno(rhs.bandno),
										precincts(nullptr),
										numPrecincts(0),
										numAllocatedPrecincts(0),
										numbps(rhs.numbps),
										stepsize(rhs.stepsize),
										inv_step(rhs.inv_step)
{
}

grk_band& grk_band::operator= (const grk_band &rhs){
	if (this != &rhs) { // self-assignment check expected
		*this = grk_band(rhs);
	}
	return *this;
}


bool grk_band::isEmpty() {
	return ((x1 - x0 == 0) || (y1 - y0 == 0));
}

void grk_precinct::deleteTagTrees() {
	delete incltree;
	incltree = nullptr;
	delete imsbtree;
	imsbtree = nullptr;
}

void grk_precinct::initTagTrees() {

	// if cw == 0 or ch == 0,
	// then the precinct has no code blocks, therefore
	// no need for inclusion and msb tag trees
	if (cw > 0 && ch > 0) {
		if (!incltree) {
			try {
				incltree = new TagTree(cw, ch);
			} catch (std::exception &e) {
				GRK_WARN("No incltree created.");
			}
		} else {
			if (!incltree->init(cw, ch)) {
				GRK_WARN("Failed to re-initialize incltree.");
				delete incltree;
				incltree = nullptr;
			}
		}

		if (!imsbtree) {
			try {
				imsbtree = new TagTree(cw, ch);
			} catch (std::exception &e) {
				GRK_WARN("No imsbtree created.");
			}
		} else {
			if (!imsbtree->init(cw, ch)) {
				GRK_WARN("Failed to re-initialize imsbtree.");
				delete imsbtree;
				imsbtree = nullptr;
			}
		}
	}
}

grk_resolution::grk_resolution() :
		pw(0),
		ph(0),
		numbands(0)
{}


}

