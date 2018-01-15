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

// two bytes 0xFF 0xFF are synthesized at the end of each code block compressed buffer
// to simulate "end of compressed stream" marker. This allows code to avoid checking
// for end of compressed stream by length
const uint16_t numSynthBytes = 2;
const uint16_t synthBytes = 0xFFFF;

struct mqc_t;
struct raw_t;
class t1;

class t1_decode_base : public t1 {
public:
	t1_decode_base(uint16_t code_block_width, uint16_t code_block_height);
	virtual ~t1_decode_base();

	/**
	Decode 1 code-block
	@param t1 T1 handle
	@param cblk Code-block coding parameters
	@param orient
	@param roishift Region of interest shifting value
	@param cblksty Code-block style
	*/
	virtual bool decode_cblk(tcd_cblk_dec_t* cblk,
		uint8_t orient,
		uint32_t roishift,
		uint32_t cblksty)=0;
	virtual void postDecode(decodeBlockInfo* block)=0;


	int32_t  *dataPtr;

protected:
	bool allocCompressed(tcd_cblk_dec_t* cblk);
	uint8_t* compressed_block;
	size_t compressed_block_size;
	mqc_t *mqc;
	raw_t *raw;

};

}


