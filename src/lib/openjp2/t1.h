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

namespace grk {

#define T1_SIG_NE 0x0001	/**< Context orientation : North-East direction */
#define T1_SIG_SE 0x0002	/**< Context orientation : South-East direction */
#define T1_SIG_SW 0x0004	/**< Context orientation : South-West direction */
#define T1_SIG_NW 0x0008	/**< Context orientation : North-West direction */
#define T1_SIG_N 0x0010		/**< Context orientation : North direction */
#define T1_SIG_E 0x0020		/**< Context orientation : East direction */
#define T1_SIG_S 0x0040		/**< Context orientation : South direction */
#define T1_SIG_W 0x0080		/**< Context orientation : West direction */

#define T1_SGN_N 0x0100
#define T1_SGN_E 0x0200
#define T1_SGN_S 0x0400
#define T1_SGN_W 0x0800


#define T1_NUMCTXS_ZC 9
#define T1_NUMCTXS_SC 5
#define T1_NUMCTXS_MAG 3
#define T1_NUMCTXS_AGG 1
#define T1_NUMCTXS_UNI 1

#define T1_CTXNO_ZC 0
#define T1_CTXNO_SC (T1_CTXNO_ZC+T1_NUMCTXS_ZC)
#define T1_CTXNO_MAG (T1_CTXNO_SC+T1_NUMCTXS_SC)
#define T1_CTXNO_AGG (T1_CTXNO_MAG+T1_NUMCTXS_MAG)
#define T1_CTXNO_UNI (T1_CTXNO_AGG+T1_NUMCTXS_AGG)
#define T1_NUMCTXS (T1_CTXNO_UNI+T1_NUMCTXS_UNI)

#define T1_NMSEDEC_BITS 7
#define T1_NMSEDEC_FRACBITS (T1_NMSEDEC_BITS-1)

#define T1_TYPE_MQ 0	/**< Normal coding using entropy coder */
#define T1_TYPE_RAW 1	/**< No encoding the information is store under raw format in codestream (mode switch RAW)*/

		/*********************/
		/*   STATE FLAGS     */
		/*********************/


		/** We hold the state of individual data points for the T1 encoder using
		*  a single 32-bit flags word to hold the state of 4 data points.  This corresponds
		*  to the 4-point-high columns that the data is processed in.
		*  These #defines declare the layout of a 32-bit flags word.
		*  This is currently done for encoding only.
		*/

		/* T1_SIGMA_XXX is significance flag for stripe column and neighbouring locations: 18 locations in total
		*  As an example, the bits T1_SIGMA_3, T1_SIGMA_4 and T1_SIGMA_5
		*  indicate the significance state of the west neighbour of data point zero
		*  of our four, the point itself, and its east neighbour respectively.
		*  Many of the bits are arranged so that given a flags word, you can
		*  look at the values for the data point 0, then shift the flags
		*  word right by 3 bits and look at the same bit positions to see the
		*  values for data point 1.
		*/

#define T1_SIGMA_0  (1U << 0)
#define T1_SIGMA_1  (1U << 1)
#define T1_SIGMA_2  (1U << 2)
#define T1_SIGMA_3  (1U << 3)
#define T1_SIGMA_4  (1U << 4)
#define T1_SIGMA_5  (1U << 5)
#define T1_SIGMA_6  (1U << 6)
#define T1_SIGMA_7  (1U << 7)
#define T1_SIGMA_8  (1U << 8)
#define T1_SIGMA_9  (1U << 9)
#define T1_SIGMA_10 (1U << 10)
#define T1_SIGMA_11 (1U << 11)
#define T1_SIGMA_12 (1U << 12)
#define T1_SIGMA_13 (1U << 13)
#define T1_SIGMA_14 (1U << 14)
#define T1_SIGMA_15 (1U << 15)
#define T1_SIGMA_16 (1U << 16)
#define T1_SIGMA_17 (1U << 17)

		/*
		* T1_CHI_X is the sign flag for the (X+1)th location in the stripe column.
		* T1_PI_X  indicates whether Xth location was coded in significance propagation pass
		* T1_MU_X  indicates whether Xth location belongs to the magnitude refinement pass
		*/

#define T1_CHI_0_I  18
#define T1_CHI_0    (1U << T1_CHI_0_I)
#define T1_CHI_1_I  19
#define T1_CHI_1    (1U << T1_CHI_1_I)
#define T1_MU_0     (1U << 20)
#define T1_PI_0     (1U << 21)
#define T1_CHI_2_I  22
#define T1_CHI_2    (1U << T1_CHI_2_I)
#define T1_MU_1     (1U << 23)
#define T1_PI_1_I	24
#define T1_PI_1     (1U << T1_PI_1_I)
#define T1_CHI_3    (1U << 25)
#define T1_MU_2     (1U << 26)
#define T1_PI_2_I	27
#define T1_PI_2     (1U << T1_PI_2_I)
#define T1_CHI_4    (1U << 28)
#define T1_MU_3     (1U << 29)
#define T1_PI_3     (1U << 30)
#define T1_CHI_5_I	31 
#define T1_CHI_5    (1U << T1_CHI_5_I)

		/**The #defines below are convenience flags; say you have a flags word
		*  f, you can do things like
		*
		*  (f & T1_SIGMA_CURRENT)
		*
		*  to see the significance bit of data point 0, then do
		*
		*  ((f >> 3) & T1_SIGMA_CURRENT)
		*
		*  to see the significance bit of data point 1.
		*/

#define T1_SIGMA_NW   T1_SIGMA_0
#define T1_SIGMA_N    T1_SIGMA_1
#define T1_SIGMA_NE   T1_SIGMA_2
#define T1_SIGMA_W    T1_SIGMA_3
#define T1_SIGMA_CURRENT T1_SIGMA_4
#define T1_SIGMA_E    T1_SIGMA_5
#define T1_SIGMA_SW   T1_SIGMA_6
#define T1_SIGMA_S    T1_SIGMA_7
#define T1_SIGMA_SE   T1_SIGMA_8
#define T1_SIGMA_NEIGHBOURS (T1_SIGMA_NW | T1_SIGMA_N | T1_SIGMA_NE | T1_SIGMA_W | T1_SIGMA_E | T1_SIGMA_SW | T1_SIGMA_S | T1_SIGMA_SE)

#define T1_CHI_CURRENT   T1_CHI_1
#define T1_CHI_CURRENT_I T1_CHI_1_I
#define T1_MU_CURRENT    T1_MU_0
#define T1_PI_CURRENT    T1_PI_0

#define T1_LUT_SGN_W (1U << 0)
#define T1_LUT_SIG_N (1U << 1)
#define T1_LUT_SGN_E (1U << 2)
#define T1_LUT_SIG_W (1U << 3)
#define T1_LUT_SGN_N (1U << 4)
#define T1_LUT_SIG_E (1U << 5)
#define T1_LUT_SGN_S (1U << 6)
#define T1_LUT_SIG_S (1U << 7)


		// sign bit is stored at this location in 32 bit coefficient
#define T1_DATA_SIGN_BIT_INDEX 31

#define FLAGS_ADDRESS(x, y) (flags + ((x) + 1 + (((y) >> 2) + 1) * flags_stride))


	typedef uint32_t flag_opt_t;

	class t1 {
	public:
		t1();
		virtual ~t1();
		uint16_t w;
		uint16_t h;
	protected:
		bool allocateBuffers(uint16_t cblkw, uint16_t cblkh);
		void initBuffers(uint16_t w, uint16_t h);

		flag_opt_t *flags;
		uint16_t flags_stride;

		void  updateFlags(flag_opt_t *flagsp, uint32_t ci3, uint32_t s, uint32_t stride, uint8_t vsc);

		uint8_t		getZeroCodingContext(uint32_t f, uint8_t orient);
		uint8_t		getMRPContext(uint32_t f);
		uint8_t		getSignCodingContext(uint32_t lu);
		uint8_t		getSPByte(uint32_t lu);
		uint32_t    getSignCodingOrSPPByteIndex(uint32_t fX, uint32_t pfX, uint32_t nfX, uint32_t ci3);

	};


}

