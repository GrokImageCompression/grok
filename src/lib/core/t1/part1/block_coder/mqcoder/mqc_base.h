/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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

#include "mqc_state.h"
#include <string>
#include <cstddef>

#pragma once

namespace grk
{

typedef uint32_t grk_flag;

#define MQC_NUMCTXS 19

#define PUSH_MQC()            \
  auto curctx = coder.curctx; \
  uint32_t c = coder.c;       \
  uint32_t a = coder.a;       \
  uint8_t ct = coder.ct

#define POP_MQC()        \
  coder.curctx = curctx; \
  coder.c = c;           \
  coder.a = a;           \
  coder.ct = ct;

/** We hold the state of individual data points for the BlockCoder compressor using
 *  a single 32-bit flags word to hold the state of 4 data points.  This corresponds
 *  to the 4-point-high columns that the data is processed in.
 *  These \#defines declare the layout of a 32-bit flags word.
 */

/* SIGMA: significance state (3 cols x 6 rows)
 * CHI:   state for negative sample value (1 col x 6 rows)
 * MU:    state for visited in refinement pass (1 col x 4 rows)
 * PI:    state for visited in significance pass (1 col * 4 rows)
 */

#define T1_SIGMA_0 (1U << 0)
#define T1_SIGMA_1 (1U << 1)
#define T1_SIGMA_2 (1U << 2)
#define T1_SIGMA_3 (1U << 3)
#define T1_SIGMA_4 (1U << 4)
#define T1_SIGMA_5 (1U << 5)
#define T1_SIGMA_6 (1U << 6)
#define T1_SIGMA_7 (1U << 7)
#define T1_SIGMA_8 (1U << 8)
#define T1_SIGMA_9 (1U << 9)
#define T1_SIGMA_10 (1U << 10)
#define T1_SIGMA_11 (1U << 11)
#define T1_SIGMA_12 (1U << 12)
#define T1_SIGMA_13 (1U << 13)
#define T1_SIGMA_14 (1U << 14)
#define T1_SIGMA_15 (1U << 15)
#define T1_SIGMA_16 (1U << 16)
#define T1_SIGMA_17 (1U << 17)
#define T1_CHI_0 (1U << 18)
#define T1_CHI_0_I 18
#define T1_CHI_1 (1U << 19)
#define T1_CHI_1_I 19
#define T1_MU_0 (1U << 20)
#define T1_PI_0 (1U << 21)
#define T1_CHI_2 (1U << 22)
#define T1_CHI_2_I 22
#define T1_MU_1 (1U << 23)
#define T1_PI_1_I 24
#define T1_PI_1 (1U << T1_PI_1_I)
#define T1_CHI_3 (1U << 25)
#define T1_MU_2 (1U << 26)
#define T1_PI_2_I 27
#define T1_PI_2 (1U << T1_PI_2_I)
#define T1_CHI_4 (1U << 28)
#define T1_MU_3 (1U << 29)
#define T1_PI_3 (1U << 30)
#define T1_CHI_5 (1U << 31)
#define T1_CHI_5_I 31

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

#define T1_SIGMA_NW T1_SIGMA_0
#define T1_SIGMA_N T1_SIGMA_1
#define T1_SIGMA_NE T1_SIGMA_2
#define T1_SIGMA_W T1_SIGMA_3
#define T1_SIGMA_THIS T1_SIGMA_4
#define T1_SIGMA_E T1_SIGMA_5
#define T1_SIGMA_SW T1_SIGMA_6
#define T1_SIGMA_S T1_SIGMA_7
#define T1_SIGMA_SE T1_SIGMA_8
#define T1_SIGMA_NEIGHBOURS                                                                      \
  (T1_SIGMA_NW | T1_SIGMA_N | T1_SIGMA_NE | T1_SIGMA_W | T1_SIGMA_E | T1_SIGMA_SW | T1_SIGMA_S | \
   T1_SIGMA_SE)

#define T1_CHI_THIS T1_CHI_1
#define T1_CHI_THIS_I T1_CHI_1_I
#define T1_MU_THIS T1_MU_0
#define T1_PI_THIS T1_PI_0
#define T1_CHI_S T1_CHI_2

#define T1_LUT_SGN_W (1U << 0)
#define T1_LUT_SIG_N (1U << 1)
#define T1_LUT_SGN_E (1U << 2)
#define T1_LUT_SIG_W (1U << 3)
#define T1_LUT_SGN_N (1U << 4)
#define T1_LUT_SIG_E (1U << 5)
#define T1_LUT_SGN_S (1U << 6)
#define T1_LUT_SIG_S (1U << 7)

#define T1_TYPE_MQ 0 /** Normal coding using entropy coder */
#define T1_TYPE_RAW 1 /** Raw compressing*/

#define SETCURCTX(curctx, ctxno) curctx = &(mqc)->ctxs[(uint32_t)(ctxno)]

#define GETCTXNO_MAG(f) \
  (uint8_t)(T1_CTXNO_MAG + (((f) & T1_MU_0) ? 2 : !!((f) & T1_SIGMA_NEIGHBOURS)))

#define UPDATE_FLAGS(flags, flagsPtr, ci, s, stride, vsc) \
  {                                                       \
    /* east */                                            \
    flagsPtr[-1] |= T1_SIGMA_5 << (ci);                   \
    /* mark target as significant */                      \
    flags |= ((s << T1_CHI_1_I) | T1_SIGMA_4) << (ci);    \
    /* west */                                            \
    flagsPtr[1] |= T1_SIGMA_3 << (ci);                    \
    /* north-west, north, north-east */                   \
    if(ci == 0U && !(vsc))                                \
    {                                                     \
      auto north = flagsPtr - (stride);                   \
      *north |= (s << T1_CHI_5_I) | T1_SIGMA_16;          \
      north[-1] |= T1_SIGMA_17;                           \
      north[1] |= T1_SIGMA_15;                            \
    }                                                     \
    /* south-west, south, south-east */                   \
    if(ci == 9U)                                          \
    {                                                     \
      auto south = flagsPtr + (stride);                   \
      *south |= (s << T1_CHI_0_I) | T1_SIGMA_1;           \
      south[-1] |= T1_SIGMA_2;                            \
      south[1] |= T1_SIGMA_0;                             \
    }                                                     \
  }

#define GETCTXNO_ZC(mqc, f) (mqc)->lut_ctxno_zc_orient[((f) & T1_SIGMA_NEIGHBOURS)]

/*
0 pfX T1_CHI_THIS           T1_LUT_SGN_W
1 tfX T1_SIGMA_1            T1_LUT_SIG_N
2 nfX T1_CHI_THIS           T1_LUT_SGN_E
3 tfX T1_SIGMA_3            T1_LUT_SIG_W
4  fX T1_CHI_(THIS - 1)     T1_LUT_SGN_N
5 tfX T1_SIGMA_5            T1_LUT_SIG_E
6  fX T1_CHI_(THIS + 1)     T1_LUT_SGN_S
7 tfX T1_SIGMA_7            T1_LUT_SIG_S
*/
inline uint8_t getctxtno_sc_or_spb_index(uint32_t fX, uint32_t pfX, uint32_t nfX, uint32_t ci)
{
  uint32_t lu = (fX >> (ci)) & (T1_SIGMA_1 | T1_SIGMA_3 | T1_SIGMA_5 | T1_SIGMA_7);
  lu |= (pfX >> (T1_CHI_THIS_I + (ci))) & (1U << 0);
  lu |= (nfX >> (T1_CHI_THIS_I - 2U + (ci))) & (1U << 2);
  if(ci == 0U)
    lu |= (fX >> (T1_CHI_0_I - 4U)) & (1U << 4);
  else
    lu |= (fX >> (T1_CHI_1_I - 4U + ((ci - 3U)))) & (1U << 4);
  lu |= (fX >> (T1_CHI_2_I - 6U + (ci))) & (1U << 6);
  return (uint8_t)lu;
}

/**
 * @struct mqcoder_base
 * @brief MQ coder base
 */
struct mqcoder_base
{
  /**
   * @brief Creates an mqcoder_base
   */
  explicit mqcoder_base(bool cached);

  /**
   * @brief Destroys an mqcoder_base
   */
  ~mqcoder_base() = default;

  // Copy constructor
  mqcoder_base(const mqcoder_base& other);

  // Assignment operator for mqcoder_base
  mqcoder_base& operator=(const mqcoder_base& other);

  bool operator==(const mqcoder_base& other) const;
  /**
   * @brief Prints internal state
   */
  void print(const std::string& msg);

  void reinit(void);

  /**
   * @brief Temporary buffer where bits are coded or decoded
   */
  uint32_t c;

  /**
   *  @brief
   */
  uint32_t a;
  /**
   * @brief  Number of bits already read / available to write
   */
  uint8_t ct;

  /**
   * @brief Count the number of times a terminating {0xFF, >0x8F} marker is read
   *
   * Only used by decoder
   */
  uint32_t end_of_byte_stream_counter;
  /**
   *  @brief Pointer to current position in buffer
   */
  uint8_t* bp;

  /**
   * @brief  Array of contexts
   */
  const mqc_state* ctxs[MQC_NUMCTXS];
  /**
   *  @brief Pointer to current context in ctxs array
   */
  const mqc_state** curctx;

  /**
   * @brief Index of curctx in ctxs array
   */
  ptrdiff_t curctx_index_;

  /**
   * @brief true if in differential decompress mode
   */
  bool cached_;

  /**
   * @brief true if final layer is being decompressed
   */
  bool finalLayer_;
};

} // namespace grk
