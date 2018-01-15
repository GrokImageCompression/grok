/**
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
*/


#pragma once

/* BIBO analysis - extra bits needed to avoid overflow:

Lossless:
without colour transform: 4 extra bits
with colour transform:    5 extra bits

Lossy:

Need 1 extra bit

So,  worst-case scenario is lossless with colour transform : need to add 5 more bits to prec to avoid overflow
*/
#define BIBO_EXTRA_BITS 5

namespace grk {

void decode_synch_plugin_with_host(tcd_t *tcd, event_mgr_t *p_manager);

void encode_synch_with_plugin(tcd_t *tcd,
	uint32_t compno,
	uint32_t resno,
	uint32_t bandno,
	uint32_t precno,
	uint32_t cblkno,
	tcd_band_t *band,
	tcd_cblk_enc_t *cblk,
	uint32_t* numPix);


bool tile_equals(grok_plugin_tile_t* plugin_tile,
				tcd_tile_t *p_tile);

// set context stream for debugging purposes
void set_context_stream(tcd_t *p_tcd);

void nextCXD(plugin_debug_mqc_t *mqc, uint32_t d);

void  mqc_next_plane(plugin_debug_mqc_t *mqc);



}