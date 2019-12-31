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

void decode_synch_plugin_with_host(TileProcessor *tcd);

void encode_synch_with_plugin(TileProcessor *tcd, uint32_t compno, uint32_t resno,
		uint32_t bandno, uint32_t precno, uint32_t cblkno, grk_tcd_band *band,
		grk_tcd_cblk_enc *cblk, uint32_t *numPix);

bool tile_equals(grk_plugin_tile *plugin_tile, grk_tcd_tile *p_tile);

// set context stream for debugging purposes
void set_context_stream(TileProcessor *p_tileProcessor);

void nextCXD(grk_plugin_debug_mqc *mqc, uint32_t d);

void mqc_next_plane(grk_plugin_debug_mqc *mqc);

}
