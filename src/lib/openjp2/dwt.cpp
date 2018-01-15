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
 * Copyright (c) 2007, Jonathan Ballard <dzonatas@dzonux.net>
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
#include "Barrier.h"
#include "T1Decoder.h"
#include <atomic>
#include "testing.h"


namespace grk {


/* <summary>                             */
/* Determine maximum computed resolution level for inverse wavelet transform */
/* </summary>                            */
uint32_t dwt::max_resolution(tcd_resolution_t* restrict r, uint32_t i)
{
	uint32_t mr = 0;
	uint32_t w;
	while (--i) {
		++r;
		if (mr < (w = r->x1 - r->x0))
			mr = w;
		if (mr < (w = r->y1 - r->y0))
			mr = w;
	}
	return mr;
}


/* <summary>                             */
/* Forward lazy transform (vertical).    */
/* </summary>                            */
void dwt::deinterleave_v(int32_t *a, int32_t *b, int32_t d_n, int32_t s_n, int32_t x, int32_t cas)
{
	int32_t i = s_n;
	int32_t * l_dest = b;
	int32_t * l_src = a + cas;

	while (i--) {
		*l_dest = *l_src;
		l_dest += x;
		l_src += 2;
	} /* b[i*x]=a[2*i+cas]; */

	l_dest = b + s_n * x;
	l_src = a + 1 - cas;

	i = d_n;
	while (i--) {
		*l_dest = *l_src;
		l_dest += x;
		l_src += 2;
	} /*b[(s_n+i)*x]=a[(2*i+1-cas)];*/
}

/* <summary>			                 */
/* Forward lazy transform (horizontal).  */
/* </summary>                            */
void dwt::deinterleave_h(int32_t *a, int32_t *b, int32_t d_n, int32_t s_n, int32_t cas)
{
	int32_t i;
	int32_t * l_dest = b;
	int32_t * l_src = a + cas;

	for (i = 0; i<s_n; ++i) {
		*l_dest++ = *l_src;
		l_src += 2;
	}

	l_dest = b + s_n;
	l_src = a + 1 - cas;

	for (i = 0; i<d_n; ++i) {
		*l_dest++ = *l_src;
		l_src += 2;
	}
}

/*
Explicit calculation of the Quantization Stepsizes
*/
static void dwt_encode_stepsize(int32_t stepsize, int32_t numbps, stepsize_t *bandno_stepsize);

/* <summary>                                                              */
/* This table contains the norms of the 5-3 wavelets for different bands. */
/* </summary>                                                             */
static const double dwt_norms[4][10] = {
    {1.000, 1.500, 2.750, 5.375, 10.68, 21.34, 42.67, 85.33, 170.7, 341.3},
    {1.038, 1.592, 2.919, 5.703, 11.33, 22.64, 45.25, 90.48, 180.9},
    {1.038, 1.592, 2.919, 5.703, 11.33, 22.64, 45.25, 90.48, 180.9},
    {.7186, .9218, 1.586, 3.043, 6.019, 12.01, 24.00, 47.97, 95.93}
};

/* <summary>                                                              */
/* This table contains the norms of the 9-7 wavelets for different bands. */
/* </summary>                                                             */
static const double dwt_norms_real[4][10] = {
    {1.000, 1.965, 4.177, 8.403, 16.90, 33.84, 67.69, 135.3, 270.6, 540.9},
    {2.022, 3.989, 8.355, 17.04, 34.27, 68.63, 137.3, 274.6, 549.0},
    {2.022, 3.989, 8.355, 17.04, 34.27, 68.63, 137.3, 274.6, 549.0},
    {2.080, 3.865, 8.307, 17.18, 34.71, 69.59, 139.3, 278.6, 557.2}
};


static void dwt_encode_stepsize(int32_t stepsize, int32_t numbps, stepsize_t *bandno_stepsize)
{
    int32_t p, n;
    p = int_floorlog2(stepsize) - 13;
    n = 11 - int_floorlog2(stepsize);
    bandno_stepsize->mant = (n < 0 ? stepsize >> -n : stepsize << n) & 0x7ff;
    bandno_stepsize->expn = numbps - p;
}

/* <summary>                          */
/* Get gain of 5-3 wavelet transform. */
/* </summary>                         */
uint32_t dwt_getgain(uint8_t orient)
{
    if (orient == 0)
        return 0;
    if (orient == 1 || orient == 2)
        return 1;
    return 2;
}

/* <summary>                */
/* Get norm of 5-3 wavelet. */
/* </summary>               */
double dwt_getnorm(uint32_t level, uint8_t orient)
{
    return dwt_norms[orient][level];
}


/* <summary>                          */
/* Get gain of 9-7 wavelet transform. */
/* </summary>                         */
uint32_t dwt_getgain_real(uint8_t orient)
{
    (void)orient;
    return 0;
}

/* <summary>                */
/* Get norm of 9-7 wavelet. */
/* </summary>               */
double dwt_getnorm_real(uint32_t level, uint8_t orient)
{
    return dwt_norms_real[orient][level];
}

void dwt_calc_explicit_stepsizes(tccp_t * tccp, uint32_t prec)
{
    uint32_t numbands, bandno;
    numbands = 3 * tccp->numresolutions - 2;
    for (bandno = 0; bandno < numbands; bandno++) {
        double stepsize;
        uint32_t resno, level, orient, gain;

        resno = (bandno == 0) ? 0 : ((bandno - 1) / 3 + 1);
        orient = (bandno == 0) ? 0 : ((bandno - 1) % 3 + 1);
        level = tccp->numresolutions - 1 - resno;
        gain = (tccp->qmfbid == 0) ? 0 : ((orient == 0) ? 0 : (((orient == 1) || (orient == 2)) ? 1 : 2));
        if (tccp->qntsty == J2K_CCP_QNTSTY_NOQNT) {
            stepsize = 1.0;
        } else {
            double norm = dwt_norms_real[orient][level];
            stepsize = (double)((uint64_t)1 << gain) / norm;
        }
        dwt_encode_stepsize((int32_t) floor(stepsize * 8192.0), (int32_t)(prec + gain), &tccp->stepsizes[bandno]);
    }
}



}