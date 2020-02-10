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
 */

#include "T1Factory.h"
#include "T1Part1.h"
#include "T1Part1OPJ.h"
#include "T1HT.h"

namespace grk {

T1Interface* T1Factory::get_t1(bool isEncoder,
								grk_coding_parameters *cp,
								grk_tcp *tcp,
								uint16_t maxCblkW,
								uint16_t maxCblkH) {
	if (cp && cp->ccap)
		return new t1_ht::T1HT(isEncoder, tcp, maxCblkW, maxCblkH);
	else
		return new t1_part1::T1Part1OPJ(isEncoder, tcp, maxCblkW, maxCblkH);
}

}
