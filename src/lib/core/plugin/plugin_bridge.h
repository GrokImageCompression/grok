/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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

namespace grk
{
void compress_synch_with_plugin(TileProcessorCompress* tcd, uint16_t compno, uint8_t resno,
                                uint8_t bandIndex, uint64_t precinctIndex, uint32_t cblkno,
                                Subband* band, t1::CodeblockCompress* cblk, uint32_t* num_pix);

bool tile_equals(grk_plugin_tile* plugin_tile, const Tile* tilePtr);

#ifdef PLUGIN_DEBUG_ENCODE
// set context stream for debugging purposes
void set_context_stream(ITileProcessor* p_tileProcessor);

void nextCXD(grk_plugin_debug_mqc* mqc, uint32_t d);

void mqc_next_plane(grk_plugin_debug_mqc* mqc);

#endif

} // namespace grk
