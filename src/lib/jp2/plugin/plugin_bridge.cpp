/**
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

// Performed after T2, just before plugin decompress is triggered
// note: only support single segment at the moment
void decode_synch_plugin_with_host(TileProcessor *tcd) {
	if (tcd->current_plugin_tile && tcd->current_plugin_tile->tileComponents) {
		auto tile = tcd->tile;
		for (uint32_t compno = 0; compno < tile->numcomps; compno++) {
			auto tilec = &tile->comps[compno];
			auto plugin_tilec = tcd->current_plugin_tile->tileComponents[compno];
			assert(tilec->numresolutions == plugin_tilec->numResolutions);
			for (uint32_t resno = 0; resno < tilec->numresolutions; resno++) {
				auto res = &tilec->resolutions[resno];
				auto plugin_res = plugin_tilec->resolutions[resno];
				assert(plugin_res->numBands == res->numbands);
				for (uint32_t bandno = 0; bandno < res->numbands; bandno++) {
					auto band = &res->bands[bandno];
					auto plugin_band = plugin_res->bands[bandno];
					assert(plugin_band->numPrecincts == (uint64_t)res->pw * res->ph);
					//!!!! plugin still uses stepsize/2
					plugin_band->stepsize = band->stepsize/2;
					for (uint64_t precno = 0; precno < (uint64_t)res->pw * res->ph;
							precno++) {
						auto prc = &band->precincts[precno];
						auto plugin_prc = plugin_band->precincts[precno];
						assert(plugin_prc->numBlocks == (uint64_t)prc->cw * prc->ch);
						for (uint64_t cblkno = 0; cblkno < (uint64_t)prc->cw * prc->ch;
								cblkno++) {
							auto cblk = &prc->dec[cblkno];
							if (!cblk->numSegments)
								continue;
							// sanity check
							if (cblk->numSegments != 1) {
								GRK_INFO(
										"Plugin does not handle code blocks with multiple segments. Image will be decoded on CPU.");
								throw PluginDecodeUnsupportedException();
							}
							auto maxPasses = 3
									* (tcd->image->comps[0].prec
											+ BIBO_EXTRA_BITS) - 2;
							if (cblk->segs[0].numpasses > maxPasses) {
								GRK_INFO(
										"Number of passes %u in segment exceeds BIBO maximum %u. Image will be decoded on CPU.",
										cblk->segs[0].numpasses, maxPasses);
								throw PluginDecodeUnsupportedException();
							}

							auto plugin_cblk =
									plugin_prc->blocks[cblkno];

							// copy segments into plugin codeblock buffer, and point host code block data
							// to plugin data buffer
							plugin_cblk->compressedDataLength =
									(uint32_t)cblk->getSegBuffersLen();
							cblk->copy_to_contiguous_buffer(
									plugin_cblk->compressedData);
							cblk->compressedData = plugin_cblk->compressedData;
							cblk->compressedDataSize = plugin_cblk->compressedDataLength;
							cblk->owns_data = false;
							plugin_cblk->numBitPlanes = cblk->numbps;
							plugin_cblk->numPasses = cblk->segs[0].numpasses;
						}
					}
				}
			}
		}
	}
}

bool tile_equals(grk_plugin_tile *plugin_tile, grk_tile *p_tile) {
	uint32_t state = grk_plugin_get_debug_state();
	if (!(state & GRK_PLUGIN_STATE_DEBUG))
		return true;
	if ((!plugin_tile && p_tile) || (plugin_tile && !p_tile))
		return false;
	if (!plugin_tile && !p_tile)
		return true;
	if (plugin_tile->numComponents != p_tile->numcomps)
		return false;
	for (uint32_t compno = 0; compno < p_tile->numcomps; ++compno) {
		auto tilecomp = p_tile->comps + compno;
		auto plugin_tilecomp =
				plugin_tile->tileComponents[compno];
		if (tilecomp->numresolutions != plugin_tilecomp->numResolutions)
			return false;
		for (uint32_t resno = 0; resno < tilecomp->numresolutions; ++resno) {
			auto resolution = tilecomp->resolutions + resno;
			auto plugin_resolution =
					plugin_tilecomp->resolutions[resno];
			if (resolution->numbands != plugin_resolution->numBands)
				return false;
			for (uint32_t bandno = 0; bandno < resolution->numbands; ++bandno) {
				auto band = resolution->bands + bandno;
				auto plugin_band =
						plugin_resolution->bands[bandno];
				size_t num_precincts = band->numPrecincts;
				if (num_precincts != plugin_band->numPrecincts)
					return false;
				for (uint64_t precno = 0; precno < num_precincts; ++precno) {
					auto precinct = band->precincts + precno;
					auto plugin_precinct =
							plugin_band->precincts[precno];
					if ((uint64_t)precinct->ch * precinct->cw
							!= plugin_precinct->numBlocks) {
						return false;
					}
					for (uint64_t cblkno = 0;
							cblkno < (uint64_t)precinct->ch * precinct->cw; ++cblkno) {
						auto cblk = precinct->dec + cblkno;
						auto plugin_cblk =
								plugin_precinct->blocks[cblkno];
						if (cblk->x0 != plugin_cblk->x0
								|| cblk->x1 != plugin_cblk->x1
								|| cblk->y0 != plugin_cblk->y0
								|| cblk->y1 != plugin_cblk->y1)
							return false;
					}
				}
			}
		}
	}
	return true;
}

void encode_synch_with_plugin(TileProcessor *tcd, uint32_t compno, uint32_t resno,
		uint32_t bandno, uint64_t precno, uint64_t cblkno, grk_band *band,
		grk_cblk_enc *cblk, uint32_t *numPix) {

	if (tcd->current_plugin_tile && tcd->current_plugin_tile->tileComponents) {
		auto plugin_band =
				tcd->current_plugin_tile->tileComponents[compno]->resolutions[resno]->bands[bandno];
		auto precinct = plugin_band->precincts[precno];
		auto plugin_cblk = precinct->blocks[cblkno];
		uint32_t state = grk_plugin_get_debug_state();

		if (state & GRK_PLUGIN_STATE_DEBUG) {
			if (band->stepsize != plugin_band->stepsize) {
				GRK_WARN("ojp band step size {} differs from plugin step size {}",
						band->stepsize, plugin_band->stepsize);
			}
			if (cblk->numPassesTotal != plugin_cblk->numPasses)
				GRK_WARN("OPJ total number of passes ({}) differs from "
						"plugin total number of passes ({}) : component={}, res={}, band={}, block={}",
						cblk->numPassesTotal,
						(uint32_t) plugin_cblk->numPasses, compno, resno,
						bandno, cblkno);
		}

		cblk->numPassesTotal = (uint32_t) plugin_cblk->numPasses;
		*numPix = (uint32_t) plugin_cblk->numPix;

		if (state & GRK_PLUGIN_STATE_DEBUG) {
			uint32_t grkNumPix = cblk->area();
			if (plugin_cblk->numPix != grkNumPix)
				printf(
						"[WARNING]  ojp numPix %u differs from plugin numPix %u\n",
						grkNumPix, plugin_cblk->numPix);
		}

		bool goodData = true;
		uint16_t totalRatePlugin = (uint16_t) plugin_cblk->compressedDataLength;

		//check data
		if (state & GRK_PLUGIN_STATE_DEBUG) {
			uint32_t totalRate = 0;
			if (cblk->numPassesTotal > 0) {
				totalRate = (cblk->passes + cblk->numPassesTotal - 1)->rate;
				if (totalRatePlugin != totalRate) {
					GRK_WARN("CPU rate {} differs from plugin rate {}",
							totalRate, totalRatePlugin);
				}
			}

			for (uint32_t p = 0; p < totalRate; ++p) {
				if (cblk->paddedCompressedData[p] != plugin_cblk->compressedData[p]) {
					GRK_WARN("data differs at position={}, component={}, res={}, band={}, block={}, CPU rate ={}, plugin rate={}",
							p, compno, resno, bandno, cblkno, totalRate,
							totalRatePlugin);
					goodData = false;
					break;
				}
			}
		}

		if (goodData)
			cblk->paddedCompressedData = plugin_cblk->compressedData;
		cblk->compressedDataSize = (uint32_t) (plugin_cblk->compressedDataLength);
		cblk->owns_data = false;
		cblk->numbps = (uint32_t) plugin_cblk->numBitPlanes;
		if (state & GRK_PLUGIN_STATE_DEBUG) {
			if (cblk->x0 != plugin_cblk->x0 || cblk->y0 != plugin_cblk->y0
					|| cblk->x1 != plugin_cblk->x1
					|| cblk->y1 != plugin_cblk->y1) {
			    GRK_ERROR("plugin code block bounding box differs from OPJ code block");
			}
		}

		uint32_t lastRate = 0;
		for (uint32_t passno = 0; passno < cblk->numPassesTotal; passno++) {
			auto pass = cblk->passes + passno;
			auto pluginPass = plugin_cblk->passes + passno;

			// synch distortion, if applicable
			if (tcd->needs_rate_control()) {
				if (state & GRK_PLUGIN_STATE_DEBUG) {
					if (fabs(
							pass->distortiondec
									- pluginPass->distortionDecrease)
							/ fabs(pass->distortiondec) > 0.01) {
						GRK_WARN("distortion decrease for pass {} differs between plugin and OPJ:  plugin: {}, OPJ : {}",
								passno, pluginPass->distortionDecrease,
								pass->distortiondec);
					}
				}
				pass->distortiondec = pluginPass->distortionDecrease;
			}
			uint16_t pluginRate = (uint16_t) (pluginPass->rate + 1);
			if (pluginRate > totalRatePlugin)
				pluginRate = totalRatePlugin;

			//Preventing generation of FF as last data byte of a pass
			if ((pluginRate > 1)
					&& (plugin_cblk->compressedData[pluginRate - 1] == 0xFF)) {
				pluginRate--;
			}

			if (state & GRK_PLUGIN_STATE_DEBUG) {
				if (pluginRate != pass->rate) {
					GRK_WARN("plugin rate {} differs from OPJ rate {}\n",
							pluginRate, pass->rate);
				}
			}

			pass->rate = pluginRate;
			pass->len = (uint16_t) (pass->rate - lastRate);
			lastRate = pass->rate;
		}
	}
}

// set context stream for debugging purposes
void set_context_stream(TileProcessor *p_tileProcessor) {
	for (uint32_t compno = 0; compno < p_tileProcessor->tile->numcomps; compno++) {
		auto tilec = p_tileProcessor->tile->comps + compno;
		tilec->numpix = 0;
		for (uint32_t resno = 0; resno < tilec->numresolutions; resno++) {
			auto res = &tilec->resolutions[resno];
			for (uint32_t bandno = 0; bandno < res->numbands; bandno++) {
				auto band = &res->bands[bandno];
				for (uint64_t precno = 0; precno < (uint64_t)res->pw * res->ph;
						precno++) {
					auto prc = &band->precincts[precno];
					for (uint64_t cblkno = 0; cblkno < (uint64_t)prc->cw * prc->ch;
							cblkno++) {
						auto cblk = &prc->enc[cblkno];
						if (p_tileProcessor->current_plugin_tile
								&& p_tileProcessor->current_plugin_tile->tileComponents) {
							auto comp =
									p_tileProcessor->current_plugin_tile->tileComponents[compno];
							if (resno < comp->numResolutions) {
								auto plugin_band =
										comp->resolutions[resno]->bands[bandno];
								auto precinct =
										plugin_band->precincts[precno];
								auto plugin_cblk =
										precinct->blocks[cblkno];
								cblk->contextStream =
										plugin_cblk->contextStream;
							}
						}
					}
				}
			}
		}
	}
}

static const char *plugin_debug_mqc_next_cxd_method_name =
		"plugin_debug_mqc_next_cxd";
static const char *plugin_debug_mqc_next_plane_method_name =
		"plugin_debug_mqc_next_plane";

// Debug: these methods wrap plugin methods for parsing a context stream
void mqc_next_plane(grk_plugin_debug_mqc *mqc) {
	auto mgr = minpf_get_plugin_manager();
	if (mgr && mgr->num_libraries > 0) {
		auto func = (PLUGIN_DEBUG_MQC_NEXT_PLANE) minpf_get_symbol(
				mgr->dynamic_libraries[0],
				plugin_debug_mqc_next_plane_method_name);
		if (func)
			func(mqc);
	}
}
void nextCXD(grk_plugin_debug_mqc *mqc, uint32_t d) {
	auto mgr = minpf_get_plugin_manager();
	if (mgr && mgr->num_libraries > 0) {
		auto func = (PLUGIN_DEBUG_MQC_NEXT_CXD) minpf_get_symbol(
				mgr->dynamic_libraries[0],
				plugin_debug_mqc_next_cxd_method_name);
		if (func)
			func(mqc, d);
	}
}

}
