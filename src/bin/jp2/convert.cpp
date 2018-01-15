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
 * Copyright (c) 2006-2007, Parvatha Elangovan
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
#include "opj_apps_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

extern "C" {

#include "openjpeg.h"
#include "convert.h"

}

bool grok_set_binary_mode(FILE* file) {
#ifdef _WIN32
	return (_setmode(_fileno(file), _O_BINARY) != -1);
#else
	(void)file;
	return true;
#endif
}

/* Component precision scaling */
void clip_component(opj_image_comp_t* component, uint32_t precision)
{
    size_t i;
    size_t len;
	uint32_t umax = UINT_MAX;

    len = (size_t)component->w * (size_t)component->h;
    if (precision < 32) {
        umax = (1U << precision) - 1U;
    }

    if (component->sgnd) {
        int32_t* l_data = component->data;
        int32_t max = (int32_t)(umax / 2U);
        int32_t min = -max - 1;
        for (i = 0; i < len; ++i) {
            if (l_data[i] > max) {
                l_data[i] = max;
            } else if (l_data[i] < min) {
                l_data[i] = min;
            }
        }
    } else {
        uint32_t* l_data = (uint32_t*)component->data;
        for (i = 0; i < len; ++i) {
            if (l_data[i] > umax) {
                l_data[i] = umax;
            }
        }
    }
    component->prec = precision;
}

/* Component precision scaling */
static void scale_component_up(opj_image_comp_t* component, uint32_t precision)
{
    size_t i, len;

    len = (size_t)component->w * (size_t)component->h;
    if (component->sgnd) {
        int64_t  newMax =(int64_t)1U << (precision - 1);
        int64_t  oldMax = (int64_t)1U << (component->prec - 1);
        int32_t* l_data = component->data;
        for (i = 0; i < len; ++i) {
            l_data[i] = (int32_t)(((int64_t)l_data[i] * newMax) / oldMax);
        }
    } else {
        uint64_t  newMax = ((uint64_t)1U << precision) - 1U;
        uint64_t  oldMax = ((uint64_t)1U << component->prec) - 1U;
        uint32_t* l_data = (uint32_t*)component->data;
        for (i = 0; i < len; ++i) {
            l_data[i] = (uint32_t)(((uint64_t)l_data[i] * newMax) / oldMax);
        }
    }
    component->prec = precision;
}
void scale_component(opj_image_comp_t* component, uint32_t precision)
{
    int shift;
    size_t i, len;

    if (component->prec == precision) {
        return;
    }
    if (component->prec < precision) {
        scale_component_up(component, precision);
        return;
    }
    shift = (int)(component->prec - precision);
    len = (size_t)component->w * (size_t)component->h;
    if (component->sgnd) {
        int32_t* l_data = component->data;
        for (i = 0; i < len; ++i) {
            l_data[i] >>= shift;
        }
    } else {
        uint32_t* l_data = (uint32_t*)component->data;
        for (i = 0; i < len; ++i) {
            l_data[i] >>= shift;
        }
    }
    component->prec = precision;
}


/* planar / interleaved conversions */
/* used by PNG/TIFF */
static void convert_32s_C1P1(const int32_t* pSrc, int32_t* const* pDst, size_t length)
{
    memcpy(pDst[0], pSrc, length * sizeof(int32_t));
}
static void convert_32s_C2P2(const int32_t* pSrc, int32_t* const* pDst, size_t length)
{
    size_t i;
    int32_t* pDst0 = pDst[0];
    int32_t* pDst1 = pDst[1];

    for (i = 0; i < length; i++) {
        pDst0[i] = pSrc[2*i+0];
        pDst1[i] = pSrc[2*i+1];
    }
}
static void convert_32s_C3P3(const int32_t* pSrc, int32_t* const* pDst, size_t length)
{
    size_t i;
    int32_t* pDst0 = pDst[0];
    int32_t* pDst1 = pDst[1];
    int32_t* pDst2 = pDst[2];

    for (i = 0; i < length; i++) {
        pDst0[i] = pSrc[3*i+0];
        pDst1[i] = pSrc[3*i+1];
        pDst2[i] = pSrc[3*i+2];
    }
}
static void convert_32s_C4P4(const int32_t* pSrc, int32_t* const* pDst, size_t length)
{
    size_t i;
    int32_t* pDst0 = pDst[0];
    int32_t* pDst1 = pDst[1];
    int32_t* pDst2 = pDst[2];
    int32_t* pDst3 = pDst[3];

    for (i = 0; i < length; i++) {
        pDst0[i] = pSrc[4*i+0];
        pDst1[i] = pSrc[4*i+1];
        pDst2[i] = pSrc[4*i+2];
        pDst3[i] = pSrc[4*i+3];
    }
}
const convert_32s_CXPX convert_32s_CXPX_LUT[5] = {
    nullptr,
    convert_32s_C1P1,
    convert_32s_C2P2,
    convert_32s_C3P3,
    convert_32s_C4P4
};

static void convert_32s_P1C1(int32_t const* const* pSrc, int32_t* pDst, size_t length, int32_t adjust)
{
    size_t i;
    const int32_t* pSrc0 = pSrc[0];

    for (i = 0; i < length; i++) {
        pDst[i] = pSrc0[i] + adjust;
    }
}
static void convert_32s_P2C2(int32_t const* const* pSrc, int32_t* pDst, size_t length, int32_t adjust)
{
    size_t i;
    const int32_t* pSrc0 = pSrc[0];
    const int32_t* pSrc1 = pSrc[1];

    for (i = 0; i < length; i++) {
        pDst[2*i+0] = pSrc0[i] + adjust;
        pDst[2*i+1] = pSrc1[i] + adjust;
    }
}
static void convert_32s_P3C3(int32_t const* const* pSrc, int32_t* pDst, size_t length, int32_t adjust)
{
    size_t i;
    const int32_t* pSrc0 = pSrc[0];
    const int32_t* pSrc1 = pSrc[1];
    const int32_t* pSrc2 = pSrc[2];

    for (i = 0; i < length; i++) {
        pDst[3*i+0] = pSrc0[i] + adjust;
        pDst[3*i+1] = pSrc1[i] + adjust;
        pDst[3*i+2] = pSrc2[i] + adjust;
    }
}
static void convert_32s_P4C4(int32_t const* const* pSrc, int32_t* pDst, size_t length, int32_t adjust)
{
    size_t i;
    const int32_t* pSrc0 = pSrc[0];
    const int32_t* pSrc1 = pSrc[1];
    const int32_t* pSrc2 = pSrc[2];
    const int32_t* pSrc3 = pSrc[3];

    for (i = 0; i < length; i++) {
        pDst[4*i+0] = pSrc0[i] + adjust;
        pDst[4*i+1] = pSrc1[i] + adjust;
        pDst[4*i+2] = pSrc2[i] + adjust;
        pDst[4*i+3] = pSrc3[i] + adjust;
    }
}
const convert_32s_PXCX convert_32s_PXCX_LUT[5] = {
    nullptr,
    convert_32s_P1C1,
    convert_32s_P2C2,
    convert_32s_P3C3,
    convert_32s_P4C4
};

/* bit depth conversions */
/* used by PNG/TIFF up to 8bpp */
static void convert_1u32s_C1R(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
    size_t i;
    for (i = 0; i < (length & ~(size_t)7U); i+=8U) {
        uint32_t val = *pSrc++;
        pDst[i+0] = INV((int32_t)( val >> 7),1,invert);
        pDst[i+1] = INV((int32_t)((val >> 6) & 0x1U), 1,invert);
        pDst[i+2] = INV((int32_t)((val >> 5) & 0x1U), 1,invert);
        pDst[i+3] = INV((int32_t)((val >> 4) & 0x1U), 1,invert);
        pDst[i+4] = INV((int32_t)((val >> 3) & 0x1U), 1,invert);
        pDst[i+5] = INV((int32_t)((val >> 2) & 0x1U), 1,invert);
        pDst[i+6] = INV((int32_t)((val >> 1) & 0x1U), 1,invert);
        pDst[i+7] = INV((int32_t)(val & 0x1U), 1,invert);
    }
    if (length & 7U) {
        uint32_t val = *pSrc++;
        length = length & 7U;
        pDst[i+0] = INV((int32_t)(val >> 7),1,invert);

        if (length > 1U) {
            pDst[i+1] = INV((int32_t)((val >> 6) & 0x1U), 1,invert);
            if (length > 2U) {
                pDst[i+2] = INV((int32_t)((val >> 5) & 0x1U), 1,invert);
                if (length > 3U) {
                    pDst[i+3] = INV((int32_t)((val >> 4) & 0x1U), 1,invert);
                    if (length > 4U) {
                        pDst[i+4] = INV((int32_t)((val >> 3) & 0x1U), 1,invert);
                        if (length > 5U) {
                            pDst[i+5] = INV((int32_t)((val >> 2) & 0x1U), 1,invert);
                            if (length > 6U) {
                                pDst[i+6] = INV((int32_t)((val >> 1) & 0x1U), 1,invert);
                            }
                        }
                    }
                }
            }
        }
    }
}
static void convert_2u32s_C1R(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
    size_t i;
    for (i = 0; i < (length & ~(size_t)3U); i+=4U) {
        uint32_t val = *pSrc++;
        pDst[i+0] = INV((int32_t)( val >> 6),3,invert);
        pDst[i+1] = INV((int32_t)((val >> 4) & 0x3U),3,invert);
        pDst[i+2] = INV((int32_t)((val >> 2) & 0x3U),3,invert);
        pDst[i+3] = INV((int32_t)(val & 0x3U),3,invert);
    }
    if (length & 3U) {
        uint32_t val = *pSrc++;
        length = length & 3U;
        pDst[i+0] = INV((int32_t)(val >> 6), 3, invert);

        if (length > 1U) {
            pDst[i+1] = INV((int32_t)((val >> 4) & 0x3U),3,invert);
            if (length > 2U) {
                pDst[i+2] = INV((int32_t)((val >> 2) & 0x3U),3,invert);

            }
        }
    }
}
static void convert_4u32s_C1R(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
    size_t i;
    for (i = 0; i < (length & ~(size_t)1U); i+=2U) {
        uint32_t val = *pSrc++;
        pDst[i+0] = INV((int32_t)(val >> 4),15,invert);
        pDst[i+1] = INV((int32_t)(val & 0xFU),15,invert);
    }
    if (length & 1U) {
        uint8_t val = *pSrc++;
        pDst[i+0] = INV((int32_t)(val >> 4), 15, invert);
    }
}
static void convert_6u32s_C1R(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
    size_t i;
    for (i = 0; i < (length & ~(size_t)3U); i+=4U) {
        uint32_t val0 = *pSrc++;
        uint32_t val1 = *pSrc++;
        uint32_t val2 = *pSrc++;
        pDst[i+0] = INV((int32_t)(val0 >> 2),63,invert);
        pDst[i+1] = INV((int32_t)(((val0 & 0x3U) << 4) | (val1 >> 4)), 63, invert);
        pDst[i+2] = INV((int32_t)(((val1 & 0xFU) << 2) | (val2 >> 6)), 63, invert);
        pDst[i+3] = INV((int32_t)(val2 & 0x3FU), 63, invert);

    }
    if (length & 3U) {
        uint32_t val0 = *pSrc++;
        length = length & 3U;
        pDst[i+0] = INV((int32_t)(val0 >> 2), 63, invert);

        if (length > 1U) {
            uint32_t val1 = *pSrc++;
            pDst[i+1] = INV((int32_t)(((val0 & 0x3U) << 4) | (val1 >> 4)), 63, invert);
            if (length > 2U) {
                uint32_t val2 = *pSrc++;
                pDst[i+2] = INV((int32_t)(((val1 & 0xFU) << 2) | (val2 >> 6)), 63, invert);
            }
        }
    }
}
static void convert_8u32s_C1R(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
    size_t i;
    for (i = 0; i < length; i++) {
        pDst[i] = INV(pSrc[i],0xFF,invert);
    }
}
const convert_XXx32s_C1R convert_XXu32s_C1R_LUT[9] = {
    nullptr,
    convert_1u32s_C1R,
    convert_2u32s_C1R,
    nullptr,
    convert_4u32s_C1R,
    nullptr,
    convert_6u32s_C1R,
    nullptr,
    convert_8u32s_C1R
};


static void convert_32s1u_C1R(const int32_t* pSrc, uint8_t* pDst, size_t length)
{
    size_t i;
    for (i = 0; i < (length & ~(size_t)7U); i+=8U) {
        uint32_t src0 = (uint32_t)pSrc[i+0];
        uint32_t src1 = (uint32_t)pSrc[i+1];
        uint32_t src2 = (uint32_t)pSrc[i+2];
        uint32_t src3 = (uint32_t)pSrc[i+3];
        uint32_t src4 = (uint32_t)pSrc[i+4];
        uint32_t src5 = (uint32_t)pSrc[i+5];
        uint32_t src6 = (uint32_t)pSrc[i+6];
        uint32_t src7 = (uint32_t)pSrc[i+7];

        *pDst++ = (uint8_t)((src0 << 7) | (src1 << 6) | (src2 << 5) | (src3 << 4) | (src4 << 3) | (src5 << 2) | (src6 << 1) | src7);
    }

    if (length & 7U) {
        uint32_t src0 = (uint32_t)pSrc[i+0];
        uint32_t src1 = 0U;
        uint32_t src2 = 0U;
        uint32_t src3 = 0U;
        uint32_t src4 = 0U;
        uint32_t src5 = 0U;
        uint32_t src6 = 0U;
        length = length & 7U;

        if (length > 1U) {
            src1 = (uint32_t)pSrc[i+1];
            if (length > 2U) {
                src2 = (uint32_t)pSrc[i+2];
                if (length > 3U) {
                    src3 = (uint32_t)pSrc[i+3];
                    if (length > 4U) {
                        src4 = (uint32_t)pSrc[i+4];
                        if (length > 5U) {
                            src5 = (uint32_t)pSrc[i+5];
                            if (length > 6U) {
                                src6 = (uint32_t)pSrc[i+6];
                            }
                        }
                    }
                }
            }
        }
        *pDst++ = (uint8_t)((src0 << 7) | (src1 << 6) | (src2 << 5) | (src3 << 4) | (src4 << 3) | (src5 << 2) | (src6 << 1));
    }
}

static void convert_32s2u_C1R(const int32_t* pSrc, uint8_t* pDst, size_t length)
{
    size_t i;
    for (i = 0; i < (length & ~(size_t)3U); i+=4U) {
        uint32_t src0 = (uint32_t)pSrc[i+0];
        uint32_t src1 = (uint32_t)pSrc[i+1];
        uint32_t src2 = (uint32_t)pSrc[i+2];
        uint32_t src3 = (uint32_t)pSrc[i+3];

        *pDst++ = (uint8_t)((src0 << 6) | (src1 << 4) | (src2 << 2) | src3);
    }

    if (length & 3U) {
        uint32_t src0 = (uint32_t)pSrc[i+0];
        uint32_t src1 = 0U;
        uint32_t src2 = 0U;
        length = length & 3U;

        if (length > 1U) {
            src1 = (uint32_t)pSrc[i+1];
            if (length > 2U) {
                src2 = (uint32_t)pSrc[i+2];
            }
        }
        *pDst++ = (uint8_t)((src0 << 6) | (src1 << 4) | (src2 << 2));
    }
}

static void convert_32s4u_C1R(const int32_t* pSrc, uint8_t* pDst, size_t length)
{
    size_t i;
    for (i = 0; i < (length & ~(size_t)1U); i+=2U) {
        uint32_t src0 = (uint32_t)pSrc[i+0];
        uint32_t src1 = (uint32_t)pSrc[i+1];

        *pDst++ = (uint8_t)((src0 << 4) | src1);
    }

    if (length & 1U) {
        uint32_t src0 = (uint32_t)pSrc[i+0];
        *pDst++ = (uint8_t)((src0 << 4));
    }
}

static void convert_32s6u_C1R(const int32_t* pSrc, uint8_t* pDst, size_t length)
{
    size_t i;
    for (i = 0; i < (length & ~(size_t)3U); i+=4U) {
        uint32_t src0 = (uint32_t)pSrc[i+0];
        uint32_t src1 = (uint32_t)pSrc[i+1];
        uint32_t src2 = (uint32_t)pSrc[i+2];
        uint32_t src3 = (uint32_t)pSrc[i+3];

        *pDst++ = (uint8_t)((src0 << 2) | (src1 >> 4));
        *pDst++ = (uint8_t)(((src1 & 0xFU) << 4) | (src2 >> 2));
        *pDst++ = (uint8_t)(((src2 & 0x3U) << 6) | src3);
    }

    if (length & 3U) {
        uint32_t src0 = (uint32_t)pSrc[i+0];
        uint32_t src1 = 0U;
        uint32_t src2 = 0U;
        length = length & 3U;

        if (length > 1U) {
            src1 = (uint32_t)pSrc[i+1];
            if (length > 2U) {
                src2 = (uint32_t)pSrc[i+2];
            }
        }
        *pDst++ = (uint8_t)((src0 << 2) | (src1 >> 4));
        if (length > 1U) {
            *pDst++ = (uint8_t)(((src1 & 0xFU) << 4) | (src2 >> 2));
            if (length > 2U) {
                *pDst++ = (uint8_t)(((src2 & 0x3U) << 6));
            }
        }
    }
}
static void convert_32s8u_C1R(const int32_t* pSrc, uint8_t* pDst, size_t length)
{
    size_t i;
    for (i = 0; i < length; ++i) {
        pDst[i] = (uint8_t)pSrc[i];
    }
}
const convert_32sXXx_C1R convert_32sXXu_C1R_LUT[9] = {
    nullptr,
    convert_32s1u_C1R,
    convert_32s2u_C1R,
    nullptr,
    convert_32s4u_C1R,
    nullptr,
    convert_32s6u_C1R,
    nullptr,
    convert_32s8u_C1R
};

bool sanityCheckOnImage(opj_image_t* image, uint32_t numcomps) {
	if (numcomps == 0)
		return false;

	//check for null image components
	for (uint32_t i = 0; i < numcomps; ++i) {
		if (!image->comps[i].data) {
			fprintf(stderr, "[ERROR] null data for component %d",i);
			return false;
		}
	}

	// check that all components have same dimensions
	for (uint32_t i = 1; i < numcomps; ++i) {
		if (image->comps[i].w != image->comps[0].w ||
					image->comps[i].h != image->comps[0].h) {
			fprintf(stderr, "[ERROR] dimensions of component %d differ from dimensions of component 0", i);
			return false;
		}
	}
	return true;

}
