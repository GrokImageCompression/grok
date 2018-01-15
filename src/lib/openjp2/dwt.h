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


#pragma once

namespace grk {

struct dwt_t {
	int32_t* mem;
	uint32_t d_n;
	uint32_t s_n;
	uint8_t cas;
};

class dwt : public dwt_interface {
public:
	virtual ~dwt() {}
protected:
	uint32_t max_resolution(tcd_resolution_t* restrict r, uint32_t i);
	void deinterleave_v(int32_t *a, int32_t *b, int32_t d_n, int32_t s_n, int32_t x, int32_t cas);
	void deinterleave_h(int32_t *a, int32_t *b, int32_t d_n, int32_t s_n, int32_t cas);
};

/**
Get the gain of a subband for the reversible 5-3 DWT.
@param orient Number that identifies the subband (0->LL, 1->HL, 2->LH, 3->HH)
@return 0 if orient = 0, returns 1 if orient = 1 or 2, returns 2 otherwise
*/
uint32_t dwt_getgain(uint8_t orient) ;
/**
Get the norm of a wavelet function of a subband at a specified level for the reversible 5-3 DWT.
@param level Level of the wavelet function
@param orient Band of the wavelet function
@return the norm of the wavelet function
*/
double dwt_getnorm(uint32_t level, uint8_t orient);
/**
Get the gain of a subband for the irreversible 9-7 DWT.
@param orient Number that identifies the subband (0->LL, 1->HL, 2->LH, 3->HH)
@return the gain of the 9-7 wavelet transform
*/
uint32_t dwt_getgain_real(uint8_t orient);
/**
Get the norm of a wavelet function of a subband at a specified level for the irreversible 9-7 DWT
@param level Level of the wavelet function
@param orient Band of the wavelet function
@return the norm of the 9-7 wavelet
*/
double dwt_getnorm_real(uint32_t level, uint8_t orient);
/**
Explicit calculation of the Quantization Stepsizes
@param tccp Tile-component coding parameters
@param prec Precint analyzed
*/
void dwt_calc_explicit_stepsizes(tccp_t * tccp, uint32_t prec);
}