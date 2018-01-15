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

#include <stdint.h>

namespace grk {

struct tcd_tilecomp_t;
class dwt;

struct dwt53_t {
	int32_t* data;
	int64_t d_n;
	int64_t s_n;
	pt_t	range_even;
	pt_t	range_odd;
	int64_t  interleaved_offset;
	uint8_t odd_top_left_bit;
};



class dwt53 : public dwt {
public:
	/**
	Forward wavelet transform in 2-D.
	Apply a reversible DWT transform to a component of an image.
	@param tilec Tile component information (current tile)
	*/
	bool encode(tcd_tilecomp_t * tilec);

	/**
	Inverse wavelet transform in 2-D.
	Apply a reversible inverse DWT transform to a component of an image.
	@param tilec Tile component information (current tile)
	@param numres Number of resolution levels to decode
	*/
	bool decode(tcd_tilecomp_t* tilec,
		uint32_t numres,
		uint32_t numThreads);

	/**
	Inverse wavelet transform in 2-D.
	Apply a reversible inverse DWT transform to a component of an image.
	@param tilec Tile component information (current tile)
	@param numres Number of resolution levels to decode
	*/
	bool region_decode(tcd_tilecomp_t* tilec,
		uint32_t numres,
		uint32_t numThreads);

private:
	void encode_line(int32_t *a, int32_t d_n, int32_t s_n, uint8_t cas);
	void decode_line(dwt_t *v);
	void interleave_v(dwt_t* v, int32_t *a, int32_t x);
	void interleave_h(dwt_t* h, int32_t *a);
	void region_decode_1d(dwt53_t *buffer);
	/**
	Inverse lazy transform (horizontal)
	*/
	void region_interleave_h(dwt53_t* buffer_h,
		int32_t *tile_data);
	/**
	Inverse lazy transform (vertical)
	*/
	void region_interleave_v(dwt53_t* buffer_v,
		int32_t *tile_data,
		size_t stride);

};

}