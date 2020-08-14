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

#pragma once

namespace grk {

/**
Inverse 5-3 wavelet transform in 2-D.
Apply a reversible inverse DWT transform to a component of an image.
@param p_tcd TCD handle
@param tilec Tile component information (current tile)
@param numres Number of resolution levels to decompress
*/
bool decode_53(TileProcessor *p_tcd,
                        TileComponent* GRK_RESTRICT tilec,
                        uint32_t numres);

/**
Inverse 9-7 wavelet transform in 2-D.
Apply an irreversible inverse DWT transform to a component of an image.
@param p_tcd TCD handle
@param tilec Tile component information (current tile)
@param numres Number of resolution levels to decompress
*/
bool decode_97(TileProcessor *p_tcd,
                             TileComponent* GRK_RESTRICT tilec,
							 uint32_t numres);

}
