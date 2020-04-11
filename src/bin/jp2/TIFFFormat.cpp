/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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
 * Copyright (c) 2015, Matthieu Darbois
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

#include <cstdio>
#include <cstdlib>
#include "grk_apps_config.h"
#include "grok.h"
#include "TIFFFormat.h"
#include "convert.h"
#include <cstring>
#include "common.h"

#ifndef GROK_HAVE_LIBTIFF
# error GROK_HAVE_LIBTIFF_NOT_DEFINED
#endif /* GROK_HAVE_LIBTIFF */

#include <tiffio.h>
#include "color.h"
#include <cassert>
#include <memory>

static bool tiffWarningHandlerVerbose = true;
void MyTiffErrorHandler(const char *module, const char *fmt, va_list ap) {
	(void) module;
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
}

void MyTiffWarningHandler(const char *module, const char *fmt, va_list ap) {
	(void) module;
	if (tiffWarningHandlerVerbose) {
		vfprintf(stdout, fmt, ap);
		fprintf(stdout, "\n");
	}
}

void tiffSetErrorAndWarningHandlers(bool verbose) {
	tiffWarningHandlerVerbose = verbose;
	TIFFSetErrorHandler(MyTiffErrorHandler);
	TIFFSetWarningHandler(MyTiffWarningHandler);
}

static bool readTiffPixelsUnsigned(TIFF *tif, grk_image_comp *comps,
		uint32_t numcomps, uint16_t tiSpp, uint16_t tiPC, uint16_t tiPhoto);
static bool readTiffPixelsSigned(TIFF *tif, grk_image_comp *comps,
		uint32_t numcomps, uint16_t tiSpp, uint16_t tiPC, uint16_t tiPhoto);
/* -->> -->> -->> -->>

 TIFF IMAGE FORMAT

 <<-- <<-- <<-- <<-- */
#define PUTBITS2(s, nb) \
	trailing <<= remaining; \
	trailing |= (uint32_t)((s) >> (nb - remaining)); \
	*pDst++ = (uint8_t)trailing; \
	trailing = (uint32_t)((s) & ((1U << (nb - remaining)) - 1U)); \
	if (nb >= (remaining + 8)) { \
		*pDst++ = (uint8_t)(trailing >> (nb - (remaining + 8))); \
		trailing &= (uint32_t)((1U << (nb - (remaining + 8))) - 1U); \
		remaining += 16 - nb; \
	} else { \
		remaining += 8 - nb; \
	}

#define PUTBITS(s, nb) \
  if (nb >= remaining) { \
		PUTBITS2(s, nb) \
	} else { \
		trailing <<= nb; \
		trailing |= (uint32_t)(s); \
		remaining -= nb; \
	}
#define FLUSHBITS() \
	if (remaining != 8) { \
		trailing <<= remaining; \
		*pDst++ = (uint8_t)trailing; \
	}

#define GETBITS(dest, nb, mask, invert) { \
	int needed = (nb); \
	uint32_t dst = 0U; \
	if (available == 0) { \
		val = *pSrc++; \
		available = 8; \
	} \
	while (needed > available) { \
		dst |= val & ((1U << available) - 1U); \
		needed -= available; \
		dst <<= needed; \
		val = *pSrc++; \
		available = 8; \
	} \
	dst |= (val >> (available - needed)) & ((1U << needed) - 1U); \
	available -= needed; \
	dest = INV((int32_t)dst, mask,invert); \
}

static void tif_32sto3u(const int32_t *pSrc, uint8_t *pDst, size_t length) {
	size_t i;

	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		uint32_t src1 = (uint32_t) pSrc[i + 1];
		uint32_t src2 = (uint32_t) pSrc[i + 2];
		uint32_t src3 = (uint32_t) pSrc[i + 3];
		uint32_t src4 = (uint32_t) pSrc[i + 4];
		uint32_t src5 = (uint32_t) pSrc[i + 5];
		uint32_t src6 = (uint32_t) pSrc[i + 6];
		uint32_t src7 = (uint32_t) pSrc[i + 7];

		*pDst++ = (uint8_t) ((src0 << 5) | (src1 << 2) | (src2 >> 1));
		*pDst++ = (uint8_t) ((src2 << 7) | (src3 << 4) | (src4 << 1)
				| (src5 >> 2));
		*pDst++ = (uint8_t) ((src5 << 6) | (src6 << 3) | (src7));
	}

	if (length & 7U) {
		uint32_t trailing = 0U;
		int remaining = 8U;
		length &= 7U;
		PUTBITS((uint32_t )pSrc[i + 0], 3)
		if (length > 1U) {
			PUTBITS((uint32_t )pSrc[i + 1], 3)
			if (length > 2U) {
				PUTBITS((uint32_t )pSrc[i + 2], 3)
				if (length > 3U) {
					PUTBITS((uint32_t )pSrc[i + 3], 3)
					if (length > 4U) {
						PUTBITS((uint32_t )pSrc[i + 4], 3)
						if (length > 5U) {
							PUTBITS((uint32_t )pSrc[i + 5], 3)
							if (length > 6U) {
								PUTBITS((uint32_t )pSrc[i + 6], 3)
							}
						}
					}
				}
			}
		}
		FLUSHBITS()
	}
}

static void tif_32sto5u(const int32_t *pSrc, uint8_t *pDst, size_t length) {
	size_t i;

	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		uint32_t src1 = (uint32_t) pSrc[i + 1];
		uint32_t src2 = (uint32_t) pSrc[i + 2];
		uint32_t src3 = (uint32_t) pSrc[i + 3];
		uint32_t src4 = (uint32_t) pSrc[i + 4];
		uint32_t src5 = (uint32_t) pSrc[i + 5];
		uint32_t src6 = (uint32_t) pSrc[i + 6];
		uint32_t src7 = (uint32_t) pSrc[i + 7];

		*pDst++ = (uint8_t) ((src0 << 3) | (src1 >> 2));
		*pDst++ = (uint8_t) ((src1 << 6) | (src2 << 1) | (src3 >> 4));
		*pDst++ = (uint8_t) ((src3 << 4) | (src4 >> 1));
		*pDst++ = (uint8_t) ((src4 << 7) | (src5 << 2) | (src6 >> 3));
		*pDst++ = (uint8_t) ((src6 << 5) | (src7));

	}

	if (length & 7U) {
		uint32_t trailing = 0U;
		int remaining = 8U;
		length &= 7U;
		PUTBITS((uint32_t )pSrc[i + 0], 5)
		if (length > 1U) {
			PUTBITS((uint32_t )pSrc[i + 1], 5)
			if (length > 2U) {
				PUTBITS((uint32_t )pSrc[i + 2], 5)
				if (length > 3U) {
					PUTBITS((uint32_t )pSrc[i + 3], 5)
					if (length > 4U) {
						PUTBITS((uint32_t )pSrc[i + 4], 5)
						if (length > 5U) {
							PUTBITS((uint32_t )pSrc[i + 5], 5)
							if (length > 6U) {
								PUTBITS((uint32_t )pSrc[i + 6], 5)
							}
						}
					}
				}
			}
		}
		FLUSHBITS()
	}
}

static void tif_32sto7u(const int32_t *pSrc, uint8_t *pDst, size_t length) {
	size_t i;

	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		uint32_t src1 = (uint32_t) pSrc[i + 1];
		uint32_t src2 = (uint32_t) pSrc[i + 2];
		uint32_t src3 = (uint32_t) pSrc[i + 3];
		uint32_t src4 = (uint32_t) pSrc[i + 4];
		uint32_t src5 = (uint32_t) pSrc[i + 5];
		uint32_t src6 = (uint32_t) pSrc[i + 6];
		uint32_t src7 = (uint32_t) pSrc[i + 7];

		*pDst++ = (uint8_t) ((src0 << 1) | (src1 >> 6));
		*pDst++ = (uint8_t) ((src1 << 2) | (src2 >> 5));
		*pDst++ = (uint8_t) ((src2 << 3) | (src3 >> 4));
		*pDst++ = (uint8_t) ((src3 << 4) | (src4 >> 3));
		*pDst++ = (uint8_t) ((src4 << 5) | (src5 >> 2));
		*pDst++ = (uint8_t) ((src5 << 6) | (src6 >> 1));
		*pDst++ = (uint8_t) ((src6 << 7) | (src7));
	}

	if (length & 7U) {
		uint32_t trailing = 0U;
		int remaining = 8U;
		length &= 7U;
		PUTBITS((uint32_t )pSrc[i + 0], 7)
		if (length > 1U) {
			PUTBITS((uint32_t )pSrc[i + 1], 7)
			if (length > 2U) {
				PUTBITS((uint32_t )pSrc[i + 2], 7)
				if (length > 3U) {
					PUTBITS((uint32_t )pSrc[i + 3], 7)
					if (length > 4U) {
						PUTBITS((uint32_t )pSrc[i + 4], 7)
						if (length > 5U) {
							PUTBITS((uint32_t )pSrc[i + 5], 7)
							if (length > 6U) {
								PUTBITS((uint32_t )pSrc[i + 6], 7)
							}
						}
					}
				}
			}
		}
		FLUSHBITS()
	}
}

static void tif_32sto9u(const int32_t *pSrc, uint8_t *pDst, size_t length) {
	size_t i;

	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		uint32_t src1 = (uint32_t) pSrc[i + 1];
		uint32_t src2 = (uint32_t) pSrc[i + 2];
		uint32_t src3 = (uint32_t) pSrc[i + 3];
		uint32_t src4 = (uint32_t) pSrc[i + 4];
		uint32_t src5 = (uint32_t) pSrc[i + 5];
		uint32_t src6 = (uint32_t) pSrc[i + 6];
		uint32_t src7 = (uint32_t) pSrc[i + 7];

		*pDst++ = (uint8_t) ((src0 >> 1));
		*pDst++ = (uint8_t) ((src0 << 7) | (src1 >> 2));
		*pDst++ = (uint8_t) ((src1 << 6) | (src2 >> 3));
		*pDst++ = (uint8_t) ((src2 << 5) | (src3 >> 4));
		*pDst++ = (uint8_t) ((src3 << 4) | (src4 >> 5));
		*pDst++ = (uint8_t) ((src4 << 3) | (src5 >> 6));
		*pDst++ = (uint8_t) ((src5 << 2) | (src6 >> 7));
		*pDst++ = (uint8_t) ((src6 << 1) | (src7 >> 8));
		*pDst++ = (uint8_t) (src7);
	}

	if (length & 7U) {
		uint32_t trailing = 0U;
		int remaining = 8U;
		length &= 7U;
		PUTBITS2((uint32_t )pSrc[i + 0], 9)
		if (length > 1U) {
			PUTBITS2((uint32_t )pSrc[i + 1], 9)
			if (length > 2U) {
				PUTBITS2((uint32_t )pSrc[i + 2], 9)
				if (length > 3U) {
					PUTBITS2((uint32_t )pSrc[i + 3], 9)
					if (length > 4U) {
						PUTBITS2((uint32_t )pSrc[i + 4], 9)
						if (length > 5U) {
							PUTBITS2((uint32_t )pSrc[i + 5], 9)
							if (length > 6U) {
								PUTBITS2((uint32_t )pSrc[i + 6], 9)
							}
						}
					}
				}
			}
		}
		FLUSHBITS()
	}
}

static void tif_32sto10u(const int32_t *pSrc, uint8_t *pDst, size_t length) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 3U); i += 4U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		uint32_t src1 = (uint32_t) pSrc[i + 1];
		uint32_t src2 = (uint32_t) pSrc[i + 2];
		uint32_t src3 = (uint32_t) pSrc[i + 3];

		*pDst++ = (uint8_t) (src0 >> 2);
		*pDst++ = (uint8_t) (((src0 & 0x3U) << 6) | (src1 >> 4));
		*pDst++ = (uint8_t) (((src1 & 0xFU) << 4) | (src2 >> 6));
		*pDst++ = (uint8_t) (((src2 & 0x3FU) << 2) | (src3 >> 8));
		*pDst++ = (uint8_t) (src3);
	}

	if (length & 3U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		uint32_t src1 = 0U;
		uint32_t src2 = 0U;
		length = length & 3U;

		if (length > 1U) {
			src1 = (uint32_t) pSrc[i + 1];
			if (length > 2U) {
				src2 = (uint32_t) pSrc[i + 2];
			}
		}
		*pDst++ = (uint8_t) (src0 >> 2);
		*pDst++ = (uint8_t) (((src0 & 0x3U) << 6) | (src1 >> 4));
		if (length > 1U) {
			*pDst++ = (uint8_t) (((src1 & 0xFU) << 4) | (src2 >> 6));
			if (length > 2U) {
				*pDst++ = (uint8_t) (((src2 & 0x3FU) << 2));
			}
		}
	}
}

static void tif_32sto11u(const int32_t *pSrc, uint8_t *pDst, size_t length) {
	size_t i;

	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		uint32_t src1 = (uint32_t) pSrc[i + 1];
		uint32_t src2 = (uint32_t) pSrc[i + 2];
		uint32_t src3 = (uint32_t) pSrc[i + 3];
		uint32_t src4 = (uint32_t) pSrc[i + 4];
		uint32_t src5 = (uint32_t) pSrc[i + 5];
		uint32_t src6 = (uint32_t) pSrc[i + 6];
		uint32_t src7 = (uint32_t) pSrc[i + 7];

		*pDst++ = (uint8_t) ((src0 >> 3));
		*pDst++ = (uint8_t) ((src0 << 5) | (src1 >> 6));
		*pDst++ = (uint8_t) ((src1 << 2) | (src2 >> 9));
		*pDst++ = (uint8_t) ((src2 >> 1));
		*pDst++ = (uint8_t) ((src2 << 7) | (src3 >> 4));
		*pDst++ = (uint8_t) ((src3 << 4) | (src4 >> 7));
		*pDst++ = (uint8_t) ((src4 << 1) | (src5 >> 10));
		*pDst++ = (uint8_t) ((src5 >> 2));
		*pDst++ = (uint8_t) ((src5 << 6) | (src6 >> 5));
		*pDst++ = (uint8_t) ((src6 << 3) | (src7 >> 8));
		*pDst++ = (uint8_t) (src7);
	}

	if (length & 7U) {
		uint32_t trailing = 0U;
		int remaining = 8U;
		length &= 7U;
		PUTBITS2((uint32_t )pSrc[i + 0], 11)
		if (length > 1U) {
			PUTBITS2((uint32_t )pSrc[i + 1], 11)
			if (length > 2U) {
				PUTBITS2((uint32_t )pSrc[i + 2], 11)
				if (length > 3U) {
					PUTBITS2((uint32_t )pSrc[i + 3], 11)
					if (length > 4U) {
						PUTBITS2((uint32_t )pSrc[i + 4], 11)
						if (length > 5U) {
							PUTBITS2((uint32_t )pSrc[i + 5], 11)
							if (length > 6U) {
								PUTBITS2((uint32_t )pSrc[i + 6], 11)
							}
						}
					}
				}
			}
		}
		FLUSHBITS()
	}
}
static void tif_32sto12u(const int32_t *pSrc, uint8_t *pDst, size_t length) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 1U); i += 2U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		uint32_t src1 = (uint32_t) pSrc[i + 1];

		*pDst++ = (uint8_t) (src0 >> 4);
		*pDst++ = (uint8_t) (((src0 & 0xFU) << 4) | (src1 >> 8));
		*pDst++ = (uint8_t) (src1);
	}

	if (length & 1U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		*pDst++ = (uint8_t) (src0 >> 4);
		*pDst++ = (uint8_t) (((src0 & 0xFU) << 4));
	}
}

static void tif_32sto13u(const int32_t *pSrc, uint8_t *pDst, size_t length) {
	size_t i;

	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		uint32_t src1 = (uint32_t) pSrc[i + 1];
		uint32_t src2 = (uint32_t) pSrc[i + 2];
		uint32_t src3 = (uint32_t) pSrc[i + 3];
		uint32_t src4 = (uint32_t) pSrc[i + 4];
		uint32_t src5 = (uint32_t) pSrc[i + 5];
		uint32_t src6 = (uint32_t) pSrc[i + 6];
		uint32_t src7 = (uint32_t) pSrc[i + 7];

		*pDst++ = (uint8_t) ((src0 >> 5));
		*pDst++ = (uint8_t) ((src0 << 3) | (src1 >> 10));
		*pDst++ = (uint8_t) ((src1 >> 2));
		*pDst++ = (uint8_t) ((src1 << 6) | (src2 >> 7));
		*pDst++ = (uint8_t) ((src2 << 1) | (src3 >> 12));
		*pDst++ = (uint8_t) ((src3 >> 4));
		*pDst++ = (uint8_t) ((src3 << 4) | (src4 >> 9));
		*pDst++ = (uint8_t) ((src4 >> 1));
		*pDst++ = (uint8_t) ((src4 << 7) | (src5 >> 6));
		*pDst++ = (uint8_t) ((src5 << 2) | (src6 >> 11));
		*pDst++ = (uint8_t) ((src6 >> 3));
		*pDst++ = (uint8_t) ((src6 << 5) | (src7 >> 8));
		*pDst++ = (uint8_t) (src7);
	}

	if (length & 7U) {
		uint32_t trailing = 0U;
		int remaining = 8U;
		length &= 7U;
		PUTBITS2((uint32_t )pSrc[i + 0], 13)
		if (length > 1U) {
			PUTBITS2((uint32_t )pSrc[i + 1], 13)
			if (length > 2U) {
				PUTBITS2((uint32_t )pSrc[i + 2], 13)
				if (length > 3U) {
					PUTBITS2((uint32_t )pSrc[i + 3], 13)
					if (length > 4U) {
						PUTBITS2((uint32_t )pSrc[i + 4], 13)
						if (length > 5U) {
							PUTBITS2((uint32_t )pSrc[i + 5], 13)
							if (length > 6U) {
								PUTBITS2((uint32_t )pSrc[i + 6], 13)
							}
						}
					}
				}
			}
		}
		FLUSHBITS()
	}
}

static void tif_32sto14u(const int32_t *pSrc, uint8_t *pDst, size_t length) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 3U); i += 4U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		uint32_t src1 = (uint32_t) pSrc[i + 1];
		uint32_t src2 = (uint32_t) pSrc[i + 2];
		uint32_t src3 = (uint32_t) pSrc[i + 3];

		*pDst++ = (uint8_t) (src0 >> 6);
		*pDst++ = (uint8_t) (((src0 & 0x3FU) << 2) | (src1 >> 12));
		*pDst++ = (uint8_t) (src1 >> 4);
		*pDst++ = (uint8_t) (((src1 & 0xFU) << 4) | (src2 >> 10));
		*pDst++ = (uint8_t) (src2 >> 2);
		*pDst++ = (uint8_t) (((src2 & 0x3U) << 6) | (src3 >> 8));
		*pDst++ = (uint8_t) (src3);
	}

	if (length & 3U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		uint32_t src1 = 0U;
		uint32_t src2 = 0U;
		length = length & 3U;

		if (length > 1U) {
			src1 = (uint32_t) pSrc[i + 1];
			if (length > 2U) {
				src2 = (uint32_t) pSrc[i + 2];
			}
		}
		*pDst++ = (uint8_t) (src0 >> 6);
		*pDst++ = (uint8_t) (((src0 & 0x3FU) << 2) | (src1 >> 12));
		if (length > 1U) {
			*pDst++ = (uint8_t) (src1 >> 4);
			*pDst++ = (uint8_t) (((src1 & 0xFU) << 4) | (src2 >> 10));
			if (length > 2U) {
				*pDst++ = (uint8_t) (src2 >> 2);
				*pDst++ = (uint8_t) (((src2 & 0x3U) << 6));
			}
		}
	}
}

static void tif_32sto15u(const int32_t *pSrc, uint8_t *pDst, size_t length) {
	size_t i;

	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		uint32_t src1 = (uint32_t) pSrc[i + 1];
		uint32_t src2 = (uint32_t) pSrc[i + 2];
		uint32_t src3 = (uint32_t) pSrc[i + 3];
		uint32_t src4 = (uint32_t) pSrc[i + 4];
		uint32_t src5 = (uint32_t) pSrc[i + 5];
		uint32_t src6 = (uint32_t) pSrc[i + 6];
		uint32_t src7 = (uint32_t) pSrc[i + 7];

		*pDst++ = (uint8_t) ((src0 >> 7));
		*pDst++ = (uint8_t) ((src0 << 1) | (src1 >> 14));
		*pDst++ = (uint8_t) ((src1 >> 6));
		*pDst++ = (uint8_t) ((src1 << 2) | (src2 >> 13));
		*pDst++ = (uint8_t) ((src2 >> 5));
		*pDst++ = (uint8_t) ((src2 << 3) | (src3 >> 12));
		*pDst++ = (uint8_t) ((src3 >> 4));
		*pDst++ = (uint8_t) ((src3 << 4) | (src4 >> 11));
		*pDst++ = (uint8_t) ((src4 >> 3));
		*pDst++ = (uint8_t) ((src4 << 5) | (src5 >> 10));
		*pDst++ = (uint8_t) ((src5 >> 2));
		*pDst++ = (uint8_t) ((src5 << 6) | (src6 >> 9));
		*pDst++ = (uint8_t) ((src6 >> 1));
		*pDst++ = (uint8_t) ((src6 << 7) | (src7 >> 8));
		*pDst++ = (uint8_t) (src7);
	}

	if (length & 7U) {
		uint32_t trailing = 0U;
		int remaining = 8U;
		length &= 7U;
		PUTBITS2((uint32_t )pSrc[i + 0], 15)
		if (length > 1U) {
			PUTBITS2((uint32_t )pSrc[i + 1], 15)
			if (length > 2U) {
				PUTBITS2((uint32_t )pSrc[i + 2], 15)
				if (length > 3U) {
					PUTBITS2((uint32_t )pSrc[i + 3], 15)
					if (length > 4U) {
						PUTBITS2((uint32_t )pSrc[i + 4], 15)
						if (length > 5U) {
							PUTBITS2((uint32_t )pSrc[i + 5], 15)
							if (length > 6U) {
								PUTBITS2((uint32_t )pSrc[i + 6], 15)
							}
						}
					}
				}
			}
		}
		FLUSHBITS()
	}
}

static void tif_32sto16u(const int32_t *pSrc, uint16_t *pDst, size_t length) {
	size_t i;
	for (i = 0; i < length; ++i) {
		pDst[i] = (uint16_t) pSrc[i];
	}
}

static void tif_3uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 >> 5)), INV_MASK_3, invert);
		pDst[i + 1] = INV((int32_t )(((val0 & 0x1FU) >> 2)), INV_MASK_3,
				invert);
		pDst[i + 2] = INV((int32_t )(((val0 & 0x3U) << 1) | (val1 >> 7)),
				INV_MASK_3, invert);
		pDst[i + 3] = INV((int32_t )(((val1 & 0x7FU) >> 4)), INV_MASK_3,
				invert);
		pDst[i + 4] = INV((int32_t )(((val1 & 0xFU) >> 1)), INV_MASK_3, invert);
		pDst[i + 5] = INV((int32_t )(((val1 & 0x1U) << 2) | (val2 >> 6)),
				INV_MASK_3, invert);
		pDst[i + 6] = INV((int32_t )(((val2 & 0x3FU) >> 3)), INV_MASK_3,
				invert);
		pDst[i + 7] = INV((int32_t )(((val2 & 0x7U))), INV_MASK_3, invert);

	}
	if (length & 7U) {
		uint32_t val;
		int available = 0;

		length = length & 7U;

		GETBITS(pDst[i + 0], 3, INV_MASK_3, invert)

		if (length > 1U) {
			GETBITS(pDst[i + 1], 3, INV_MASK_3, invert)
			if (length > 2U) {
				GETBITS(pDst[i + 2], 3, INV_MASK_3, invert)
				if (length > 3U) {
					GETBITS(pDst[i + 3], 3, INV_MASK_3, invert)
					if (length > 4U) {
						GETBITS(pDst[i + 4], 3, INV_MASK_3, invert)
						if (length > 5U) {
							GETBITS(pDst[i + 5], 3, INV_MASK_3, invert)
							if (length > 6U) {
								GETBITS(pDst[i + 6], 3, INV_MASK_3, invert)
							}
						}
					}
				}
			}
		}
	}
}
static void tif_5uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 >> 3)), INV_MASK_5, invert);
		pDst[i + 1] = INV((int32_t )(((val0 & 0x7U) << 2) | (val1 >> 6)),
				INV_MASK_5, invert);
		pDst[i + 2] = INV((int32_t )(((val1 & 0x3FU) >> 1)), INV_MASK_5,
				invert);
		pDst[i + 3] = INV((int32_t )(((val1 & 0x1U) << 4) | (val2 >> 4)),
				INV_MASK_5, invert);
		pDst[i + 4] = INV((int32_t )(((val2 & 0xFU) << 1) | (val3 >> 7)),
				INV_MASK_5, invert);
		pDst[i + 5] = INV((int32_t )(((val3 & 0x7FU) >> 2)), INV_MASK_5,
				invert);
		pDst[i + 6] = INV((int32_t )(((val3 & 0x3U) << 3) | (val4 >> 5)),
				INV_MASK_5, invert);
		pDst[i + 7] = INV((int32_t )(((val4 & 0x1FU))), INV_MASK_5, invert);

	}
	if (length & 7U) {
		uint32_t val;
		int available = 0;

		length = length & 7U;

		GETBITS(pDst[i + 0], 5, INV_MASK_5, invert)

		if (length > 1U) {
			GETBITS(pDst[i + 1], 5, INV_MASK_5, invert)
			if (length > 2U) {
				GETBITS(pDst[i + 2], 5, INV_MASK_5, invert)
				if (length > 3U) {
					GETBITS(pDst[i + 3], 5, INV_MASK_5, invert)
					if (length > 4U) {
						GETBITS(pDst[i + 4], 5, INV_MASK_5, invert)
						if (length > 5U) {
							GETBITS(pDst[i + 5], 5, INV_MASK_5, invert)
							if (length > 6U) {
								GETBITS(pDst[i + 6], 5, INV_MASK_5, invert)
							}
						}
					}
				}
			}
		}
	}
}
static void tif_7uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;
		uint32_t val5 = *pSrc++;
		uint32_t val6 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 >> 1)), INV_MASK_7, invert);
		pDst[i + 1] = INV((int32_t )(((val0 & 0x1U) << 6) | (val1 >> 2)),
				INV_MASK_7, invert);
		pDst[i + 2] = INV((int32_t )(((val1 & 0x3U) << 5) | (val2 >> 3)),
				INV_MASK_7, invert);
		pDst[i + 3] = INV((int32_t )(((val2 & 0x7U) << 4) | (val3 >> 4)),
				INV_MASK_7, invert);
		pDst[i + 4] = INV((int32_t )(((val3 & 0xFU) << 3) | (val4 >> 5)),
				INV_MASK_7, invert);
		pDst[i + 5] = INV((int32_t )(((val4 & 0x1FU) << 2) | (val5 >> 6)),
				INV_MASK_7, invert);
		pDst[i + 6] = INV((int32_t )(((val5 & 0x3FU) << 1) | (val6 >> 7)),
				INV_MASK_7, invert);
		pDst[i + 7] = INV((int32_t )(((val6 & 0x7FU))), INV_MASK_7, invert);

	}
	if (length & 7U) {
		uint32_t val;
		int available = 0;

		length = length & 7U;

		GETBITS(pDst[i + 0], 7, INV_MASK_7, invert)

		if (length > 1U) {
			GETBITS(pDst[i + 1], 7, INV_MASK_7, invert)
			if (length > 2U) {
				GETBITS(pDst[i + 2], 7, INV_MASK_7, invert)
				if (length > 3U) {
					GETBITS(pDst[i + 3], 7, INV_MASK_7, invert)
					if (length > 4U) {
						GETBITS(pDst[i + 4], 7, INV_MASK_7, invert)
						if (length > 5U) {
							GETBITS(pDst[i + 5], 7, INV_MASK_7, invert)
							if (length > 6U) {
								GETBITS(pDst[i + 6], 7, INV_MASK_7, invert)
							}
						}
					}
				}
			}
		}
	}
}
static void tif_9uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;
		uint32_t val5 = *pSrc++;
		uint32_t val6 = *pSrc++;
		uint32_t val7 = *pSrc++;
		uint32_t val8 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 << 1) | (val1 >> 7)), INV_MASK_9,
				invert);
		pDst[i + 1] = INV((int32_t )(((val1 & 0x7FU) << 2) | (val2 >> 6)),
				INV_MASK_9, invert);
		pDst[i + 2] = INV((int32_t )(((val2 & 0x3FU) << 3) | (val3 >> 5)),
				INV_MASK_9, invert);
		pDst[i + 3] = INV((int32_t )(((val3 & 0x1FU) << 4) | (val4 >> 4)),
				INV_MASK_9, invert);
		pDst[i + 4] = INV((int32_t )(((val4 & 0xFU) << 5) | (val5 >> 3)),
				INV_MASK_9, invert);
		pDst[i + 5] = INV((int32_t )(((val5 & 0x7U) << 6) | (val6 >> 2)),
				INV_MASK_9, invert);
		pDst[i + 6] = INV((int32_t )(((val6 & 0x3U) << 7) | (val7 >> 1)),
				INV_MASK_9, invert);
		pDst[i + 7] = INV((int32_t )(((val7 & 0x1U) << 8) | (val8)), INV_MASK_9,
				invert);

	}
	if (length & 7U) {
		uint32_t val;
		int available = 0;

		length = length & 7U;

		GETBITS(pDst[i + 0], 9, INV_MASK_9, invert)

		if (length > 1U) {
			GETBITS(pDst[i + 1], 9, INV_MASK_9, invert)
			if (length > 2U) {
				GETBITS(pDst[i + 2], 9, INV_MASK_9, invert)
				if (length > 3U) {
					GETBITS(pDst[i + 3], 9, INV_MASK_9, invert)
					if (length > 4U) {
						GETBITS(pDst[i + 4], 9, INV_MASK_9, invert)
						if (length > 5U) {
							GETBITS(pDst[i + 5], 9, INV_MASK_9, invert)
							if (length > 6U) {
								GETBITS(pDst[i + 6], 9, INV_MASK_9, invert)
							}
						}
					}
				}
			}
		}
	}
}

static void tif_10uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 3U); i += 4U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 << 2) | (val1 >> 6)), INV_MASK_10,
				invert);
		pDst[i + 1] = INV((int32_t )(((val1 & 0x3FU) << 4) | (val2 >> 4)),
				INV_MASK_10, invert);
		pDst[i + 2] = INV((int32_t )(((val2 & 0xFU) << 6) | (val3 >> 2)),
				INV_MASK_10, invert);
		pDst[i + 3] = INV((int32_t )(((val3 & 0x3U) << 8) | val4), INV_MASK_10,
				invert);

	}
	if (length & 3U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		length = length & 3U;
		pDst[i + 0] = INV((int32_t )((val0 << 2) | (val1 >> 6)), INV_MASK_10,
				invert);

		if (length > 1U) {
			uint32_t val2 = *pSrc++;
			pDst[i + 1] = INV((int32_t )(((val1 & 0x3FU) << 4) | (val2 >> 4)),
					INV_MASK_10, invert);
			if (length > 2U) {
				uint32_t val3 = *pSrc++;
				pDst[i + 2] = INV(
						(int32_t )(((val2 & 0xFU) << 6) | (val3 >> 2)),
						INV_MASK_10, invert);
			}
		}
	}
}

static void tif_11uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;
		uint32_t val5 = *pSrc++;
		uint32_t val6 = *pSrc++;
		uint32_t val7 = *pSrc++;
		uint32_t val8 = *pSrc++;
		uint32_t val9 = *pSrc++;
		uint32_t val10 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 << 3) | (val1 >> 5)), INV_MASK_11,
				invert);
		pDst[i + 1] = INV((int32_t )(((val1 & 0x1FU) << 6) | (val2 >> 2)),
				INV_MASK_11, invert);
		pDst[i + 2] = INV(
				(int32_t )(((val2 & 0x3U) << 9) | (val3 << 1) | (val4 >> 7)),
				INV_MASK_11, invert);
		pDst[i + 3] = INV((int32_t )(((val4 & 0x7FU) << 4) | (val5 >> 4)),
				INV_MASK_11, invert);
		pDst[i + 4] = INV((int32_t )(((val5 & 0xFU) << 7) | (val6 >> 1)),
				INV_MASK_11, invert);
		pDst[i + 5] = INV(
				(int32_t )(((val6 & 0x1U) << 10) | (val7 << 2) | (val8 >> 6)),
				INV_MASK_11, invert);
		pDst[i + 6] = INV((int32_t )(((val8 & 0x3FU) << 5) | (val9 >> 3)),
				INV_MASK_11, invert);
		pDst[i + 7] = INV((int32_t )(((val9 & 0x7U) << 8) | (val10)),
				INV_MASK_11, invert);

	}
	if (length & 7U) {
		uint32_t val;
		int available = 0;

		length = length & 7U;

		GETBITS(pDst[i + 0], 11, INV_MASK_11, invert)

		if (length > 1U) {
			GETBITS(pDst[i + 1], 11, INV_MASK_11, invert)
			if (length > 2U) {
				GETBITS(pDst[i + 2], 11, INV_MASK_11, invert)
				if (length > 3U) {
					GETBITS(pDst[i + 3], 11, INV_MASK_11, invert)
					if (length > 4U) {
						GETBITS(pDst[i + 4], 11, INV_MASK_11, invert)
						if (length > 5U) {
							GETBITS(pDst[i + 5], 11, INV_MASK_11, invert)
							if (length > 6U) {
								GETBITS(pDst[i + 6], 11, INV_MASK_11, invert)
							}
						}
					}
				}
			}
		}
	}
}
static void tif_12uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 1U); i += 2U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 << 4) | (val1 >> 4)), INV_MASK_12,
				invert);
		pDst[i + 1] = INV((int32_t )(((val1 & 0xFU) << 8) | val2), INV_MASK_12,
				invert);
	}
	if (length & 1U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		pDst[i + 0] = INV((int32_t )((val0 << 4) | (val1 >> 4)), INV_MASK_12,
				invert);
	}
}

static void tif_13uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;
		uint32_t val5 = *pSrc++;
		uint32_t val6 = *pSrc++;
		uint32_t val7 = *pSrc++;
		uint32_t val8 = *pSrc++;
		uint32_t val9 = *pSrc++;
		uint32_t val10 = *pSrc++;
		uint32_t val11 = *pSrc++;
		uint32_t val12 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 << 5) | (val1 >> 3)), INV_MASK_13,
				invert);
		pDst[i + 1] = INV(
				(int32_t )(((val1 & 0x7U) << 10) | (val2 << 2) | (val3 >> 6)),
				INV_MASK_13, invert);
		pDst[i + 2] = INV((int32_t )(((val3 & 0x3FU) << 7) | (val4 >> 1)),
				INV_MASK_13, invert);
		pDst[i + 3] = INV(
				(int32_t )(((val4 & 0x1U) << 12) | (val5 << 4) | (val6 >> 4)),
				INV_MASK_13, invert);
		pDst[i + 4] = INV(
				(int32_t )(((val6 & 0xFU) << 9) | (val7 << 1) | (val8 >> 7)),
				INV_MASK_13, invert);
		pDst[i + 5] = INV((int32_t )(((val8 & 0x7FU) << 6) | (val9 >> 2)),
				INV_MASK_13, invert);
		pDst[i + 6] = INV(
				(int32_t )(((val9 & 0x3U) << 11) | (val10 << 3) | (val11 >> 5)),
				INV_MASK_13, invert);
		pDst[i + 7] = INV((int32_t )(((val11 & 0x1FU) << 8) | (val12)),
				INV_MASK_13, invert);

	}
	if (length & 7U) {
		uint32_t val;
		int available = 0;

		length = length & 7U;

		GETBITS(pDst[i + 0], 13, INV_MASK_13, invert)

		if (length > 1U) {
			GETBITS(pDst[i + 1], 13, INV_MASK_13, invert)
			if (length > 2U) {
				GETBITS(pDst[i + 2], 13, INV_MASK_13, invert)
				if (length > 3U) {
					GETBITS(pDst[i + 3], 13, INV_MASK_13, invert)
					if (length > 4U) {
						GETBITS(pDst[i + 4], 13, INV_MASK_13, invert)
						if (length > 5U) {
							GETBITS(pDst[i + 5], 13, INV_MASK_13, invert)
							if (length > 6U) {
								GETBITS(pDst[i + 6], 13, INV_MASK_13, invert)
							}
						}
					}
				}
			}
		}
	}
}

static void tif_14uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 3U); i += 4U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;
		uint32_t val5 = *pSrc++;
		uint32_t val6 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 << 6) | (val1 >> 2)), INV_MASK_14,
				invert);
		pDst[i + 1] = INV(
				(int32_t )(((val1 & 0x3U) << 12) | (val2 << 4) | (val3 >> 4)),
				INV_MASK_14, invert);
		pDst[i + 2] = INV(
				(int32_t )(((val3 & 0xFU) << 10) | (val4 << 2) | (val5 >> 6)),
				INV_MASK_14, invert);
		pDst[i + 3] = INV((int32_t )(((val5 & 0x3FU) << 8) | val6), INV_MASK_14,
				invert);

	}
	if (length & 3U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		length = length & 3U;
		pDst[i + 0] = (int32_t) ((val0 << 6) | (val1 >> 2));

		if (length > 1U) {
			uint32_t val2 = *pSrc++;
			uint32_t val3 = *pSrc++;
			pDst[i + 1] =
					INV(
							(int32_t )(((val1 & 0x3U) << 12) | (val2 << 4)
									| (val3 >> 4)), INV_MASK_14, invert);
			if (length > 2U) {
				uint32_t val4 = *pSrc++;
				uint32_t val5 = *pSrc++;
				pDst[i + 2] = INV(
						(int32_t )(((val3 & 0xFU) << 10) | (val4 << 2)
								| (val5 >> 6)), INV_MASK_14, invert);
			}
		}
	}
}

static void tif_15uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;
		uint32_t val5 = *pSrc++;
		uint32_t val6 = *pSrc++;
		uint32_t val7 = *pSrc++;
		uint32_t val8 = *pSrc++;
		uint32_t val9 = *pSrc++;
		uint32_t val10 = *pSrc++;
		uint32_t val11 = *pSrc++;
		uint32_t val12 = *pSrc++;
		uint32_t val13 = *pSrc++;
		uint32_t val14 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 << 7) | (val1 >> 1)), (1 << 15) - 1,
				invert);
		pDst[i + 1] = INV(
				(int32_t )(((val1 & 0x1U) << 14) | (val2 << 6) | (val3 >> 2)),
				INV_MASK_15, invert);
		pDst[i + 2] = INV(
				(int32_t )(((val3 & 0x3U) << 13) | (val4 << 5) | (val5 >> 3)),
				INV_MASK_15, invert);
		pDst[i + 3] = INV(
				(int32_t )(((val5 & 0x7U) << 12) | (val6 << 4) | (val7 >> 4)),
				INV_MASK_15, invert);
		pDst[i + 4] = INV(
				(int32_t )(((val7 & 0xFU) << 11) | (val8 << 3) | (val9 >> 5)),
				INV_MASK_15, invert);
		pDst[i + 5] =
				INV(
						(int32_t )(((val9 & 0x1FU) << 10) | (val10 << 2)
								| (val11 >> 6)), INV_MASK_15, invert);
		pDst[i + 6] =
				INV(
						(int32_t )(((val11 & 0x3FU) << 9) | (val12 << 1)
								| (val13 >> 7)), INV_MASK_15, invert);
		pDst[i + 7] = INV((int32_t )(((val13 & 0x7FU) << 8) | (val14)),
				INV_MASK_15, invert);

	}
	if (length & 7U) {
		uint32_t val;
		int available = 0;

		length = length & 7U;

		GETBITS(pDst[i + 0], 15, INV_MASK_15, invert)

		if (length > 1U) {
			GETBITS(pDst[i + 1], 15, INV_MASK_15, invert)
			if (length > 2U) {
				GETBITS(pDst[i + 2], 15, INV_MASK_15, invert)
				if (length > 3U) {
					GETBITS(pDst[i + 3], 15, INV_MASK_15, invert)
					if (length > 4U) {
						GETBITS(pDst[i + 4], 15, INV_MASK_15, invert)
						if (length > 5U) {
							GETBITS(pDst[i + 5], 15, INV_MASK_15, invert)
							if (length > 6U) {
								GETBITS(pDst[i + 6], 15, INV_MASK_15, invert)
							}
						}
					}
				}
			}
		}
	}
}

/* seems that libtiff decodes this to machine endianness */
static void tif_16uto32s(const uint16_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < length; i++) {
		pDst[i] = INV(pSrc[i], 0xFFFF, invert);
	}
}

static void set_resolution(double *res, float resx, float resy, short resUnit) {
	// resolution is in pels / metre
	res[0] = resx;
	res[1] = resy;

	switch (resUnit) {
	case RESUNIT_INCH:
		//2.54 cm / inch
		res[0] *= 100 / 2.54;
		res[1] *= 100 / 2.54;
		break;
		// cm
	case RESUNIT_CENTIMETER:
		res[0] *= 100;
		res[1] *= 100;
		break;
	default:
		break;
	}
}

/*
 * libtiff/tif_getimage.c : 1,2,4,8,16 bitspersample accepted
 * CINEMA                 : 12 bit precision
 */
static grk_image* tiftoimage(const char *filename,
		grk_cparameters *parameters) {
	TIFF *tif;
	uint32_t subsampling_dx = parameters->subsampling_dx;
	uint32_t subsampling_dy = parameters->subsampling_dy;
	GRK_COLOR_SPACE color_space = GRK_CLRSPC_UNKNOWN;
	grk_image_cmptparm cmptparm[4];
	grk_image *image = nullptr;
	uint16_t tiBps = 0, tiPhoto = 0, tiSf = SAMPLEFORMAT_UINT, tiSpp = 0, tiPC =
			0;
	bool hasTiSf = false;
	short tiResUnit = 0;
	float tiXRes = 0, tiYRes = 0;
	uint32_t tiWidth = 0, tiHeight = 0;
	bool is_cinema = GRK_IS_CINEMA(parameters->rsiz);
	bool success = true;
	bool isCIE = false;

	tif = TIFFOpen(filename, "r");
	if (!tif) {
		spdlog::error("tiftoimage:Failed to open {} for reading\n", filename);
		return 0;
	}

	TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &tiWidth);
	TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &tiHeight);
	TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &tiBps);
	TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &tiSpp);
	TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &tiPhoto);
	TIFFGetField(tif, TIFFTAG_PLANARCONFIG, &tiPC);
	hasTiSf = TIFFGetField(tif, TIFFTAG_SAMPLEFORMAT, &tiSf) == 1;

	uint32_t w = tiWidth;
	uint32_t h = tiHeight;
	uint32_t numcomps = 0;
	uint32_t icclen = 0;
	uint8_t *iccbuf = nullptr;
	uint8_t *iptc_buf = nullptr;
	uint32_t iptc_len = 0;
	uint8_t *xmp_buf = nullptr;
	uint32_t xmp_len = 0;
	uint16 *sampleinfo = nullptr;
	uint16 extrasamples = 0;
	bool hasXRes = false, hasYRes = false, hasResUnit = false;
	bool isSigned = (tiSf == SAMPLEFORMAT_INT);

	// 1. sanity checks
	if (hasTiSf && tiSf != SAMPLEFORMAT_UINT && tiSf != SAMPLEFORMAT_INT) {
		spdlog::error("tiftoimage: Unsupported sample format {}\n"
				"\tAborting.\n", tiSf);
		success = false;
		goto cleanup;
	}
	if (tiSpp == 0 || tiSpp > 4) { /* should be 1 ... 4 */
		spdlog::error("tiftoimage: Bad value for samples per pixel == %hu.\n"
				"\tAborting.\n", tiSpp);
		success = false;
		goto cleanup;
	}
	if (tiBps > 16U || tiBps == 0) {
		spdlog::error("tiftoimage: Bad values for Bits == {}.\n"
				"\tMax. 16 Bits are allowed here.\n\tAborting.\n", tiBps);
		success = false;
		goto cleanup;
	}
	if (tiPhoto != PHOTOMETRIC_MINISBLACK && tiPhoto != PHOTOMETRIC_MINISWHITE
			&& tiPhoto != PHOTOMETRIC_RGB && tiPhoto != PHOTOMETRIC_ICCLAB
			&& tiPhoto != PHOTOMETRIC_CIELAB
			&& tiPhoto != PHOTOMETRIC_YCBCR
			&& tiPhoto != PHOTOMETRIC_SEPARATED) {
		spdlog::error("tiftoimage: Unsupported color format {}.\n"
				"\tOnly RGB(A), GRAY(A), CIELAB, YCC and CMKYK have been implemented",
				(int) tiPhoto);
		spdlog::error("\tAborting");
		success = false;
		goto cleanup;
	}
	if (tiWidth == 0 || tiHeight == 0) {
		spdlog::error("tiftoimage: Bad values for width(%u) "
				"and/or height(%u)\n\tAborting.\n", tiWidth, tiHeight);
		success = false;
		goto cleanup;

	}
	TIFFGetFieldDefaulted(tif, TIFFTAG_EXTRASAMPLES, &extrasamples,
			&sampleinfo);

	// 2. initialize image components and signed/unsigned
	memset(&cmptparm[0], 0, 4 * sizeof(grk_image_cmptparm));
	if ((tiPhoto == PHOTOMETRIC_RGB) && (is_cinema) && (tiBps != 12U)) {
		if (parameters->verbose)
			spdlog::warn("Input image bitdepth is {} bits\n"
					"TIF conversion has automatically rescaled to 12-bits\n"
					"to comply with cinema profiles.\n", tiBps);
	} else {
		is_cinema = 0U;
	}
	numcomps = extrasamples;
	switch (tiPhoto) {
	case PHOTOMETRIC_RGB:
		color_space = GRK_CLRSPC_SRGB;
		numcomps += 3;
		break;
	case PHOTOMETRIC_MINISBLACK:
	case PHOTOMETRIC_MINISWHITE:
		color_space = GRK_CLRSPC_GRAY;
		numcomps++;
		break;
	case PHOTOMETRIC_CIELAB:
	case PHOTOMETRIC_ICCLAB:
		isCIE = true;
		color_space = GRK_CLRSPC_DEFAULT_CIE;
		numcomps += 3;
		if (tiSpp != 3) {
			if (parameters->verbose)
				spdlog::warn(
						"Input image is in CIE colour space but samples per pixel = {}\n",
						tiSpp);
		}
		break;
	case PHOTOMETRIC_YCBCR:
		color_space = GRK_CLRSPC_SYCC;
		numcomps += 3;
		break;
	case PHOTOMETRIC_SEPARATED:
		color_space = GRK_CLRSPC_CMYK;
		numcomps += 4;
		break;
	default:
		break;
	}
	if (numcomps == 0) {
		success = false;
		goto cleanup;
	}

	if (tiPhoto == PHOTOMETRIC_CIELAB) {
		if (hasTiSf && (tiSf != SAMPLEFORMAT_INT)) {
			if (parameters->verbose)
				spdlog::warn(
						"Input image is in CIE colour space but sample format is unsigned int");
		}
		isSigned = true;
	} else if (tiPhoto == PHOTOMETRIC_ICCLAB) {
		if (hasTiSf && (tiSf != SAMPLEFORMAT_UINT)) {
			if (parameters->verbose)
				spdlog::warn(
						"Input image is in ICC CIE colour space but sample format is signed int");
		}
		isSigned = false;
	}

	if (isSigned) {
		if (PHOTOMETRIC_MINISWHITE || tiBps != 8) {
			spdlog::error(
					"tiftoimage: only non-inverted 8-bit signed images are supported");
			success = false;
			goto cleanup;
		}
	}

	// 4. create image
	for (uint32_t j = 0; j < numcomps; j++) {
		cmptparm[j].prec = tiBps;
		cmptparm[j].dx = subsampling_dx;
		cmptparm[j].dy = subsampling_dy;
		cmptparm[j].w = w;
		cmptparm[j].h = h;
	}
	image = grk_image_create(numcomps, &cmptparm[0], color_space);
	if (!image) {
		success = false;
		goto cleanup;
	}
	/* set image offset and reference grid */
	image->x0 = parameters->image_offset_x0;
	image->x1 =
			!image->x0 ?
					(w - 1) * subsampling_dx + 1 :
					image->x0 + (w - 1) * subsampling_dx + 1;
	if (image->x1 <= image->x0) {
		spdlog::error("tiftoimage: Bad value for image->x1({}) vs. "
				"image->x0({})\n\tAborting.\n", image->x1, image->x0);
		success = false;
		goto cleanup;
	}
	image->y0 = parameters->image_offset_y0;
	image->y1 =
			!image->y0 ?
					(h - 1) * subsampling_dy + 1 :
					image->y0 + (h - 1) * subsampling_dy + 1;
	if (image->y1 <= image->y0) {
		spdlog::error("tiftoimage: Bad value for image->y1({}) vs. "
				"image->y0({})\n\tAborting.\n", image->y1, image->y0);
		success = false;
		goto cleanup;
	}
	for (uint32_t j = 0; j < numcomps; j++) {
		// handle alpha channel
		auto numColourChannels = numcomps - extrasamples;
		if (extrasamples > 0 && j >= numColourChannels) {
			auto alphaType = sampleinfo[j - numColourChannels];
			if (alphaType == EXTRASAMPLE_ASSOCALPHA)
				image->comps[j].alpha =
						GROK_COMPONENT_TYPE_PREMULTIPLIED_OPACITY;
			else if (alphaType == EXTRASAMPLE_UNASSALPHA)
				image->comps[j].alpha = GROK_COMPONENT_TYPE_OPACITY;
			else {
				// some older mono or RGB images may have alpha channel stored as EXTRASAMPLE_UNSPECIFIED
				if (numcomps == 2 || numcomps == 4) {
					image->comps[j].alpha = GROK_COMPONENT_TYPE_OPACITY;
				}
			}
		}
		image->comps[j].sgnd = isSigned ? 1 : 0;
	}

	// 5. extract capture resolution
	hasXRes = TIFFGetField(tif, TIFFTAG_XRESOLUTION, &tiXRes) == 1;
	hasYRes = TIFFGetField(tif, TIFFTAG_YRESOLUTION, &tiYRes) == 1;
	hasResUnit = TIFFGetField(tif, TIFFTAG_RESOLUTIONUNIT, &tiResUnit) == 1;
	if (hasXRes && hasYRes && hasResUnit && tiResUnit != RESUNIT_NONE) {
		set_resolution(parameters->capture_resolution_from_file, tiXRes, tiYRes,
				tiResUnit);
		parameters->write_capture_resolution_from_file = true;
		image->capture_resolution[0] = tiXRes;
		image->capture_resolution[1] = tiYRes;
	}
	// 6. extract embedded ICC profile (with sanity check on binary size of profile)
	// note: we ignore ICC profile for CIE images as JPEG 2000 can't signal both
	// CIE and ICC
	if (!isCIE) {
		if ((TIFFGetField(tif, TIFFTAG_ICCPROFILE, &icclen, &iccbuf) == 1)
				&& icclen > 0 && icclen < grk::maxICCProfileBufferLen) {
			image->icc_profile_buf = grk_buffer_new(icclen);
			memcpy(image->icc_profile_buf, iccbuf, icclen);
			image->icc_profile_len = icclen;
			image->color_space = GRK_CLRSPC_ICC;
		}
	}
	// 7. extract IPTC meta-data
	if (TIFFGetField(tif, TIFFTAG_RICHTIFFIPTC, &iptc_len, &iptc_buf) == 1) {
		if (TIFFIsByteSwapped(tif))
			TIFFSwabArrayOfLong((uint32*) iptc_buf, iptc_len);
		// since TIFFTAG_RICHTIFFIPTC is of type TIFF_LONG, we must multiply
		// by 4 to get the length in bytes
		image->iptc_len = iptc_len * 4;
		image->iptc_buf = grk_buffer_new(iptc_len);
		memcpy(image->iptc_buf, iptc_buf, iptc_len);
	}
	// 8. extract XML meta-data
	if (TIFFGetField(tif, TIFFTAG_XMLPACKET, &xmp_len, &xmp_buf) == 1) {
		image->xmp_len = xmp_len;
		image->xmp_buf = grk_buffer_new(xmp_len);
		memcpy(image->xmp_buf, xmp_buf, xmp_len);
	}

	// 9. read pixel data
	if (isSigned)
		success = success
				&& readTiffPixelsSigned(tif, image->comps, numcomps, tiSpp,
						tiPC, tiPhoto);
	else
		success = success
				&& readTiffPixelsUnsigned(tif, image->comps, numcomps, tiSpp,
						tiPC, tiPhoto);

	cleanup: if (tif)
		TIFFClose(tif);
	if (success) {
		if (is_cinema) {
			for (uint32_t j = 0; j < numcomps; ++j) {
				scale_component(&(image->comps[j]), 12);
			}
		}
		return image;
	}
	if (image)
		grk_image_destroy(image);

	return nullptr;
}/* tiftoimage() */

static bool readTiffPixelsUnsigned(TIFF *tif, grk_image_comp *comps,
		uint32_t numcomps, uint16_t tiSpp, uint16_t tiPC, uint16_t tiPhoto) {
	if (!tif)
		return false;

	bool success = true;
	convert_XXx32s_C1R cvtTifTo32s = nullptr;
	convert_32s_CXPX cvtCxToPx = nullptr;
	int32_t *planes[4];
	tsize_t rowStride;
	bool invert;
	tdata_t buf = nullptr;
	tstrip_t strip;
	tsize_t strip_size;
	uint32_t currentPlane = 0;
	int32_t *buffer32s = nullptr;

	switch (comps[0].prec) {
	case 1:
	case 2:
	case 4:
	case 6:
	case 8:
		cvtTifTo32s = convert_XXu32s_C1R_LUT[comps[0].prec];
		break;
		/* others are specific to TIFF */
	case 3:
		cvtTifTo32s = tif_3uto32s;
		break;
	case 5:
		cvtTifTo32s = tif_5uto32s;
		break;
	case 7:
		cvtTifTo32s = tif_7uto32s;
		break;
	case 9:
		cvtTifTo32s = tif_9uto32s;
		break;
	case 10:
		cvtTifTo32s = tif_10uto32s;
		break;
	case 11:
		cvtTifTo32s = tif_11uto32s;
		break;
	case 12:
		cvtTifTo32s = tif_12uto32s;
		break;
	case 13:
		cvtTifTo32s = tif_13uto32s;
		break;
	case 14:
		cvtTifTo32s = tif_14uto32s;
		break;
	case 15:
		cvtTifTo32s = tif_15uto32s;
		break;
	case 16:
		cvtTifTo32s = (convert_XXx32s_C1R) tif_16uto32s;
		break;
	default:
		/* never here */
		break;
	}
	cvtCxToPx = convert_32s_CXPX_LUT[numcomps];
	if (tiPC == PLANARCONFIG_SEPARATE) {
		cvtCxToPx = convert_32s_CXPX_LUT[1]; /* override */
		tiSpp = 1U; /* consider only one sample per plane */
	}

	strip_size = TIFFStripSize(tif);
	buf = _TIFFmalloc(strip_size);
	if (buf == nullptr) {
		success = false;
		goto local_cleanup;
	}
	rowStride = (comps[0].w * tiSpp * comps[0].prec + 7U) / 8U;
	buffer32s = new int32_t[(size_t) comps[0].w * tiSpp];
	strip = 0;
	invert = tiPhoto == PHOTOMETRIC_MINISWHITE;
	for (uint32_t j = 0; j < numcomps; j++) {
		planes[j] = comps[j].data;
	}
	do {
		grk_image_comp *comp = comps + currentPlane;
		planes[0] = comp->data; /* to manage planar data */
		uint32_t height = comp->h;
		/* Read the Image components */
		for (; (height > 0) && (strip < TIFFNumberOfStrips(tif)); strip++) {
			tsize_t ssize = TIFFReadEncodedStrip(tif, strip, buf, strip_size);
			if (ssize < 1 || ssize > strip_size) {
				spdlog::error("tiftoimage: Bad value for ssize(%lld) "
						"vs. strip_size(%lld).\n\tAborting.\n",
						(long long) ssize, (long long) strip_size);
				success = false;
				goto local_cleanup;
			}
			const uint8_t *datau8 = (const uint8_t*) buf;
			while (ssize >= rowStride) {
				cvtTifTo32s(datau8, buffer32s, (size_t) comp->w * tiSpp,
						invert);
				cvtCxToPx(buffer32s, planes, (size_t) comp->w);
				planes[0] += comp->w;
				planes[1] += comp->w;
				planes[2] += comp->w;
				planes[3] += comp->w;
				datau8 += rowStride;
				ssize -= rowStride;
				height--;
			}
		}
		currentPlane++;
	} while ((tiPC == PLANARCONFIG_SEPARATE) && (currentPlane < numcomps));
	local_cleanup: delete[] buffer32s;
	if (buf)
		_TIFFfree(buf);
	return success;
}

static bool readTiffPixelsSigned(TIFF *tif, grk_image_comp *comps,
		uint32_t numcomps, uint16_t tiSpp, uint16_t tiPC, uint16_t tiPhoto) {
	if (!tif)
		return false;

	(void) tiPhoto;
	bool success = true;
	convert_32s_CXPX cvtCxToPx = nullptr;
	int32_t *planes[4];
	tsize_t rowStride;
	tdata_t buf = nullptr;
	tstrip_t strip;
	tsize_t strip_size;
	uint32_t currentPlane = 0;
	int32_t *buffer32s = nullptr;

	cvtCxToPx = convert_32s_CXPX_LUT[numcomps];
	if (tiPC == PLANARCONFIG_SEPARATE) {
		cvtCxToPx = convert_32s_CXPX_LUT[1]; /* override */
		tiSpp = 1U; /* consider only one sample per plane */
	}

	strip_size = TIFFStripSize(tif);
	buf = _TIFFmalloc(strip_size);
	if (buf == nullptr) {
		success = false;
		goto local_cleanup;
	}
	rowStride = (comps[0].w * tiSpp * comps[0].prec + 7U) / 8U;
	buffer32s = new int32_t[(size_t) comps[0].w * tiSpp];
	strip = 0;
	for (uint32_t j = 0; j < numcomps; j++) {
		planes[j] = comps[j].data;
	}
	do {
		grk_image_comp *comp = comps + currentPlane;
		planes[0] = comp->data; /* to manage planar data */
		uint32_t height = comp->h;
		/* Read the Image components */
		for (; (height > 0) && (strip < TIFFNumberOfStrips(tif)); strip++) {
			tsize_t ssize = TIFFReadEncodedStrip(tif, strip, buf, strip_size);
			if (ssize < 1 || ssize > strip_size) {
				spdlog::error("tiftoimage: Bad value for ssize(%lld) "
						"vs. strip_size(%lld).\n\tAborting.\n",
						(long long) ssize, (long long) strip_size);
				success = false;
				goto local_cleanup;
			}
			const int8_t *datau8 = (const int8_t*) buf;
			while (ssize >= rowStride) {
				for (size_t i = 0; i < (size_t) comp->w * tiSpp; ++i)
					buffer32s[i] = datau8[i];
				cvtCxToPx(buffer32s, planes, (size_t) comp->w);
				planes[0] += comp->w;
				planes[1] += comp->w;
				planes[2] += comp->w;
				planes[3] += comp->w;
				datau8 += rowStride;
				ssize -= rowStride;
				height--;
			}
		}
		currentPlane++;
	} while ((tiPC == PLANARCONFIG_SEPARATE) && (currentPlane < numcomps));
	local_cleanup: delete[] buffer32s;
	if (buf)
		_TIFFfree(buf);
	return success;
}

static int imagetotif(grk_image *image, const char *outfile,
		uint32_t compression, bool verbose) {
	int tiPhoto;
	TIFF *tif = nullptr;
	tdata_t buf = nullptr;
	tsize_t strip_size, rowStride;
	int32_t const *planes[4];
	int32_t *buffer32s = nullptr;
	convert_32s_PXCX cvtPxToCx = nullptr;
	convert_32sXXx_C1R cvt32sToTif = nullptr;
	bool success = true;
	int32_t firstAlpha = -1;
	size_t numAlphaChannels = 0;
	planes[0] = image->comps[0].data;
	uint32_t numcomps = image->numcomps;
	uint32_t sgnd = image->comps[0].sgnd;
	int32_t adjust =
			(image->comps[0].sgnd && image->comps[0].prec < 8) ?
					1 << (image->comps[0].prec - 1) : 0;
	if (image->color_space == GRK_CLRSPC_CMYK) {
		if (numcomps < 4U) {
			spdlog::error(
					"imagetotif: CMYK images shall be composed of at least 4 planes.");
			spdlog::error("\tAborting");
			return 1;
		}
		tiPhoto = PHOTOMETRIC_SEPARATED;
		if (numcomps > 4U) {
			if (verbose)
				spdlog::warn("imagetotif: number of components {} is "
						"greater than 4. Truncating to 4", numcomps);
			numcomps = 4U;
		}
	} else if (numcomps > 2U) {
		switch (image->color_space){
		case GRK_CLRSPC_UNKNOWN:
		case GRK_CLRSPC_UNSPECIFIED:
		case GRK_CLRSPC_SRGB:
			tiPhoto = PHOTOMETRIC_RGB;
			break;
		case GRK_CLRSPC_EYCC:
		case GRK_CLRSPC_SYCC:
			tiPhoto = PHOTOMETRIC_YCBCR;
			break;
		case GRK_CLRSPC_DEFAULT_CIE:
		case GRK_CLRSPC_CUSTOM_CIE:
			tiPhoto = sgnd ? PHOTOMETRIC_CIELAB : PHOTOMETRIC_ICCLAB;
			break;
		default:
			tiPhoto = PHOTOMETRIC_RGB;
			break;
		}
		if (numcomps > 4U) {
			if (verbose)
				spdlog::warn("imagetotif: number of components {} is "
						"greater than 4. Truncating to 4", numcomps);
			numcomps = 4U;
		}
	} else {
		tiPhoto = PHOTOMETRIC_MINISBLACK;
	}


	uint32_t width = image->comps[0].w;
	uint32_t height = image->comps[0].h;

	// actual bits per sample
	uint32_t bps = image->comps[0].prec;
	uint32_t tif_bps = bps;
	if (bps == 0) {
		spdlog::error("imagetotif: image precision is zero.");
		success = false;
		goto cleanup;
	}

	//check for null image components
	for (uint32_t i = 0; i < numcomps; ++i) {
		auto comp = image->comps[i];
		if (!comp.data) {
			spdlog::error("imagetotif: component {} is null.", i);
			spdlog::error("\tAborting");
			success = false;
			goto cleanup;
		}
	}
	uint32_t i;
	for (i = 1U; i < numcomps; ++i) {
		if (image->comps[0].dx != image->comps[i].dx)
			break;
		if (image->comps[0].dy != image->comps[i].dy)
			break;
		if (image->comps[0].prec != image->comps[i].prec)
			break;
		if (image->comps[0].sgnd != image->comps[i].sgnd)
			break;
		planes[i] = image->comps[i].data;
	}
	if (i != numcomps) {
		spdlog::error(
				"imagetotif: All components shall have the same subsampling, same bit depth.");
		spdlog::error("\tAborting");
		success = false;
		goto cleanup;
	}

	// even bits per sample
	if (bps > 16)
		bps = 0;
	if (bps == 0) {
		spdlog::error("imagetotif: Bits={}, Only 1 to 16 bits implemented\n",
				bps);
		spdlog::error("\tAborting");
		success = false;
		goto cleanup;
	}
	cvtPxToCx = convert_32s_PXCX_LUT[numcomps];
	switch (tif_bps) {
	case 1:
	case 2:
	case 4:
	case 6:
	case 8:
		cvt32sToTif = convert_32sXXu_C1R_LUT[tif_bps];
		break;
	case 3:
		cvt32sToTif = tif_32sto3u;
		break;
	case 5:
		cvt32sToTif = tif_32sto5u;
		break;
	case 7:
		cvt32sToTif = tif_32sto7u;
		break;
	case 9:
		cvt32sToTif = tif_32sto9u;
		break;
	case 10:
		cvt32sToTif = tif_32sto10u;
		break;
	case 11:
		cvt32sToTif = tif_32sto11u;
		break;
	case 12:
		cvt32sToTif = tif_32sto12u;
		break;
	case 13:
		cvt32sToTif = tif_32sto13u;
		break;
	case 14:
		cvt32sToTif = tif_32sto14u;
		break;
	case 15:
		cvt32sToTif = tif_32sto15u;
		break;
	case 16:
		cvt32sToTif = (convert_32sXXx_C1R) tif_32sto16u;
		break;
	default:
		/* never here */
		break;
	}
	// Alpha channels
	for (i = 0U; i < numcomps; ++i) {
		if (image->comps[i].alpha) {
			if (firstAlpha == -1)
				firstAlpha = 0;
			numAlphaChannels++;
		}
	}
	// TIFF assumes that alpha channels occur as last channels in image.
	if (numAlphaChannels && ((size_t)firstAlpha + numAlphaChannels >= numcomps)) {
		if (verbose)
			spdlog::warn(
					"TIFF requires that alpha channels occur as last channels in image. TIFFTAG_EXTRASAMPLES tag for alpha will not be set");
		numAlphaChannels = 0;
	}
	buffer32s = (int32_t*) malloc((size_t) width * numcomps * sizeof(int32_t));
	if (buffer32s == nullptr) {
		success = false;
		goto cleanup;
	}

	tif = TIFFOpen(outfile, "wb");
	if (!tif) {
		spdlog::error("imagetotif:failed to open {} for writing\n", outfile);
		success = false;
		goto cleanup;
	}

	TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, width);
	TIFFSetField(tif, TIFFTAG_IMAGELENGTH, height);
	TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT,
			sgnd ? SAMPLEFORMAT_INT : SAMPLEFORMAT_UINT);
	TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, numcomps);
	TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, tif_bps);
	TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
	TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, tiPhoto);
	TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 1);
    if( tiPhoto == PHOTOMETRIC_YCBCR )	{
		TIFFSetField( tif, TIFFTAG_YCBCRSUBSAMPLING, image->comps[1].dx, image->comps[1].dy);
		/* TODO: also write YCbCrPositioning and YCbCrCoefficients tag identical to source IFD */
	}

	if (compression == COMPRESSION_ADOBE_DEFLATE) {
#ifdef ZIP_SUPPORT
		TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_ADOBE_DEFLATE); // zip compression
#endif
	}

	if (image->icc_profile_buf) {
		if (image->color_space == GRK_CLRSPC_ICC)
			TIFFSetField(tif, TIFFTAG_ICCPROFILE, image->icc_profile_len,
					image->icc_profile_buf);
	}

	if (image->xmp_buf && image->xmp_len) {
		TIFFSetField(tif, TIFFTAG_XMLPACKET, image->xmp_len, image->xmp_buf);
	}

	if (image->iptc_buf && image->iptc_len) {
		auto iptc_buf = image->iptc_buf;
		auto iptc_len = image->iptc_len;

		// length must be multiple of 4
		uint8_t *buf = nullptr;
		iptc_len += (4 - (iptc_len & 0x03));
		if (iptc_len != image->iptc_len) {
			buf = (uint8_t*) calloc(iptc_len, 1);
			if (!buf)
				return false;
			memcpy(buf, image->iptc_buf, image->iptc_len);
			iptc_buf = buf;
		}

		// Tag is of type TIFF_LONG, so byte length is divided by four
		if (TIFFIsByteSwapped(tif))
			TIFFSwabArrayOfLong((uint32_t*) iptc_buf, iptc_len / 4);
		TIFFSetField(tif, TIFFTAG_RICHTIFFIPTC, (uint32_t) iptc_len / 4,
				(void*) iptc_buf);

		if (buf)
			free(buf);
	}

	if (image->capture_resolution[0] > 0 && image->capture_resolution[1] > 0) {
		TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_CENTIMETER); // cm
		for (int i = 0; i < 2; ++i) {
			TIFFSetField(tif, TIFFTAG_XRESOLUTION,
					image->capture_resolution[0] / 100);
			TIFFSetField(tif, TIFFTAG_YRESOLUTION,
					image->capture_resolution[1] / 100);
		}
	}

	if (numAlphaChannels) {
		std::unique_ptr<uint16[]> out(new uint16[numAlphaChannels]);
		size_t alphaCount = 0;
		for (i = 0U; i < numcomps; ++i) {
			if (image->comps[i].alpha)
				out[alphaCount++] =
						(image->comps[i].alpha == GROK_COMPONENT_TYPE_OPACITY) ?
								EXTRASAMPLE_UNASSALPHA : EXTRASAMPLE_ASSOCALPHA;
		}
		TIFFSetField(tif, TIFFTAG_EXTRASAMPLES, numAlphaChannels, out.get());
	}

	strip_size = TIFFStripSize(tif);
	rowStride = (width * numcomps * tif_bps + 7U) / 8U;
	if (rowStride != strip_size) {
		spdlog::error("Invalid TIFF strip size");
		success = false;
		goto cleanup;
	}
	buf = _TIFFmalloc(strip_size);
	if (buf == nullptr) {
		success = false;
		goto cleanup;
	}

	for (i = 0; i < image->comps[0].h; ++i) {
		cvtPxToCx(planes, buffer32s, (size_t) width, adjust);
		cvt32sToTif(buffer32s, (uint8_t*) buf, (size_t) width * numcomps);
		(void) TIFFWriteEncodedStrip(tif, i, (void*) buf, strip_size);
		planes[0] += width;
		planes[1] += width;
		planes[2] += width;
		planes[3] += width;
	}

	cleanup: if (buf)
		_TIFFfree((void*) buf);
	if (tif)
		TIFFClose(tif);
	if (buffer32s)
		free(buffer32s);

	return success ? 0 : 1;
}/* imagetotif() */

bool TIFFFormat::encode(grk_image *image, const char *filename,
		uint32_t compressionParam, bool verbose) {
	return imagetotif(image, filename, compressionParam, verbose) ? false : true;
}
grk_image* TIFFFormat::decode(const char *filename,
		grk_cparameters *parameters) {
	return tiftoimage(filename, parameters);
}

