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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include <algorithm>
using namespace std;

#include "t1_common.h"

/* BEGINNING of flags that apply to grk_flag */
/** We hold the state of individual data points for the T1 compressor using
 *  a single 32-bit flags word to hold the state of 4 data points.  This corresponds
 *  to the 4-point-high columns that the data is processed in.
 *
 *  These \#defines declare the layout of a 32-bit flags word.
 *
 *  This is currently done for compressing only.
 *  The values must NOT be changed, otherwise this is going to break a lot of
 *  assumptions.
 */

/* SIGMA: significance state (3 cols x 6 rows)
 * CHI:   state for negative sample value (1 col x 6 rows)
 * MU:    state for visited in refinement pass (1 col x 4 rows)
 * PI:    state for visited in significance pass (1 col * 4 rows)
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
#define T1_CHI_0    (1U << 18)
#define T1_CHI_0_I  18
#define T1_CHI_1    (1U << 19)
#define T1_CHI_1_I  19
#define T1_MU_0     (1U << 20)
#define T1_PI_0     (1U << 21)
#define T1_CHI_2    (1U << 22)
#define T1_CHI_2_I  22
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
#define T1_CHI_5    (1U << 31)
#define T1_CHI_5_I  31

/** As an example, the bits T1_SIGMA_3, T1_SIGMA_4 and T1_SIGMA_5
 *  indicate the significance state of the west neighbour of data point zero
 *  of our four, the point itself, and its east neighbour respectively.
 *  Many of the bits are arranged so that given a flags word, you can
 *  look at the values for the data point 0, then shift the flags
 *  word right by 3 bits and look at the same bit positions to see the
 *  values for data point 1.
 *
 *  The \#defines below help a bit with this; say you have a flags word
 *  f, you can do things like
 *
 *  (f & T1_SIGMA_THIS)
 *
 *  to see the significance bit of data point 0, then do
 *
 *  ((f >> 3) & T1_SIGMA_THIS)
 *
 *  to see the significance bit of data point 1.
 */

#define T1_SIGMA_NW   T1_SIGMA_0
#define T1_SIGMA_N    T1_SIGMA_1
#define T1_SIGMA_NE   T1_SIGMA_2
#define T1_SIGMA_W    T1_SIGMA_3
#define T1_SIGMA_THIS T1_SIGMA_4
#define T1_SIGMA_E    T1_SIGMA_5
#define T1_SIGMA_SW   T1_SIGMA_6
#define T1_SIGMA_S    T1_SIGMA_7
#define T1_SIGMA_SE   T1_SIGMA_8
#define T1_SIGMA_NEIGHBOURS (T1_SIGMA_NW | T1_SIGMA_N | T1_SIGMA_NE | T1_SIGMA_W | T1_SIGMA_E | T1_SIGMA_SW | T1_SIGMA_S | T1_SIGMA_SE)

#define T1_CHI_THIS   T1_CHI_1
#define T1_CHI_THIS_I T1_CHI_1_I
#define T1_MU_THIS    T1_MU_0
#define T1_PI_THIS    T1_PI_0
#define T1_CHI_S      T1_CHI_2

#define T1_LUT_SGN_W (1U << 0)
#define T1_LUT_SIG_N (1U << 1)
#define T1_LUT_SGN_E (1U << 2)
#define T1_LUT_SIG_W (1U << 3)
#define T1_LUT_SGN_N (1U << 4)
#define T1_LUT_SIG_E (1U << 5)
#define T1_LUT_SGN_S (1U << 6)
#define T1_LUT_SIG_S (1U << 7)

#define T1_TYPE_MQ 0    /** Normal coding using entropy coder */
#define T1_TYPE_RAW 1   /** Raw compressing*/

static int t1_init_ctxno_zc(uint32_t f, uint32_t orientation)
{
    int h, v, d, n, t, hv;
    n = 0;
    h = ((f & T1_SIGMA_3) != 0) + ((f & T1_SIGMA_5) != 0);
    v = ((f & T1_SIGMA_1) != 0) + ((f & T1_SIGMA_7) != 0);
    d = ((f & T1_SIGMA_0) != 0) + ((f & T1_SIGMA_2) != 0) + ((
                f & T1_SIGMA_8) != 0) + ((f & T1_SIGMA_6) != 0);

    switch (orientation) {
    case 2:
        t = h;
        h = v;
        v = t;
    case 0:
    case 1:
        if (!h) {
            if (!v) {
                if (!d) {
                    n = 0;
                } else if (d == 1) {
                    n = 1;
                } else {
                    n = 2;
                }
            } else if (v == 1) {
                n = 3;
            } else {
                n = 4;
            }
        } else if (h == 1) {
            if (!v) {
                if (!d) {
                    n = 5;
                } else {
                    n = 6;
                }
            } else {
                n = 7;
            }
        } else {
            n = 8;
        }
        break;
    case 3:
        hv = h + v;
        if (!d) {
            if (!hv) {
                n = 0;
            } else if (hv == 1) {
                n = 1;
            } else {
                n = 2;
            }
        } else if (d == 1) {
            if (!hv) {
                n = 3;
            } else if (hv == 1) {
                n = 4;
            } else {
                n = 5;
            }
        } else if (d == 2) {
            if (!hv) {
                n = 6;
            } else {
                n = 7;
            }
        } else {
            n = 8;
        }
        break;
    }

    return (T1_CTXNO_ZC + n);
}

static int t1_init_ctxno_sc(uint32_t f)
{
    int hc, vc, n;
    n = 0;

    hc = min(((f & (T1_LUT_SIG_E | T1_LUT_SGN_E)) ==
                      T1_LUT_SIG_E) + ((f & (T1_LUT_SIG_W | T1_LUT_SGN_W)) == T1_LUT_SIG_W),
                     1) - min(((f & (T1_LUT_SIG_E | T1_LUT_SGN_E)) ==
                                       (T1_LUT_SIG_E | T1_LUT_SGN_E)) +
                                      ((f & (T1_LUT_SIG_W | T1_LUT_SGN_W)) ==
                                       (T1_LUT_SIG_W | T1_LUT_SGN_W)), 1);

    vc = min(((f & (T1_LUT_SIG_N | T1_LUT_SGN_N)) ==
                      T1_LUT_SIG_N) + ((f & (T1_LUT_SIG_S | T1_LUT_SGN_S)) == T1_LUT_SIG_S),
                     1) - min(((f & (T1_LUT_SIG_N | T1_LUT_SGN_N)) ==
                                       (T1_LUT_SIG_N | T1_LUT_SGN_N)) +
                                      ((f & (T1_LUT_SIG_S | T1_LUT_SGN_S)) ==
                                       (T1_LUT_SIG_S | T1_LUT_SGN_S)), 1);

    if (hc < 0) {
        hc = -hc;
        vc = -vc;
    }
    if (!hc) {
        if (vc == -1) {
            n = 1;
        } else if (!vc) {
            n = 0;
        } else {
            n = 1;
        }
    } else if (hc == 1) {
        if (vc == -1) {
            n = 2;
        } else if (!vc) {
            n = 3;
        } else {
            n = 4;
        }
    }

    return (T1_CTXNO_SC + n);
}

static int t1_init_spb(uint32_t f)
{
    int hc, vc, n;

    hc = min(((f & (T1_LUT_SIG_E | T1_LUT_SGN_E)) ==
                      T1_LUT_SIG_E) + ((f & (T1_LUT_SIG_W | T1_LUT_SGN_W)) == T1_LUT_SIG_W),
                     1) - min(((f & (T1_LUT_SIG_E | T1_LUT_SGN_E)) ==
                                       (T1_LUT_SIG_E | T1_LUT_SGN_E)) +
                                      ((f & (T1_LUT_SIG_W | T1_LUT_SGN_W)) ==
                                       (T1_LUT_SIG_W | T1_LUT_SGN_W)), 1);

    vc = min(((f & (T1_LUT_SIG_N | T1_LUT_SGN_N)) ==
                      T1_LUT_SIG_N) + ((f & (T1_LUT_SIG_S | T1_LUT_SGN_S)) == T1_LUT_SIG_S),
                     1) - min(((f & (T1_LUT_SIG_N | T1_LUT_SGN_N)) ==
                                       (T1_LUT_SIG_N | T1_LUT_SGN_N)) +
                                      ((f & (T1_LUT_SIG_S | T1_LUT_SGN_S)) ==
                                       (T1_LUT_SIG_S | T1_LUT_SGN_S)), 1);

    if (!hc && !vc) {
        n = 0;
    } else {
        n = (!(hc > 0 || (!hc && vc > 0)));
    }

    return n;
}

static void dump_array16(int array[], int size)
{
    int i;
    --size;
    for (i = 0; i < size; ++i) {
        printf("0x%04x,", array[i]);
        if (!((i + 1) & 0x7)) {
            printf("\n    ");
        } else {
            printf(" ");
        }
    }
    printf("0x%04x\n};\n\n", array[size]);
}

int main(int argc, char **argv)
{
    unsigned int i, j;
    double u, v, t;

    int lut_ctxno_zc[2048];
    int lut_nmsedec_sig[1 << T1_NMSEDEC_BITS];
    int lut_nmsedec_sig0[1 << T1_NMSEDEC_BITS];
    int lut_nmsedec_ref[1 << T1_NMSEDEC_BITS];
    int lut_nmsedec_ref0[1 << T1_NMSEDEC_BITS];
    (void)argc;
    (void)argv;

    printf("/* This file was automatically generated by t1_generate_luts.c */\n\n");

    /* lut_ctxno_zc */
    for (j = 0; j < 4; ++j) {
        for (i = 0; i < 512; ++i) {
            uint32_t orientation = j;
            if (orientation == 2) {
                orientation = 1;
            } else if (orientation == 1) {
                orientation = 2;
            }
            lut_ctxno_zc[(orientation << 9) | i] = t1_init_ctxno_zc(i, j);
        }
    }

    printf("static const uint8_t lut_ctxno_zc[2048] = {\n    ");
    for (i = 0; i < 2047; ++i) {
        printf("%i,", lut_ctxno_zc[i]);
        if (!((i + 1) & 0x1f)) {
            printf("\n    ");
        } else {
            printf(" ");
        }
    }
    printf("%i\n};\n\n", lut_ctxno_zc[2047]);

    /* lut_ctxno_sc */
    printf("static const uint8_t lut_ctxno_sc[256] = {\n    ");
    for (i = 0; i < 255; ++i) {
        printf("0x%x,", t1_init_ctxno_sc(i));
        if (!((i + 1) & 0xf)) {
            printf("\n    ");
        } else {
            printf(" ");
        }
    }
    printf("0x%x\n};\n\n", t1_init_ctxno_sc(255));

    /* lut_spb */
    printf("static const uint8_t lut_spb[256] = {\n    ");
    for (i = 0; i < 255; ++i) {
        printf("%i,", t1_init_spb(i));
        if (!((i + 1) & 0x1f)) {
            printf("\n    ");
        } else {
            printf(" ");
        }
    }
    printf("%i\n};\n\n", t1_init_spb(255));

    /* FIXME FIXME FIXME */
    /* fprintf(stdout,"nmsedec luts:\n"); */
    for (i = 0U; i < (1U << T1_NMSEDEC_BITS); ++i) {
        t = i / pow(2, T1_NMSEDEC_FRACBITS);
        u = t;
        v = t - 1.5;
        lut_nmsedec_sig[i] =
            max(0,
                        (int)(floor((u * u - v * v) * pow(2, T1_NMSEDEC_FRACBITS) + 0.5) / pow(2,
                                T1_NMSEDEC_FRACBITS) * 8192.0));
        lut_nmsedec_sig0[i] =
            max(0,
                        (int)(floor((u * u) * pow(2, T1_NMSEDEC_FRACBITS) + 0.5) / pow(2,
                                T1_NMSEDEC_FRACBITS) * 8192.0));
        u = t - 1.0;
        if (i & (1 << (T1_NMSEDEC_BITS - 1))) {
            v = t - 1.5;
        } else {
            v = t - 0.5;
        }
        lut_nmsedec_ref[i] =
            max(0,
                        (int)(floor((u * u - v * v) * pow(2, T1_NMSEDEC_FRACBITS) + 0.5) / pow(2,
                                T1_NMSEDEC_FRACBITS) * 8192.0));
        lut_nmsedec_ref0[i] =
            max(0,
                        (int)(floor((u * u) * pow(2, T1_NMSEDEC_FRACBITS) + 0.5) / pow(2,
                                T1_NMSEDEC_FRACBITS) * 8192.0));
    }

    printf("static const int16_t lut_nmsedec_sig[1U << T1_NMSEDEC_BITS] = {\n    ");
    dump_array16(lut_nmsedec_sig, 1U << T1_NMSEDEC_BITS);

    printf("static const int16_t lut_nmsedec_sig0[1U << T1_NMSEDEC_BITS] = {\n    ");
    dump_array16(lut_nmsedec_sig0, 1U << T1_NMSEDEC_BITS);

    printf("static const int16_t lut_nmsedec_ref[1U << T1_NMSEDEC_BITS] = {\n    ");
    dump_array16(lut_nmsedec_ref, 1U << T1_NMSEDEC_BITS);

    printf("static const int16_t lut_nmsedec_ref0[1U << T1_NMSEDEC_BITS] = {\n    ");
    dump_array16(lut_nmsedec_ref0, 1U << T1_NMSEDEC_BITS);

    return 0;
}
