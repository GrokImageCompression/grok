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

#include "grok_includes.h"
#include "plugin_bridge.h"

namespace grk {

// Performed after T2, just before plugin decode is triggered
// note: only support single segment at the moment
void decode_synch_plugin_with_host(TileProcessor *tcd) {
	if (tcd->current_plugin_tile && tcd->current_plugin_tile->tileComponents) {
		auto tcd_tile = tcd->tile;
		for (uint32_t compno = 0; compno < tcd_tile->numcomps; compno++) {
			auto tilec = &tcd_tile->comps[compno];
			auto plugin_tilec = tcd->current_plugin_tile->tileComponents[compno];
			assert(tilec->numresolutions == plugin_tilec->numResolutions);
			for (uint32_t resno = 0; resno < tilec->numresolutions; resno++) {
				auto res = &tilec->resolutions[resno];
				auto plugin_res = plugin_tilec->resolutions[resno];
				assert(plugin_res->numBands == res->numbands);
				for (uint32_t bandno = 0; bandno < res->numbands; bandno++) {
					auto band = &res->bands[bandno];
					auto plugin_band = plugin_res->bands[bandno];
					assert(plugin_band->numPrecincts == res->pw * res->ph);
					plugin_band->stepsize = band->stepsize;
					for (uint32_t precno = 0; precno < res->pw * res->ph;
							precno++) {
						auto prc = &band->precincts[precno];
						auto plugin_prc = plugin_band->precincts[precno];
						assert(plugin_prc->numBlocks == prc->cw * prc->ch);
						for (uint32_t cblkno = 0; cblkno < prc->cw * prc->ch;
								cblkno++) {
							auto cblk = &prc->cblks.dec[cblkno];
							if (!cblk->numSegments)
								continue;
							// sanity check
							if (cblk->numSegments != 1) {
								GROK_INFO(
										"Plugin does not handle code blocks with multiple segments. Image will be decoded on CPU.");
								throw PluginDecodeUnsupportedException();
							}
							auto maxPasses = 3
									* (tcd->image->comps[0].prec
											+ BIBO_EXTRA_BITS) - 2;
							if (cblk->segs[0].numpasses > maxPasses) {
								GROK_INFO(
										"Number of passes %d in segment exceeds BIBO maximum %d. Image will be decoded on CPU.\n",
										cblk->segs[0].numpasses, maxPasses);
								throw PluginDecodeUnsupportedException();
							}

							grk_plugin_code_block *plugin_cblk =
									plugin_prc->blocks[cblkno];

							// copy segments into plugin codeblock buffer, and point host code block data
							// to plugin data buffer
							plugin_cblk->compressedDataLength =
									cblk->seg_buffers.get_len();
							cblk->seg_buffers.copy_to_contiguous_buffer(
									plugin_cblk->compressedData);
							cblk->data = plugin_cblk->compressedData;
							cblk->dataSize =
									(uint32_t) plugin_cblk->compressedDataLength;

							plugin_cblk->numBitPlanes = cblk->numbps;
							plugin_cblk->numPasses = cblk->segs[0].numpasses;
						}
					}
				}
			}
		}
	}
}

bool tile_equals(grk_plugin_tile *plugin_tile, grk_tcd_tile *p_tile) {
	uint32_t state = grok_plugin_get_debug_state();
	if (!(state & GROK_PLUGIN_STATE_DEBUG))
		return true;
	if ((!plugin_tile && p_tile) || (plugin_tile && !p_tile))
		return false;
	if (!plugin_tile && !p_tile)
		return true;
	if (plugin_tile->numComponents != p_tile->numcomps)
		return false;
	for (uint32_t compno = 0; compno < p_tile->numcomps; ++compno) {
		grk_tcd_tilecomp *tilecomp = p_tile->comps + compno;
		grk_plugin_tile_component *plugin_tilecomp =
				plugin_tile->tileComponents[compno];
		if (tilecomp->numresolutions != plugin_tilecomp->numResolutions)
			return false;
		for (uint32_t resno = 0; resno < tilecomp->numresolutions; ++resno) {
			grk_tcd_resolution *resolution = tilecomp->resolutions + resno;
			grk_plugin_resolution *plugin_resolution =
					plugin_tilecomp->resolutions[resno];
			if (resolution->numbands != plugin_resolution->numBands)
				return false;
			for (uint32_t bandno = 0; bandno < resolution->numbands; ++bandno) {
				grk_tcd_band *band = resolution->bands + bandno;
				grk_plugin_band *plugin_band =
						plugin_resolution->bands[bandno];
				size_t num_precincts = band->numPrecincts;
				if (num_precincts != plugin_band->numPrecincts)
					return false;
				for (size_t precno = 0; precno < num_precincts; ++precno) {
					grk_tcd_precinct *precinct = band->precincts + precno;
					grk_plugin_precinct *plugin_precinct =
							plugin_band->precincts[precno];
					if (precinct->ch * precinct->cw
							!= plugin_precinct->numBlocks) {
						return false;
					}
					for (uint32_t cblkno = 0;
							cblkno < precinct->ch * precinct->cw; ++cblkno) {
						grk_tcd_cblk_dec *cblk = precinct->cblks.dec + cblkno;
						grk_plugin_code_block *plugin_cblk =
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
		uint32_t bandno, uint32_t precno, uint32_t cblkno, grk_tcd_band *band,
		grk_tcd_cblk_enc *cblk, uint32_t *numPix) {

	if (tcd->current_plugin_tile && tcd->current_plugin_tile->tileComponents) {
		grk_plugin_band *plugin_band =
				tcd->current_plugin_tile->tileComponents[compno]->resolutions[resno]->bands[bandno];
		grk_plugin_precinct *precinct = plugin_band->precincts[precno];
		grk_plugin_code_block *plugin_cblk = precinct->blocks[cblkno];
		uint32_t state = grok_plugin_get_debug_state();

		if (state & GROK_PLUGIN_STATE_DEBUG) {
			if (band->stepsize != plugin_band->stepsize) {
				GROK_WARN("ojp band step size {} differs from plugin step size {}",
						band->stepsize, plugin_band->stepsize);
			}
			if (cblk->num_passes_encoded != plugin_cblk->numPasses)
				GROK_WARN("OPJ total number of passes ({}) differs from "
						"plugin total number of passes ({}) : component={}, res={}, band={}, block={}",
						cblk->num_passes_encoded,
						(uint32_t) plugin_cblk->numPasses, compno, resno,
						bandno, cblkno);
		}

		cblk->num_passes_encoded = (uint32_t) plugin_cblk->numPasses;
		*numPix = (uint32_t) plugin_cblk->numPix;

		if (state & GROK_PLUGIN_STATE_DEBUG) {
			uint32_t opjNumPix = ((cblk->x1 - cblk->x0) * (cblk->y1 - cblk->y0));
			if (plugin_cblk->numPix != opjNumPix)
				printf(
						"[WARNING]  ojp numPix %d differs from plugin numPix %d\n",
						opjNumPix, (uint32_t) plugin_cblk->numPix);
		}

		bool goodData = true;
		uint16_t totalRatePlugin = (uint16_t) plugin_cblk->compressedDataLength;

		//check data
		if (state & GROK_PLUGIN_STATE_DEBUG) {
			uint32_t totalRate = 0;
			if (cblk->num_passes_encoded > 0) {
				totalRate = (cblk->passes + cblk->num_passes_encoded - 1)->rate;
				if (totalRatePlugin != totalRate) {
					GROK_WARN("opj rate {} differs from plugin rate {}",
							totalRate, totalRatePlugin);
				}
			}

			for (uint32_t p = 0; p < totalRate; ++p) {
				if (cblk->data[p] != plugin_cblk->compressedData[p]) {
					GROK_WARN("data differs at position={}, component={}, res={}, band={}, block={}, opj rate ={}, plugin rate={}",
							p, compno, resno, bandno, cblkno, totalRate,
							totalRatePlugin);
					goodData = false;
					break;
				}
			}
		}

		if (goodData)
			cblk->data = plugin_cblk->compressedData;
		cblk->data_size = (uint32_t) (plugin_cblk->compressedDataLength);
		cblk->owns_data = false;
		cblk->numbps = (uint32_t) plugin_cblk->numBitPlanes;
		if (state & GROK_PLUGIN_STATE_DEBUG) {
			if (cblk->x0 != plugin_cblk->x0 || cblk->y0 != plugin_cblk->y0
					|| cblk->x1 != plugin_cblk->x1
					|| cblk->y1 != plugin_cblk->y1) {
			    GROK_ERROR("plugin code block bounding box differs from OPJ code block");
			}
		}

		uint16_t lastRate = 0;
		for (uint32_t passno = 0; passno < cblk->num_passes_encoded; passno++) {
			grk_tcd_pass *pass = cblk->passes + passno;
			grk_plugin_pass *pluginPass = plugin_cblk->passes + passno;

			// synch distortion, if applicable
			if (tcd->needs_rate_control()) {
				if (state & GROK_PLUGIN_STATE_DEBUG) {
					if (fabs(
							pass->distortiondec
									- pluginPass->distortionDecrease)
							/ fabs(pass->distortiondec) > 0.01) {
						GROK_WARN("distortion decrease for pass {} differs between plugin and OPJ:  plugin: {}, OPJ : {}",
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

			if (state & GROK_PLUGIN_STATE_DEBUG) {
				if (pluginRate != pass->rate) {
					GROK_WARN("plugin rate {} differs from OPJ rate {}\n",
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
		grk_tcd_tilecomp *tilec = p_tileProcessor->tile->comps + compno;
		tilec->numpix = 0;

		for (uint32_t resno = 0; resno < tilec->numresolutions; resno++) {
			grk_tcd_resolution *res = &tilec->resolutions[resno];

			for (uint32_t bandno = 0; bandno < res->numbands; bandno++) {
				grk_tcd_band *band = &res->bands[bandno];

				for (uint32_t precno = 0; precno < res->pw * res->ph;
						precno++) {
					grk_tcd_precinct *prc = &band->precincts[precno];

					for (uint32_t cblkno = 0; cblkno < prc->cw * prc->ch;
							cblkno++) {
						grk_tcd_cblk_enc *cblk = &prc->cblks.enc[cblkno];

						if (p_tileProcessor->current_plugin_tile
								&& p_tileProcessor->current_plugin_tile->tileComponents) {
							grk_plugin_tile_component *comp =
									p_tileProcessor->current_plugin_tile->tileComponents[compno];
							if (resno < comp->numResolutions) {
								grk_plugin_band *plugin_band =
										comp->resolutions[resno]->bands[bandno];
								grk_plugin_precinct *precinct =
										plugin_band->precincts[precno];
								grk_plugin_code_block *plugin_cblk =
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
	minpf_plugin_manager *mgr = nullptr;
	PLUGIN_DEBUG_MQC_NEXT_PLANE func = nullptr;
	mgr = minpf_get_plugin_manager();
	if (mgr && mgr->num_libraries > 0) {
		func = (PLUGIN_DEBUG_MQC_NEXT_PLANE) minpf_get_symbol(
				mgr->dynamic_libraries[0],
				plugin_debug_mqc_next_plane_method_name);
		if (func) {
			func(mqc);
		}
	}
}

void nextCXD(grk_plugin_debug_mqc *mqc, uint32_t d) {
	minpf_plugin_manager *mgr = nullptr;
	PLUGIN_DEBUG_MQC_NEXT_CXD func = nullptr;
	mgr = minpf_get_plugin_manager();
	if (mgr && mgr->num_libraries > 0) {
		func = (PLUGIN_DEBUG_MQC_NEXT_CXD) minpf_get_symbol(
				mgr->dynamic_libraries[0],
				plugin_debug_mqc_next_cxd_method_name);
		if (func) {
			func(mqc, d);
		}
	}
}

}
