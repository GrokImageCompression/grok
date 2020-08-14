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


class dwt_utils {
public:

	/**
	 Get the norm of a wavelet function of a subband at a specified level for the reversible 5-3 DWT.
	 @param level Level of the wavelet function
	 @param orient Band of the wavelet function
	 @return the norm of the wavelet function
	 */
	static double getnorm_53(uint32_t level, uint8_t orient);
	/**
	 Get the norm of a wavelet function of a subband at a specified level for the irreversible 9-7 DWT
	 @param level Level of the wavelet function
	 @param orient Band of the wavelet function
	 @return the norm of the 9-7 wavelet
	 */
	static double getnorm_97(uint32_t level, uint8_t orient);


	static uint32_t max_resolution(grk_resolution* GRK_RESTRICT r, uint32_t i);
	static void deinterleave_v(int32_t *a, int32_t *b, uint32_t d_n, uint32_t s_n,
			uint32_t stride, int32_t cas);
	static void deinterleave_h(int32_t *a, int32_t *b, uint32_t d_n, uint32_t s_n,
			int32_t cas);

private:
	static double getnorm(uint32_t level, uint8_t orient, bool reversible);
};

}
