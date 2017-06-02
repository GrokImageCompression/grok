/*
*    Copyright (C) 2016-2017 Grok Image Compression Inc.
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

namespace grk {


BitIO::BitIO() : start(nullptr), 
				offset(0),
				buf_len(0),
				buf(0),
				ct(0),
				sim_out(false) {

}

bool BitIO::byteout()
{
    ct = buf == 0xff ? 7 : 8;
    if (offset >= buf_len) {
        return false;
    }
    if (!sim_out)
        start[offset] = buf;
    offset++;
    buf = 0;
    return true;
}

bool BitIO::bytein()
{
    if (offset >= buf_len) {
		assert(false);
        return false;
    }
	ct = buf == 0xff ? 7 : 8;
	buf = start[offset];
	offset++;
    return true;
}

bool BitIO::putbit( uint8_t b)
{
	bool rc = true;
    if (ct == 0) {
        rc = byteout(); 
    }
    ct--;
    buf = static_cast<uint8_t>( buf | ((uint32_t)b << ct));
	return rc;
}

bool BitIO::getbit(uint32_t* bits, uint8_t pos)
{
    if (ct == 0) {
		if (!bytein()) {
			return false;
		}
    }
    ct--;
    *bits |= ((buf >> ct) & 1) << pos;
	return true;
}


size_t BitIO::numbytes()
{
    return offset;
}

void BitIO::init_enc( uint8_t *bptr, uint64_t len)
{
    start = bptr;
	buf_len = len;
    offset = 0;
    buf = 0;
    ct = 8;
    sim_out = false;
}

void BitIO::init_dec( uint8_t *bptr, uint64_t len)
{
    start = bptr;
	buf_len = len;
	offset = 0;
    buf = 0;
    ct = 0;
}


GROK_NOSANITIZE("unsigned-integer-overflow")
bool BitIO::write( uint32_t v, uint32_t n) {
	if (n > 32U)
		return false;
	for (uint32_t i = n - 1; i < n; i--) { /* overflow used for end-loop condition */
		bool success = putbit((v >> i) & 1);
		if (!success)
			return false;
	}
	return true;
}

GROK_NOSANITIZE("unsigned-integer-overflow")
bool BitIO::read(uint32_t* bits, uint32_t n) {
	assert((n > 0U) /* && (n <= 32U)*/);
#ifdef OPJ_UBSAN_BUILD
	/* This assert fails for some corrupted images which are gracefully rejected */
	/* Add this assert only for ubsan build. */
	/* This is the condition for overflow not to occur below which is needed because of GROK_NOSANITIZE */
	assert(n <= 32U);
#endif
	*bits  = 0U;
	for (uint32_t i = n - 1; i < n; i--) { /* overflow used for end-loop condition */
		if (!getbit(bits, static_cast<uint8_t>(i))) {
			return false;
		}
	}
	return true;
}

bool BitIO::flush()
{
    if (! byteout()) {
        return false;
    }
    if (ct == 7) {
        if (! byteout()) {
            return false;
        }
    }
    return true;
}

bool BitIO::inalign()
{
    if ((buf & 0xff) == 0xff) {
        if (! bytein()) {
            return false;
        }
    }
    ct = 0;
    return true;
}
}