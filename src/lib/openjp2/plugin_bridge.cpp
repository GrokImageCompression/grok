/**
*    Copyright (C) 2016 Grok Image Compression Inc.
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



#include "opj_includes.h"
#include "plugin_bridge.h"

bool tile_equals(opj_plugin_tile_t* plugin_tile,
						opj_tcd_tile_t *p_tile) {
	uint32_t state = opj_plugin_get_debug_state();
	if (!(state & OPJ_PLUGIN_STATE_DEBUG_ENCODE))
		return true;
	if ((!plugin_tile && p_tile) || (plugin_tile && !p_tile))
		return false;
	if (!plugin_tile && !p_tile)
		return true;
	if (plugin_tile->numComponents != p_tile->numcomps)
		return false;
	for (uint32_t compno = 0; compno < p_tile->numcomps; ++compno) {
		opj_tcd_tilecomp_t* tilecomp = p_tile->comps + compno;
		opj_plugin_tile_component_t* plugin_tilecomp = plugin_tile->tileComponents[compno];
		if (tilecomp->numresolutions != plugin_tilecomp->numResolutions)
			return false;
		for (uint32_t resno = 0; resno < tilecomp->numresolutions; ++resno) {
			opj_tcd_resolution_t* resolution =  tilecomp->resolutions + resno;
			opj_plugin_resolution_t* plugin_resolution = plugin_tilecomp->resolutions[resno];
			if (resolution->numbands != plugin_resolution->numBands)
				return false;
			for (uint32_t bandno = 0; bandno < resolution->numbands; ++bandno) {
				opj_tcd_band_t* band = resolution->bands + bandno;
				opj_plugin_band_t* plugin_band = plugin_resolution->bands[bandno];
				int num_precincts = band->precincts_data_size / sizeof(opj_tcd_precinct_t);
				if (num_precincts != plugin_band->numPrecincts)
					return false;
				for (int precno = 0; precno < num_precincts; ++precno) {
					opj_tcd_precinct_t* precinct = band->precincts + precno;
					opj_plugin_precinct_t* plugin_precinct = plugin_band->precincts[precno];
					if (precinct->ch * precinct->cw != plugin_precinct->numBlocks) {
						return false;
					}
					for (uint32_t cblkno = 0; cblkno < precinct->ch * precinct->cw; ++cblkno) {
						opj_tcd_cblk_dec_t* cblk = precinct->cblks.dec + cblkno;
						opj_plugin_code_block_t* plugin_cblk = plugin_precinct->blocks[cblkno];
						if (cblk->x0 != plugin_cblk->x0 ||
							cblk->x1 != plugin_cblk->x1 ||
							cblk->y0 != plugin_cblk->y0 ||
							cblk->y1 != plugin_cblk->y1)
							return false;
					}
				}
			}
		}
	}
	return true;
}

void encode_synch_with_plugin(opj_tcd_t *tcd,
						uint32_t compno,
						uint32_t resno,
						uint32_t bandno,
						uint32_t precno,
						uint32_t cblkno,
						opj_tcd_band_t *band,
						opj_tcd_cblk_enc_t *cblk,
						int32_t* numPix) {
	if (tcd->current_plugin_tile && tcd->current_plugin_tile->tileComponents) {
		opj_plugin_band_t* plugin_band = tcd->current_plugin_tile->tileComponents[compno]->resolutions[resno]->bands[bandno];
		opj_plugin_precinct_t* precinct = plugin_band->precincts[precno];
		opj_plugin_code_block_t* plugin_cblk = precinct->blocks[cblkno];
		uint32_t state = opj_plugin_get_debug_state();
		if (state & OPJ_PLUGIN_STATE_DEBUG_ENCODE) {
			if (band->stepsize != plugin_band->stepsize) {
				printf("Warning: ojp band step size %f differs from plugin step size %f\n", band->stepsize, plugin_band->stepsize);
			}
			if (cblk->totalpasses != plugin_cblk->numPasses)
				printf("Warning: total number of passes differ: component=%d, res=%d, band=%d, block=%d\n", compno, resno, bandno, cblkno);
		}
		cblk->totalpasses = (uint32_t)plugin_cblk->numPasses;
		*numPix = (int32_t)plugin_cblk->numPix;
		bool goodData = true;
		uint32_t totalRatePlugin = (uint32_t)plugin_cblk->compressedDataLength;
		//check data
		if (state & OPJ_PLUGIN_STATE_DEBUG_ENCODE) {
			uint32_t totalRate = 0;
			if (cblk->totalpasses > 0) {
				totalRate = (cblk->passes + cblk->totalpasses - 1)->rate;
			}
			if (cblk->data && plugin_cblk->compressedData) {
				for (uint32_t p = 0; p < totalRate; ++p) {
					if (cblk->data[p] != plugin_cblk->compressedData[p]) {
						for (uint32_t k = 0; k < totalRate; ++k) {
							if (plugin_cblk->compressedData[k] == 0xFF) {
								printf("Warning: data differs at position=%d, component=%d, res=%d, band=%d, block=%d, opj rate =%d, plugin rate=%d\n",
									k,
									compno,
									resno,
									bandno,
									cblkno,
									totalRate,
									totalRatePlugin);
							}
							break;
						}
						goodData = false;
						break;
					}
				}
			}
		}
		if (goodData)
			cblk->data = plugin_cblk->compressedData;
		cblk->data_size = (uint32_t)(plugin_cblk->compressedDataLength);
		cblk->owns_data = false;
		cblk->numbps = (uint32_t)plugin_cblk->numBitPlanes;
		if (state & OPJ_PLUGIN_STATE_DEBUG_ENCODE) {
			if (cblk->x0 != plugin_cblk->x0 ||
				cblk->y0 != plugin_cblk->y0 ||
				cblk->x1 != plugin_cblk->x1 ||
				cblk->y1 != plugin_cblk->y1) {
				printf("Error: plugin code block bounding box differs from OPJ code block");
			}
		}
		uint32_t lastRate = 0;

		for (uint32_t passno = 0; passno < cblk->totalpasses; passno++) {
			opj_tcd_pass_t *pass = cblk->passes + passno;
			opj_plugin_pass_t* pluginPass = plugin_cblk->passes + passno;

			if (state & OPJ_PLUGIN_STATE_DEBUG_ENCODE) {
				// only worry about distortion for lossy encoding
				if (tcd->tcp->tccps->qmfbid == 0 && fabs(pass->distortiondec - pluginPass->distortionDecrease) / fabs(pass->distortiondec)  > 0.01) {
					printf("Warning: distortion decrease for pass %d differs between plugin and OPJ:  plugin: %f, OPJ : %f\n", passno, pluginPass->distortionDecrease, pass->distortiondec);
				}
			}
			pass->distortiondec			= pluginPass->distortionDecrease;
			uint32_t pluginRate		= (uint32_t)(pluginPass->rate + 3);
			if (pluginRate > totalRatePlugin)
				pluginRate = totalRatePlugin;

			//Preventing generation of FF as last data byte of a pass
			if ((pluginRate>1) && (plugin_cblk->compressedData[pluginRate - 1] == 0xFF)) {
				pluginRate--;
			}

			if (state & OPJ_PLUGIN_STATE_DEBUG_ENCODE) {
				if (pluginRate != pass->rate) {
					printf("Warning: plugin rate %d differs from OPJ rate %d\n", pluginRate, pass->rate);
				}
			}

			pass->rate = pluginRate;
			pass->len = pass->rate - lastRate;
			lastRate = pass->rate;
		}
	}
}


// set context stream for debugging purposes
void set_context_stream(opj_tcd_t *p_tcd) {
	for (uint32_t compno = 0; compno < p_tcd->tile->numcomps; compno++) {
		opj_tcd_tilecomp_t *tilec = p_tcd->tile->comps + compno;
		tilec->numpix = 0;

		for (uint32_t resno = 0; resno < tilec->numresolutions; resno++) {
			opj_tcd_resolution_t *res = &tilec->resolutions[resno];

			for (uint32_t bandno = 0; bandno < res->numbands; bandno++) {
				opj_tcd_band_t *band = &res->bands[bandno];

				for (uint32_t precno = 0; precno < res->pw * res->ph; precno++) {
					opj_tcd_precinct_t *prc = &band->precincts[precno];

					for (uint32_t cblkno = 0; cblkno < prc->cw * prc->ch; cblkno++) {
						opj_tcd_cblk_enc_t *cblk = &prc->cblks.enc[cblkno];

						if (p_tcd->current_plugin_tile && p_tcd->current_plugin_tile->tileComponents) {
							opj_plugin_tile_component_t* comp = p_tcd->current_plugin_tile->tileComponents[compno];
							if (resno < comp->numResolutions) {
								opj_plugin_band_t* plugin_band = comp->resolutions[resno]->bands[bandno];
								opj_plugin_precinct_t* precinct = plugin_band->precincts[precno];
								opj_plugin_code_block_t* plugin_cblk = precinct->blocks[cblkno];
								cblk->contextStream = plugin_cblk->contextStream;
							}
						}
					}
				}
			}
		}
	}
}

// Debug: these methods wrap plugin methods for parsing a context stream
void  mqc_next_plane(plugin_debug_mqc_t *mqc) {
	minpf_plugin_manager* mgr = NULL;
	PLUGIN_DEBUG_MQC_NEXT_PLANE func = NULL;
	mgr = minpf_get_plugin_manager();
	if (mgr && mgr->num_libraries > 0) {
		func = (PLUGIN_DEBUG_MQC_NEXT_PLANE)minpf_get_symbol(mgr->dynamic_libraries[0], plugin_debug_mqc_next_plane_method_name);
		if (func) {
			func(mqc);
		}
	}
}

void nextCXD(plugin_debug_mqc_t *mqc, uint32_t d) {
	minpf_plugin_manager* mgr = NULL;
	PLUGIN_DEBUG_MQC_NEXT_CXD func = NULL;
	mgr = minpf_get_plugin_manager();
	if (mgr && mgr->num_libraries > 0) {
		func = (PLUGIN_DEBUG_MQC_NEXT_CXD)minpf_get_symbol(mgr->dynamic_libraries[0], plugin_debug_mqc_next_cxd_method_name);
		if (func) {
			func(mqc,d);
		}
	}
}

