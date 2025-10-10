/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

#include "grk_apps_config.h"
#include <string.h>
#include "grok.h"
#include "packer.h"

#define INV_MASK_16 0xFFFF
#define INV_MASK_15 ((1 << 15) - 1)
#define INV_MASK_14 ((1 << 14) - 1)
#define INV_MASK_13 ((1 << 13) - 1)
#define INV_MASK_12 ((1 << 12) - 1)
#define INV_MASK_11 ((1 << 11) - 1)
#define INV_MASK_10 ((1 << 10) - 1)
#define INV_MASK_9 ((1 << 9) - 1)
#define INV_MASK_8 0xFF
#define INV_MASK_7 ((1 << 7) - 1)
#define INV_MASK_6 ((1 << 6) - 1)
#define INV_MASK_5 ((1 << 5) - 1)
#define INV_MASK_4 ((1 << 4) - 1)
#define INV_MASK_3 ((1 << 3) - 1)
#define INV_MASK_2 ((1 << 2) - 1)

#define GETBITS(dest, nb, mask, invert)                           \
  {                                                               \
    uint8_t needed = (nb);                                        \
    uint32_t dst = 0U;                                            \
    if(available == 0)                                            \
    {                                                             \
      val = *src++;                                               \
      available = 8;                                              \
    }                                                             \
    while(needed > available)                                     \
    {                                                             \
      dst |= val & ((1U << available) - 1U);                      \
      needed -= available;                                        \
      dst <<= needed;                                             \
      val = *src++;                                               \
      available = 8;                                              \
    }                                                             \
    dst |= (val >> (available - needed)) & ((1U << needed) - 1U); \
    available -= needed;                                          \
    dest = INV((T)dst, mask, invert);                             \
  }

/**
 * @brief Writes one row of interleaved pixels to N planar rows
 *
 * @tparam T type of src and dest buffers
 * @param src non-strided interleaved source buffer
 * @param dest array of N planar row buffers
 * @param w width of source
 * @param numComps number of components to interleave
 */
template<typename T>
void interleave(const T* src, T* const* dest, uint32_t w, uint16_t numComps)
{
  if(numComps == 1)
  {
    memcpy(dest[0], src, w * sizeof(T));
  }
  else
  {
    size_t src_index = 0;
    for(size_t i = 0; i < w; i++)
    {
      for(size_t j = 0; j < numComps; ++j)
        dest[j][i] = src[src_index++];
    }
  }
}

template<typename T>
T sign_extend(T val, uint8_t shift)
{
  val <<= shift;
  val >>= shift;

  return val;
}

/*
 * bit depth conversions for bit depth <= 8 and 16
 * used by PNG/TIFF
 *
 *
 */
#define INV(val, mask, invert) ((invert) ? ((val) ^ (mask)) : (val))

template<size_t N, bool isSigned, typename T>
void convertToOutput(const uint8_t* src, T* dest, size_t w, bool invert)
{
  if constexpr(N == 1)
  {
    size_t i;
    for(i = 0; i < (w & ~(size_t)7U); i += 8U)
    {
      uint32_t val = *src++;
      dest[i + 0] = INV((T)(val >> 7), 1, invert);
      dest[i + 1] = INV((T)((val >> 6) & 0x1U), 1, invert);
      dest[i + 2] = INV((T)((val >> 5) & 0x1U), 1, invert);
      dest[i + 3] = INV((T)((val >> 4) & 0x1U), 1, invert);
      dest[i + 4] = INV((T)((val >> 3) & 0x1U), 1, invert);
      dest[i + 5] = INV((T)((val >> 2) & 0x1U), 1, invert);
      dest[i + 6] = INV((T)((val >> 1) & 0x1U), 1, invert);
      dest[i + 7] = INV((T)(val & 0x1U), 1, invert);
    }

    w = w & 7U;
    if(w)
    {
      uint32_t val = *src++;
      for(size_t j = 0; j < w; ++j)
        dest[i + j] = INV((T)(val >> (7 - j)), 1, invert);
    }
  }
  else if constexpr(N == 2)
  {
    size_t i;
    for(i = 0; i < (w & ~(size_t)3U); i += 4U)
    {
      uint32_t val = *src++;
      dest[i + 0] = INV((T)(val >> 6), 3, invert);
      dest[i + 1] = INV((T)((val >> 4) & 0x3U), 3, invert);
      dest[i + 2] = INV((T)((val >> 2) & 0x3U), 3, invert);
      dest[i + 3] = INV((T)(val & 0x3U), 3, invert);
    }
    if(w & 3U)
    {
      uint32_t val = *src++;
      w = w & 3U;
      dest[i + 0] = INV((T)(val >> 6), 3, invert);

      if(w > 1U)
      {
        dest[i + 1] = INV((T)((val >> 4) & 0x3U), 3, invert);
        if(w > 2U)
          dest[i + 2] = INV((T)((val >> 2) & 0x3U), 3, invert);
      }
    }
  }
  else if constexpr(N == 3)
  {
    size_t i;
    for(i = 0; i < (w & ~(size_t)7U); i += 8U)
    {
      uint32_t val0 = *src++;
      uint32_t val1 = *src++;
      uint32_t val2 = *src++;

      dest[i + 0] = INV((T)((val0 >> 5)), INV_MASK_3, invert);
      dest[i + 1] = INV((T)(((val0 & 0x1FU) >> 2)), INV_MASK_3, invert);
      dest[i + 2] = INV((T)(((val0 & 0x3U) << 1) | (val1 >> 7)), INV_MASK_3, invert);
      dest[i + 3] = INV((T)(((val1 & 0x7FU) >> 4)), INV_MASK_3, invert);
      dest[i + 4] = INV((T)(((val1 & 0xFU) >> 1)), INV_MASK_3, invert);
      dest[i + 5] = INV((T)(((val1 & 0x1U) << 2) | (val2 >> 6)), INV_MASK_3, invert);
      dest[i + 6] = INV((T)(((val2 & 0x3FU) >> 3)), INV_MASK_3, invert);
      dest[i + 7] = INV((T)(((val2 & 0x7U))), INV_MASK_3, invert);
    }

    w = w & 7U;
    if(w)
    {
      uint32_t val = 0;
      uint8_t available = 0;
      for(size_t j = 0; j < w; ++j)
        GETBITS(dest[i + j], 3, INV_MASK_3, invert)
    }
  }
  else if constexpr(N == 4)
  {
    if(isSigned)
    {
      size_t i;
      for(i = 0; i < (w & ~(size_t)1U); i += 2U)
      {
        uint8_t val = *src++;
        dest[i + 0] = INV(sign_extend<T>(val >> 4, 32 - 4), 0xF, invert);
        dest[i + 1] = INV(sign_extend<T>(val & 0xF, 32 - 4), 0xF, invert);
      }
      if(w & 1U)
        dest[i + 0] = INV(sign_extend<T>((*src++) >> 4, 32 - 4), 0xF, invert);
    }
    else
    {
      size_t i;
      for(i = 0; i < (w & ~(size_t)1U); i += 2U)
      {
        uint32_t val = *src++;
        dest[i + 0] = INV((T)(val >> 4), 0xF, invert);
        dest[i + 1] = INV((T)(val & 0xFU), 0xF, invert);
      }
      if(w & 1U)
        dest[i + 0] = INV((T)((*src++) >> 4), 0xF, invert);
    }
  }
  else if constexpr(N == 5)
  {
    size_t i;
    for(i = 0; i < (w & ~(size_t)7U); i += 8U)
    {
      uint32_t val0 = *src++;
      uint32_t val1 = *src++;
      uint32_t val2 = *src++;
      uint32_t val3 = *src++;
      uint32_t val4 = *src++;

      dest[i + 0] = INV((T)((val0 >> 3)), INV_MASK_5, invert);
      dest[i + 1] = INV((T)(((val0 & 0x7U) << 2) | (val1 >> 6)), INV_MASK_5, invert);
      dest[i + 2] = INV((T)(((val1 & 0x3FU) >> 1)), INV_MASK_5, invert);
      dest[i + 3] = INV((T)(((val1 & 0x1U) << 4) | (val2 >> 4)), INV_MASK_5, invert);
      dest[i + 4] = INV((T)(((val2 & 0xFU) << 1) | (val3 >> 7)), INV_MASK_5, invert);
      dest[i + 5] = INV((T)(((val3 & 0x7FU) >> 2)), INV_MASK_5, invert);
      dest[i + 6] = INV((T)(((val3 & 0x3U) << 3) | (val4 >> 5)), INV_MASK_5, invert);
      dest[i + 7] = INV((T)(((val4 & 0x1FU))), INV_MASK_5, invert);
    }

    w = w & 7U;
    if(w)
    {
      uint32_t val = 0;
      uint8_t available = 0;
      for(size_t j = 0; j < w; ++j)
        GETBITS(dest[i + j], 5, INV_MASK_5, invert)
    }
  }
  else if constexpr(N == 6)
  {
    size_t i;
    for(i = 0; i < (w & ~(size_t)3U); i += 4U)
    {
      uint32_t val0 = *src++;
      uint32_t val1 = *src++;
      uint32_t val2 = *src++;
      dest[i + 0] = INV((T)(val0 >> 2), 63, invert);
      dest[i + 1] = INV((T)(((val0 & 0x3U) << 4) | (val1 >> 4)), 63, invert);
      dest[i + 2] = INV((T)(((val1 & 0xFU) << 2) | (val2 >> 6)), 63, invert);
      dest[i + 3] = INV((T)(val2 & 0x3FU), 63, invert);
    }
    if(w & 3U)
    {
      uint32_t val0 = *src++;
      w = w & 3U;
      dest[i + 0] = INV((T)(val0 >> 2), 63, invert);

      if(w > 1U)
      {
        uint32_t val1 = *src++;
        dest[i + 1] = INV((T)(((val0 & 0x3U) << 4) | (val1 >> 4)), 63, invert);
        if(w > 2U)
          dest[i + 2] = INV((T)(((val1 & 0xFU) << 2) | ((*src++) >> 6)), 63, invert);
      }
    }
  }
  else if constexpr(N == 7)
  {
    size_t i;
    for(i = 0; i < (w & ~(size_t)7U); i += 8U)
    {
      uint32_t val0 = *src++;
      uint32_t val1 = *src++;
      uint32_t val2 = *src++;
      uint32_t val3 = *src++;
      uint32_t val4 = *src++;
      uint32_t val5 = *src++;
      uint32_t val6 = *src++;

      dest[i + 0] = INV((T)((val0 >> 1)), INV_MASK_7, invert);
      dest[i + 1] = INV((T)(((val0 & 0x1U) << 6) | (val1 >> 2)), INV_MASK_7, invert);
      dest[i + 2] = INV((T)(((val1 & 0x3U) << 5) | (val2 >> 3)), INV_MASK_7, invert);
      dest[i + 3] = INV((T)(((val2 & 0x7U) << 4) | (val3 >> 4)), INV_MASK_7, invert);
      dest[i + 4] = INV((T)(((val3 & 0xFU) << 3) | (val4 >> 5)), INV_MASK_7, invert);
      dest[i + 5] = INV((T)(((val4 & 0x1FU) << 2) | (val5 >> 6)), INV_MASK_7, invert);
      dest[i + 6] = INV((T)(((val5 & 0x3FU) << 1) | (val6 >> 7)), INV_MASK_7, invert);
      dest[i + 7] = INV((T)(((val6 & 0x7FU))), INV_MASK_7, invert);
    }

    w = w & 7U;
    if(w)
    {
      uint32_t val = 0;
      uint8_t available = 0;
      for(size_t j = 0; j < w; ++j)
        GETBITS(dest[i + j], 7, INV_MASK_7, invert)
    }
  }
  else if constexpr(N == 8)
  {
    for(size_t i = 0; i < w; i++)
      dest[i] = INV(src[i], 0xFF, invert);
  }
  else if constexpr(N == 9)
  {
    size_t i;
    for(i = 0; i < (w & ~(size_t)7U); i += 8U)
    {
      uint32_t val0 = *src++;
      uint32_t val1 = *src++;
      uint32_t val2 = *src++;
      uint32_t val3 = *src++;
      uint32_t val4 = *src++;
      uint32_t val5 = *src++;
      uint32_t val6 = *src++;
      uint32_t val7 = *src++;
      uint32_t val8 = *src++;

      dest[i + 0] = INV((T)((val0 << 1) | (val1 >> 7)), INV_MASK_9, invert);
      dest[i + 1] = INV((T)(((val1 & 0x7FU) << 2) | (val2 >> 6)), INV_MASK_9, invert);
      dest[i + 2] = INV((T)(((val2 & 0x3FU) << 3) | (val3 >> 5)), INV_MASK_9, invert);
      dest[i + 3] = INV((T)(((val3 & 0x1FU) << 4) | (val4 >> 4)), INV_MASK_9, invert);
      dest[i + 4] = INV((T)(((val4 & 0xFU) << 5) | (val5 >> 3)), INV_MASK_9, invert);
      dest[i + 5] = INV((T)(((val5 & 0x7U) << 6) | (val6 >> 2)), INV_MASK_9, invert);
      dest[i + 6] = INV((T)(((val6 & 0x3U) << 7) | (val7 >> 1)), INV_MASK_9, invert);
      dest[i + 7] = INV((T)(((val7 & 0x1U) << 8) | (val8)), INV_MASK_9, invert);
    }
    w = w & 7U;
    if(w)
    {
      uint32_t val = 0;
      uint8_t available = 0;
      for(size_t j = 0; j < w; ++j)
        GETBITS(dest[i + j], 9, INV_MASK_9, invert)
    }
  }
  else if constexpr(N == 10)
  {
    if(isSigned)
    {
      size_t i;
      for(i = 0; i < (w & ~(size_t)3U); i += 4U)
      {
        uint32_t val0 = *src++;
        uint32_t val1 = *src++;
        uint32_t val2 = *src++;
        uint32_t val3 = *src++;
        uint32_t val4 = *src++;

        dest[i + 0] =
            sign_extend<T>(INV((T)((val0 << 2) | (val1 >> 6)), INV_MASK_10, invert), 32 - 10);
        dest[i + 1] = sign_extend<T>(
            INV((T)(((val1 & 0x3FU) << 4) | (val2 >> 4)), INV_MASK_10, invert), 32 - 10);
        dest[i + 2] = sign_extend<T>(
            INV((T)(((val2 & 0xFU) << 6) | (val3 >> 2)), INV_MASK_10, invert), 32 - 10);
        dest[i + 3] =
            sign_extend<T>(INV((T)(((val3 & 0x3U) << 8) | val4), INV_MASK_10, invert), 32 - 10);
      }
      if(w & 3U)
      {
        uint32_t val0 = *src++;
        uint32_t val1 = *src++;
        w = w & 3U;
        dest[i + 0] =
            sign_extend<T>(INV((T)((val0 << 2) | (val1 >> 6)), INV_MASK_10, invert), 32 - 10);

        if(w > 1U)
        {
          uint32_t val2 = *src++;
          dest[i + 1] = sign_extend<T>(
              INV((T)(((val1 & 0x3FU) << 4) | (val2 >> 4)), INV_MASK_10, invert), 32 - 10);
          if(w > 2U)
          {
            uint32_t val3 = *src++;
            dest[i + 2] = sign_extend<T>(
                INV((T)(((val2 & 0xFU) << 6) | (val3 >> 2)), INV_MASK_10, invert), 32 - 10);
          }
        }
      }
    }
    else
    {
      size_t i;
      for(i = 0; i < (w & ~(size_t)3U); i += 4U)
      {
        uint32_t val0 = *src++;
        uint32_t val1 = *src++;
        uint32_t val2 = *src++;
        uint32_t val3 = *src++;
        uint32_t val4 = *src++;

        dest[i + 0] = INV((T)((val0 << 2) | (val1 >> 6)), INV_MASK_10, invert);
        dest[i + 1] = INV((T)(((val1 & 0x3FU) << 4) | (val2 >> 4)), INV_MASK_10, invert);
        dest[i + 2] = INV((T)(((val2 & 0xFU) << 6) | (val3 >> 2)), INV_MASK_10, invert);
        dest[i + 3] = INV((T)(((val3 & 0x3U) << 8) | val4), INV_MASK_10, invert);
      }
      if(w & 3U)
      {
        uint32_t val0 = *src++;
        uint32_t val1 = *src++;
        w = w & 3U;
        dest[i + 0] = INV((T)((val0 << 2) | (val1 >> 6)), INV_MASK_10, invert);

        if(w > 1U)
        {
          uint32_t val2 = *src++;
          dest[i + 1] = INV((T)(((val1 & 0x3FU) << 4) | (val2 >> 4)), INV_MASK_10, invert);
          if(w > 2U)
          {
            uint32_t val3 = *src++;
            dest[i + 2] = INV((T)(((val2 & 0xFU) << 6) | (val3 >> 2)), INV_MASK_10, invert);
          }
        }
      }
    }
  }
  else if constexpr(N == 11)
  {
    size_t i;
    for(i = 0; i < (w & ~(size_t)7U); i += 8U)
    {
      uint32_t val0 = *src++;
      uint32_t val1 = *src++;
      uint32_t val2 = *src++;
      uint32_t val3 = *src++;
      uint32_t val4 = *src++;
      uint32_t val5 = *src++;
      uint32_t val6 = *src++;
      uint32_t val7 = *src++;
      uint32_t val8 = *src++;
      uint32_t val9 = *src++;
      uint32_t val10 = *src++;

      dest[i + 0] = INV((T)((val0 << 3) | (val1 >> 5)), INV_MASK_11, invert);
      dest[i + 1] = INV((T)(((val1 & 0x1FU) << 6) | (val2 >> 2)), INV_MASK_11, invert);
      dest[i + 2] = INV((T)(((val2 & 0x3U) << 9) | (val3 << 1) | (val4 >> 7)), INV_MASK_11, invert);
      dest[i + 3] = INV((T)(((val4 & 0x7FU) << 4) | (val5 >> 4)), INV_MASK_11, invert);
      dest[i + 4] = INV((T)(((val5 & 0xFU) << 7) | (val6 >> 1)), INV_MASK_11, invert);
      dest[i + 5] =
          INV((T)(((val6 & 0x1U) << 10) | (val7 << 2) | (val8 >> 6)), INV_MASK_11, invert);
      dest[i + 6] = INV((T)(((val8 & 0x3FU) << 5) | (val9 >> 3)), INV_MASK_11, invert);
      dest[i + 7] = INV((T)(((val9 & 0x7U) << 8) | (val10)), INV_MASK_11, invert);
    }
    w = w & 7U;
    if(w)
    {
      uint32_t val = 0;
      uint8_t available = 0;
      for(size_t j = 0; j < w; ++j)
        GETBITS(dest[i + j], 11, INV_MASK_11, invert)
    }
  }
  else if constexpr(N == 12)
  {
    if(isSigned)
    {
      size_t i;
      for(i = 0; i < (w & ~(size_t)1U); i += 2U)
      {
        uint32_t val0 = *src++;
        uint32_t val1 = *src++;
        uint32_t val2 = *src++;

        dest[i + 0] =
            sign_extend<T>(INV((T)((val0 << 4) | (val1 >> 4)), INV_MASK_12, invert), 32 - 12);
        dest[i + 1] =
            sign_extend<T>(INV((T)(((val1 & 0xFU) << 8) | val2), INV_MASK_12, invert), 32 - 12);
      }
      if(w & 1U)
      {
        uint32_t val0 = *src++;
        uint32_t val1 = *src++;
        dest[i + 0] =
            sign_extend<T>(INV((T)((val0 << 4) | (val1 >> 4)), INV_MASK_12, invert), 32 - 12);
      }
    }
    else
    {
      size_t i;
      for(i = 0; i < (w & ~(size_t)1U); i += 2U)
      {
        uint32_t val0 = *src++;
        uint32_t val1 = *src++;
        uint32_t val2 = *src++;

        dest[i + 0] = INV((T)((val0 << 4) | (val1 >> 4)), INV_MASK_12, invert);
        dest[i + 1] = INV((T)(((val1 & 0xFU) << 8) | val2), INV_MASK_12, invert);
      }
      if(w & 1U)
      {
        uint32_t val0 = *src++;
        uint32_t val1 = *src++;
        dest[i + 0] = INV((T)((val0 << 4) | (val1 >> 4)), INV_MASK_12, invert);
      }
    }
  }
  else if constexpr(N == 13)
  {
    size_t i;
    for(i = 0; i < (w & ~(size_t)7U); i += 8U)
    {
      uint32_t val0 = *src++;
      uint32_t val1 = *src++;
      uint32_t val2 = *src++;
      uint32_t val3 = *src++;
      uint32_t val4 = *src++;
      uint32_t val5 = *src++;
      uint32_t val6 = *src++;
      uint32_t val7 = *src++;
      uint32_t val8 = *src++;
      uint32_t val9 = *src++;
      uint32_t val10 = *src++;
      uint32_t val11 = *src++;
      uint32_t val12 = *src++;

      dest[i + 0] = INV((T)((val0 << 5) | (val1 >> 3)), INV_MASK_13, invert);
      dest[i + 1] =
          INV((T)(((val1 & 0x7U) << 10) | (val2 << 2) | (val3 >> 6)), INV_MASK_13, invert);
      dest[i + 2] = INV((T)(((val3 & 0x3FU) << 7) | (val4 >> 1)), INV_MASK_13, invert);
      dest[i + 3] =
          INV((T)(((val4 & 0x1U) << 12) | (val5 << 4) | (val6 >> 4)), INV_MASK_13, invert);
      dest[i + 4] = INV((T)(((val6 & 0xFU) << 9) | (val7 << 1) | (val8 >> 7)), INV_MASK_13, invert);
      dest[i + 5] = INV((T)(((val8 & 0x7FU) << 6) | (val9 >> 2)), INV_MASK_13, invert);
      dest[i + 6] =
          INV((T)(((val9 & 0x3U) << 11) | (val10 << 3) | (val11 >> 5)), INV_MASK_13, invert);
      dest[i + 7] = INV((T)(((val11 & 0x1FU) << 8) | (val12)), INV_MASK_13, invert);
    }
    w = w & 7U;
    if(w)
    {
      uint32_t val = 0;
      uint8_t available = 0;
      for(size_t j = 0; j < w; ++j)
        GETBITS(dest[i + j], 13, INV_MASK_13, invert)
    }
  }
  else if constexpr(N == 14)
  {
    size_t i;
    for(i = 0; i < (w & ~(size_t)3U); i += 4U)
    {
      uint32_t val0 = *src++;
      uint32_t val1 = *src++;
      uint32_t val2 = *src++;
      uint32_t val3 = *src++;
      uint32_t val4 = *src++;
      uint32_t val5 = *src++;
      uint32_t val6 = *src++;

      dest[i + 0] = INV((T)((val0 << 6) | (val1 >> 2)), INV_MASK_14, invert);
      dest[i + 1] =
          INV((T)(((val1 & 0x3U) << 12) | (val2 << 4) | (val3 >> 4)), INV_MASK_14, invert);
      dest[i + 2] =
          INV((T)(((val3 & 0xFU) << 10) | (val4 << 2) | (val5 >> 6)), INV_MASK_14, invert);
      dest[i + 3] = INV((T)(((val5 & 0x3FU) << 8) | val6), INV_MASK_14, invert);
    }
    if(w & 3U)
    {
      uint32_t val0 = *src++;
      uint32_t val1 = *src++;
      w = w & 3U;
      dest[i + 0] = (T)((val0 << 6) | (val1 >> 2));

      if(w > 1U)
      {
        uint32_t val2 = *src++;
        uint32_t val3 = *src++;
        dest[i + 1] =
            INV((T)(((val1 & 0x3U) << 12) | (val2 << 4) | (val3 >> 4)), INV_MASK_14, invert);
        if(w > 2U)
        {
          uint32_t val4 = *src++;
          uint32_t val5 = *src++;
          dest[i + 2] =
              INV((T)(((val3 & 0xFU) << 10) | (val4 << 2) | (val5 >> 6)), INV_MASK_14, invert);
        }
      }
    }
  }
  else if constexpr(N == 15)
  {
    size_t i;
    for(i = 0; i < (w & ~(size_t)7U); i += 8U)
    {
      uint32_t val0 = *src++;
      uint32_t val1 = *src++;
      uint32_t val2 = *src++;
      uint32_t val3 = *src++;
      uint32_t val4 = *src++;
      uint32_t val5 = *src++;
      uint32_t val6 = *src++;
      uint32_t val7 = *src++;
      uint32_t val8 = *src++;
      uint32_t val9 = *src++;
      uint32_t val10 = *src++;
      uint32_t val11 = *src++;
      uint32_t val12 = *src++;
      uint32_t val13 = *src++;
      uint32_t val14 = *src++;

      dest[i + 0] = INV((T)((val0 << 7) | (val1 >> 1)), (1 << 15) - 1, invert);
      dest[i + 1] =
          INV((T)(((val1 & 0x1U) << 14) | (val2 << 6) | (val3 >> 2)), INV_MASK_15, invert);
      dest[i + 2] =
          INV((T)(((val3 & 0x3U) << 13) | (val4 << 5) | (val5 >> 3)), INV_MASK_15, invert);
      dest[i + 3] =
          INV((T)(((val5 & 0x7U) << 12) | (val6 << 4) | (val7 >> 4)), INV_MASK_15, invert);
      dest[i + 4] =
          INV((T)(((val7 & 0xFU) << 11) | (val8 << 3) | (val9 >> 5)), INV_MASK_15, invert);
      dest[i + 5] =
          INV((T)(((val9 & 0x1FU) << 10) | (val10 << 2) | (val11 >> 6)), INV_MASK_15, invert);
      dest[i + 6] =
          INV((T)(((val11 & 0x3FU) << 9) | (val12 << 1) | (val13 >> 7)), INV_MASK_15, invert);
      dest[i + 7] = INV((T)(((val13 & 0x7FU) << 8) | (val14)), INV_MASK_15, invert);
    }
    w = w & 7U;
    if(w)
    {
      uint32_t val = 0;
      uint8_t available = 0;
      for(size_t j = 0; j < w; ++j)
        GETBITS(dest[i + j], 15, INV_MASK_15, invert)
    }
  }
  else if constexpr(N == 16)
  {
#ifdef GROK_BIG_ENDIAN
    const uint16_t* typedSrc = (uint16_t*)src;
    for(size_t i = 0; i < w; i++)
      dest[i] = INV(typedSrc[i], 0xFFFF, invert);
#else
    // convert from big endian (PNG)
    size_t i;
    for(i = 0; i < w; i++)
    {
      T val0 = *src++;
      T val1 = *src++;
      dest[i] = INV(val0 << 8 | val1, 0xFFFF, invert);
    }
#endif
  }
}

template<size_t N, bool isSigned, typename T>
void convertToOutputTIFF(const uint8_t* src, T* dest, size_t w, bool invert)
{
  if constexpr(N != 16)
  {
    convertToOutput<N, isSigned, T>(src, dest, w, invert);
  }
  else
  {
    /* libtiff decodes to machine endianness */
    const uint16_t* typedSrc = (uint16_t*)src;
    for(size_t i = 0; i < w; i++)
      dest[i] = INV(typedSrc[i], 0xFFFF, invert);
  }
}

template<typename T>
void scaleComponent(grk_image_comp* component, uint8_t precision)
{
  if(component->prec == precision)
    return;
  uint32_t stride_diff = component->stride - component->w;
  auto data = (T*)component->data;
  if(component->prec < precision)
  {
    T scale = 1 << (uint32_t)(precision - component->prec);
    size_t index = 0;
    for(uint32_t j = 0; j < component->h; ++j)
    {
      for(uint32_t i = 0; i < component->w; ++i)
        data[index++] *= scale;
      index += stride_diff;
    }
  }
  else
  {
    T scale = 1 << (uint32_t)(component->prec - precision);
    size_t index = 0;
    for(uint32_t j = 0; j < component->h; ++j)
    {
      for(uint32_t i = 0; i < component->w; ++i)
        data[index++] /= scale;
      index += stride_diff;
    }
  }
  component->prec = precision;
}