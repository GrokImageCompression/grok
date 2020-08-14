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

#include <stdint.h>

namespace grk {

struct grk_dwt53 {
	int32_t *data;
	int64_t d_n;
	int64_t s_n;
	grk_pt range_even;
	grk_pt range_odd;
	int64_t interleaved_offset;
	uint8_t odd_top_left_bit;
};

class dwt53 {
public:
	void encode_line(int32_t* GRK_RESTRICT a, int32_t d_n, int32_t s_n, uint8_t cas);
};

}
