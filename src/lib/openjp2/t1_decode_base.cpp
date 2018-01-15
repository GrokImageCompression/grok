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
#include "t1_decode_base.h"
#include "T1Encoder.h"

namespace grk {

t1_decode_base::t1_decode_base(uint16_t code_block_width, uint16_t code_block_height) : dataPtr(nullptr),
																						compressed_block(nullptr),
																						compressed_block_size(0),
																						mqc(nullptr),
																						raw(nullptr)
{
	mqc = mqc_create();
	if (!mqc) {
		throw std::exception();
	}
	raw = raw_create();
	if (!raw) {
		throw std::exception();
	}
	if (code_block_width > 0 && code_block_height > 0) {
		compressed_block = (uint8_t*)grok_malloc((size_t)code_block_width * (size_t)code_block_height);
		if (!compressed_block) {
			throw std::exception();
		}
		compressed_block_size = (size_t)(code_block_width * code_block_height);
	}
}
t1_decode_base::~t1_decode_base() {
	mqc_destroy(mqc);
	raw_destroy(raw);
	if (compressed_block)
		grok_free(compressed_block);

	if (dataPtr)
		grok_aligned_free(dataPtr);
}

bool t1_decode_base::allocCompressed(tcd_cblk_dec_t* cblk) {
	/* block should have been allocated on creation of t1*/
	if (!compressed_block)
		return false;
	auto min_buf_vec = &cblk->seg_buffers;
	uint16_t total_seg_len = (uint16_t)(min_buf_vec->get_len()+ numSynthBytes);
	if (compressed_block_size < total_seg_len) {
		uint8_t* new_block = (uint8_t*)grok_realloc(compressed_block, total_seg_len);
		if (!new_block)
			return false;
		compressed_block = new_block;
		compressed_block_size = total_seg_len;
	}
	size_t offset = 0;
	// note: min_buf_vec only contains segments of non-zero length
	for (int32_t i = 0; i < min_buf_vec->size(); ++i) {
		min_buf_t* seg = (min_buf_t*)min_buf_vec->get(i);
		memcpy(compressed_block + offset, seg->buf, seg->len);
		offset += seg->len;
	}
	return true;
}

}

