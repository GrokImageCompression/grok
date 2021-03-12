/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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
#pragma once

#include "grok.h"
#include <cstdint>

#ifndef INLINE
#if defined(_MSC_VER)
#define INLINE __forceinline
#elif defined(__GNUC__)
#define INLINE inline
#else
#define INLINE
#endif /* defined(<Compiler>) */
#endif /* INLINE */

/////////////////
// buffer padding

// decompress
/**< Space for a fake FFFF marker */
const uint8_t grk_cblk_dec_compressed_data_pad_right = 2;

// compress
const uint8_t grk_cblk_enc_compressed_data_pad_left = 2;
////////////////////////////////////////////////////////

#include <math.h>
#include <cassert>
#include <cstring>
#include "mqc.h"

namespace grk {

#define T1_NMSEDEC_BITS 7
#define T1_NMSEDEC_FRACBITS (T1_NMSEDEC_BITS-1)

#define T1_NUMCTXS_ZC  9
#define T1_NUMCTXS_SC  5
#define T1_NUMCTXS_MAG 3
#define T1_NUMCTXS_AGG 1
#define T1_NUMCTXS_UNI 1

#define T1_CTXNO_ZC  0
#define T1_CTXNO_SC  (T1_CTXNO_ZC+T1_NUMCTXS_ZC)
#define T1_CTXNO_MAG (T1_CTXNO_SC+T1_NUMCTXS_SC)
#define T1_CTXNO_AGG (T1_CTXNO_MAG+T1_NUMCTXS_MAG)
#define T1_CTXNO_UNI (T1_CTXNO_AGG+T1_NUMCTXS_AGG)
#define T1_NUMCTXS   (T1_CTXNO_UNI+T1_NUMCTXS_UNI)

// We can have a maximum 31 bits in each 32 bit wavelet coefficient
// as the most significant bit is reserved for the sign.
// Since we need T1_NMSEDEC_FRACBITS fixed point fractional bits,
// we can only support a maximum of (31-T1_NMSEDEC_FRACBITS) bit planes
const uint32_t k_max_bit_planes = 31-T1_NMSEDEC_FRACBITS;

struct pass_enc {
    uint32_t rate;
    double distortiondec;
    uint32_t len;
    bool term;
};

struct cblk_enc {
    uint8_t* data;
    pass_enc* passes;
    uint32_t x0, y0, x1, y1;
    uint32_t numbps;
    uint32_t numPassesTotal;
};


/* Macros to deal with signed integer with just MSB bit set for
 * negative values (smr = signed magnitude representation) */
#define smr_abs(x)  (((uint32_t)(x)) & 0x7FFFFFFFU)
#define smr_sign(x) (((uint32_t)(x)) >> 31)
#define to_smr(x)   ((x) >= 0 ? (uint32_t)(x) : ((uint32_t)(-x) | 0x80000000U))

}

#include "T1.h"
