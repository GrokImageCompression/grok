/*
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
*
*    This source code incorporates work covered by the following copyright and
*    permission notice:
*
*
 * The copyright in this software is being made available under the 2-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2002-2014, Universite catholique de Louvain (UCL), Belgium
 * Copyright (c) 2002-2014, Professor Benoit Macq
 * Copyright (c) 2001-2003, David Janssens
 * Copyright (c) 2002-2003, Yannick Verschueren
 * Copyright (c) 2003-2007, Francois-Olivier Devaux
 * Copyright (c) 2003-2014, Antonin Descampe
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2007, Callum Lerwick <seg@haxxed.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "grok_includes.h"

// tier 1 interface
#include "mqc.h"
#include "t1.h"
#include "t1_opt_luts.h"

namespace grk {

t1::t1() : w(0),
	h(0),
	flags(nullptr),
	flags_stride(0) {
}

t1::~t1()
{
	if (flags) {
		grok_aligned_free(flags);
	}
}

/*
Allocate buffers
@param cblkw	maximum width of code block
@param cblkh	maximum height of code block

*/
bool t1::allocateBuffers(uint16_t cblkw, uint16_t cblkh)
{
	if (!flags) {
		auto flags_stride = cblkw + 2;
		auto flags_height = (cblkh + 3U) >> 2;
		auto flagssize = flags_stride * (flags_height + 2);
		flags = (flag_opt_t*)grok_aligned_malloc(flagssize * sizeof(flag_opt_t));
		if (!flags) {
			/* FIXME event manager error callback */
			return false;
		}
	}
	return true;
}

/*
Initialize buffers
@param w	width of code block
@param h	height of code block
*/
void t1::initBuffers(uint16_t w, uint16_t h) {
	this->w = w;
	this->h = h;
	flags_stride = (uint16_t)(w + 2);
	auto flags_height = (h + 3U) >> 2;
	auto flagssize = flags_stride * (flags_height + 2);
	memset(flags, 0, flagssize * sizeof(flag_opt_t));

	// handle last stripe if its height is less than 4
	flag_opt_t* p = flags;
	unsigned char lastHeight = h & 3;
	if (lastHeight) {
		flag_opt_t v = T1_PI_3 | ((lastHeight <= 2) << T1_PI_2_I) | ((lastHeight == 1) << T1_PI_1_I);
		p = flags + ((flags_height)* flags_stride);
		for (uint32_t x = 0; x < flags_stride; ++x) {
			*p++ = v;
		}
	}

	///////////////////////////////////////////////////////////////////////////////////
	// Top and bottom boundary lines of flags buffer: set magic value
	// to prevent any passes from being interested in these entries
	// Note: setting magic below is not strictly necessary, but should save a few cycles.
	for (uint32_t x = 0; x < flags_stride; ++x) {
		*p++ = (T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3);
	}
	p = flags + ((flags_height + 1) * flags_stride);
	for (uint32_t x = 0; x < flags_stride; ++x) {
		*p++ = (T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3);
	}
	////////////////////////////////////////////////////////////////////////////
}
uint8_t t1::getZeroCodingContext(uint32_t f, uint8_t orient)
{
	return lut_ctxno_zc_opt[(orient << 9) | (f & T1_SIGMA_NEIGHBOURS)];
}
uint32_t t1::getSignCodingOrSPPByteIndex(uint32_t fX, uint32_t pfX, uint32_t nfX, uint32_t ci3) {
	/*
	0 pfX T1_CHI_CURRENT           T1_LUT_CTXNO_SGN_W
	1 tfX T1_SIGMA_1            T1_LUT_CTXNO_SIG_N
	2 nfX T1_CHI_CURRENT           T1_LUT_CTXNO_SGN_E
	3 tfX T1_SIGMA_3            T1_LUT_CTXNO_SIG_W
	4  fX T1_CHI_(THIS - 1)     T1_LUT_CTXNO_SGN_N
	5 tfX T1_SIGMA_5            T1_LUT_CTXNO_SIG_E
	6  fX T1_CHI_(THIS + 1)     T1_LUT_CTXNO_SGN_S
	7 tfX T1_SIGMA_7            T1_LUT_CTXNO_SIG_S
	*/
	uint32_t lu = (fX >> ci3) & (T1_SIGMA_1 | T1_SIGMA_3 | T1_SIGMA_5 | T1_SIGMA_7);

	lu |= (pfX >> (T1_CHI_CURRENT_I + ci3)) & (1U << 0);
	lu |= (nfX >> (T1_CHI_CURRENT_I - 2U + ci3)) & (1U << 2);
	if (ci3 == 0U) {
		lu |= (fX >> (T1_CHI_0_I - 4U)) & (1U << 4);
	}
	else {
		lu |= (fX >> (T1_CHI_1_I - 4U + (ci3 - 3))) & (1U << 4);
	}
	lu |= (fX >> (T1_CHI_2_I - 6U + ci3)) & (1U << 6);
	return lu;
}
uint8_t t1::getSignCodingContext(uint32_t lu)
{
	return lut_ctxno_sc_opt[lu];
}
uint8_t t1::getMRPContext(uint32_t f) {
	return (f & T1_MU_CURRENT) ? (T1_CTXNO_MAG + 2) : ((f & T1_SIGMA_NEIGHBOURS) ? T1_CTXNO_MAG + 1 : T1_CTXNO_MAG);
}

uint8_t t1::getSPByte(uint32_t lu)
{
	return lut_spb_opt[lu];
}
void t1::updateFlags(flag_opt_t *flagsp, uint32_t ci3, uint32_t s, uint32_t stride, uint8_t vsc) {
	/* update current flag */
	flagsp[-1] |= T1_SIGMA_5 << (ci3);
	*flagsp |= (((s) << T1_CHI_1_I) | T1_SIGMA_4) << (ci3);
	flagsp[1] |= T1_SIGMA_3 << (ci3);
	/* update north flag if we are at top of column and VSC is false */
	if (((ci3) == 0U) & ((vsc) == 0U)) {
		flag_opt_t* north = flagsp - (stride);
		*north |= ((s) << T1_CHI_5_I) | T1_SIGMA_16;
		north[-1] |= T1_SIGMA_17;
		north[1] |= T1_SIGMA_15;
	}
	/* update south flag*/
	if (ci3 == 9u) {
		flag_opt_t* south = (flagsp)+(stride);
		*south |= ((s) << T1_CHI_0_I) | T1_SIGMA_1;
		south[-1] |= T1_SIGMA_2;
		south[1] |= T1_SIGMA_0;
	}
}

}

