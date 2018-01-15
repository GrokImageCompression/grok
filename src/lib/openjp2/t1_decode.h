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
#include <vector>
#include "testing.h"
#include "Tier1.h"
#include "t1.h"

namespace grk {

typedef uint16_t flag_t;

struct mqc_t;
struct raw_t;

class t1_decode_base;

class t1_decode : public t1_decode_base {
public:
	t1_decode(uint16_t code_block_width, uint16_t code_block_height);
	~t1_decode();
	/**
	Decode 1 code-block
	@param t1 T1 handle
	@param cblk Code-block coding parameters
	@param orient
	@param roishift Region of interest shifting value
	@param cblksty Code-block style
	*/
	bool decode_cblk(tcd_cblk_dec_t* cblk,
		uint8_t orient,
		uint32_t roishift,
		uint32_t cblksty) override;
	void postDecode(decodeBlockInfo* block) override;
private:
	flag_t *flags;
	uint16_t flags_stride;

	bool allocateBuffers(uint16_t w, uint16_t h);
	void initBuffers(uint16_t w, uint16_t h);
	inline void sigpass_step_raw(flag_t *flagsp,
		int32_t *datap,
		int32_t oneplushalf,
		bool vsc);
	inline void sigpass_step(flag_t *flagsp,
		int32_t *datap,
		uint8_t orient,
		int32_t oneplushalf);
	inline void sigpass_step_vsc(flag_t *flagsp,
		int32_t *datap,
		uint8_t orient,
		int32_t oneplushalf,
		bool vsc);
	void sigpass_raw(int32_t bpno, uint32_t cblksty);
	void sigpass(int32_t bpno, uint8_t orient);
	void sigpass_vsc(int32_t bpno, uint8_t orient);

	void refpass_raw(int32_t bpno, uint32_t cblksty);
	void refpass(int32_t bpno);
	void refpass_vsc(int32_t bpno);
	inline void  refpass_step_raw(flag_t *flagsp,
		int32_t *datap,
		int32_t poshalf,
		bool vsc);
	inline void refpass_step(flag_t *flagsp,
		int32_t *datap,
		int32_t poshalf);
	inline void refpass_step_vsc(flag_t *flagsp,
		int32_t *datap,
		int32_t poshalf,
		bool vsc);

	void clnpass_step_partial(flag_t *flagsp,
		int32_t *datap,
		int32_t oneplushalf);
	void clnpass_step(flag_t *flagsp,
		int32_t *datap,
		uint8_t orient,
		int32_t oneplushalf);
	void clnpass_step_vsc(flag_t *flagsp,
		int32_t *datap,
		uint8_t orient,
		int32_t oneplushalf,
		int32_t partial,
		bool vsc);
	void clnpass(int32_t bpno,
		uint8_t orient,
		uint32_t cblksty);

	void updateflags(flag_t *flagsp, uint32_t s, uint32_t stride);
};

}

