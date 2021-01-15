/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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

#include <cstdint>

namespace grk {


struct  vec4f {
	vec4f() : f{0}
	{}
	explicit vec4f(float m)
	{
		f[0]=m;
		f[1]=m;
		f[2]=m;
		f[3]=m;

	}
    float f[4];
};

uint32_t max_resolution(Resolution *GRK_RESTRICT r, uint32_t i);

template<class T> constexpr T getFilterPad(bool lossless) {
     return  lossless ? 2 : 4;
 }

template<class T> constexpr T getHorizontalPassHeight(bool lossless){
	return T(lossless ? (sizeof(int32_t)/sizeof(int32_t)) : (sizeof(vec4f) / sizeof(float)));
}

class WaveletReverse {

public:
	bool decompress(TileProcessor *p_tcd,
					TileComponent* tilec,
					uint16_t compno,
					grk_rect_u32 window,
					uint32_t numres,
					uint8_t qmfbid);

};

}
