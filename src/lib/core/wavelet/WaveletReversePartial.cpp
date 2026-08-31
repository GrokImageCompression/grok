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

#include <algorithm>
#include <functional>

#include "TFSingleton.h"
#include "grk_restrict.h"
#include "hwy_arm_disable_targets.h"
#include <hwy/highway.h>
#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "Logger.h"
#include "buffer.h"
#include "GrkObjectWrapper.h"
#include "ISparseCanvas.h"
#include "ImageComponentFlow.h"
#include "IStream.h"
#include "GrkImageMeta.h"
#include "GrkImage.h"
#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct ITileProcessor;
}
#include "CodingParams.h"

#include "ICoder.h"
#include "CoderPool.h"
#include "BitIO.h"
#include "TagTree.h"
#include "CodeblockCompress.h"
#include "CodeblockDecompress.h"
#include "Precinct.h"
#include "Subband.h"
#include "Resolution.h"
#include "TileComponentWindow.h"
#include "WaveletReverse.h"
#include "TileComponent.h"
#include "DecompressScheduler.h"

namespace grk
{

/*************************************************************************************
 *
 * Partial 5/3 or 9/7 Inverse Wavelet
 *
 **************************************************************************************
 *
 *
 * ST is the lifting element type, CT the sparse canvas sample type:
 * 5/3 lifts on int32_t or int16_t over a canvas of the same type, 9/7 lifts on
 * vec4f over an int32_t canvas.
 *
 * Horizontal pass
 *
 * Each thread processes a strip running the length of the window, with height
 *   sizeof(ST)/sizeof(CT)
 *
 * Vertical pass
 *
 * Each thread processes a strip running the height of the window, with width
 * VERT_PASS_WIDTH
 *
 ****************************************************************************/
template<typename ST, typename CT, uint32_t FILTER_WIDTH, uint32_t VERT_PASS_WIDTH>
class PartialInterleaver
{
public:
  bool interleave_h(dwt_scratch<ST>* dwt, ISparseCanvas<CT>* sa, uint32_t y_offset, uint32_t height)
  {
    const uint32_t stripHeight = (uint32_t)(sizeof(ST) / sizeof(CT));
    for(uint32_t y = 0; y < height; y++)
    {
      // read one row of L band
      if(dwt->sn)
      {
        bool ret = sa->read(dwt->resno,
                            Rect32(dwt->win_l.x0, y_offset + y,
                                   std::min<uint32_t>(dwt->win_l.x1 + FILTER_WIDTH, dwt->sn),
                                   y_offset + y + 1),
                            (CT*)dwt->memL + y, 2 * stripHeight, 0);
        if(!ret)
          return false;
      }
      // read one row of H band
      if(dwt->dn)
      {
        bool ret =
            sa->read(dwt->resno,
                     Rect32(dwt->sn + dwt->win_h.x0, y_offset + y,
                            dwt->sn + std::min<uint32_t>(dwt->win_h.x1 + FILTER_WIDTH, dwt->dn),
                            y_offset + y + 1),
                     (CT*)dwt->memH + y, 2 * stripHeight, 0);
        if(!ret)
          return false;
      }
    }

    return true;
  }
  bool interleave_v(dwt_scratch<ST>* GRK_RESTRICT dwt, ISparseCanvas<CT>* sa, uint32_t x_offset,
                    uint32_t xWidth)
  {
    const uint32_t stripWidth = (sizeof(ST) / sizeof(CT)) * VERT_PASS_WIDTH;
    // read one vertical strip (of width xWidth <= stripWidth) of L band
    bool ret = false;
    if(dwt->sn)
    {
      ret = sa->read(dwt->resno,
                     Rect32(x_offset, dwt->win_l.x0, x_offset + xWidth,
                            std::min<uint32_t>(dwt->win_l.x1 + FILTER_WIDTH, dwt->sn)),
                     (CT*)dwt->memL, 1, 2 * stripWidth);
    }
    // read one vertical strip (of width x_num_elements <= stripWidth) of H band
    if(dwt->dn)
    {
      ret = sa->read(dwt->resno,
                     Rect32(x_offset, dwt->sn + dwt->win_h.x0, x_offset + xWidth,
                            dwt->sn + std::min<uint32_t>(dwt->win_h.x1 + FILTER_WIDTH, dwt->dn)),
                     (CT*)dwt->memH, 1, 2 * stripWidth);
    }

    return ret;
  }
};

// static (baseline-target) highway: the strips below are fixed at one 128-bit
// vector by VERT_PASS_WIDTH, so there is nothing for dynamic dispatch to widen
namespace hn = hwy::HWY_NAMESPACE;

// floor((a + b + 2) / 4) for signed int16 lanes, mirrors update_avg_16_53 in
// WaveletReverse.cpp. the direct sum can overflow int16, so this goes through
// the unsigned average instruction, which carries a 17-bit intermediate.
// identity: floor((a + b + 2) / 4) == (floor((a + b) / 2) + 1) >> 1
template<class D, class V>
static HWY_INLINE V update_avg_16_53(D di, V a, V b)
{
  const hn::RebindToUnsigned<D> du;
  const auto u_sign = hn::Set(du, (uint16_t)0x8000u);
  const auto u_max = hn::Set(du, (uint16_t)0x7FFFu);
  // flip the sign bit to reach the unsigned domain, and bias b down by one so the
  // rounding +1 in AverageRound cancels
  auto a_u = hn::BitCast(du, a) ^ u_sign;
  auto b_biased = hn::BitCast(du, b) + u_max;
  auto avg = hn::AverageRound(a_u, b_biased);
  auto step = hn::BitCast(di, avg - u_max);
  return hn::ShiftRight<1>(step);
}

// floor((a + b) / 2) for signed int16 lanes, mirrors predict_avg_16_53 in
// WaveletReverse.cpp, avoiding the overflow in a + b.
// identity: (a + b) >> 1 == (a >> 1) + (b >> 1) + ((a & b) & 1)
template<class D, class V>
static HWY_INLINE V predict_avg_16_53(D di, V a, V b)
{
  return hn::ShiftRight<1>(a) + hn::ShiftRight<1>(b) +
         hn::And(hn::And(a, b), hn::Set(di, (int16_t)1));
}

// vertical 5/3 update step (low pass) over the middle rows, no bound checking:
// two rows per iteration across one VERT_PASS_WIDTH strip of columns.
// returns the first unprocessed row
template<typename ST, uint32_t VERT_PASS_WIDTH>
static int64_t hwy_partial_v_update_53(ST* buf, const int64_t i_start, const int64_t i_max)
{
  if(i_start + 1 >= i_max)
    return i_start;
  const HWY_CAPPED(ST, VERT_PASS_WIDTH) d;
  [[maybe_unused]] const auto two = hn::Set(d, (ST)2);
  int64_t i = i_start;
  for(size_t off = 0; off < VERT_PASS_WIDTH; off += hn::Lanes(d))
  {
    i = i_start;
    auto Dm1 = hn::Load(d, buf + ((i << 1) - 1) * VERT_PASS_WIDTH + off);
    for(; i + 1 < i_max; i += 2)
    {
      auto S = hn::Load(d, buf + (i << 1) * VERT_PASS_WIDTH + off);
      auto D = hn::Load(d, buf + ((i << 1) + 1) * VERT_PASS_WIDTH + off);
      auto S1 = hn::Load(d, buf + ((i << 1) + 2) * VERT_PASS_WIDTH + off);
      auto D1 = hn::Load(d, buf + ((i << 1) + 3) * VERT_PASS_WIDTH + off);
      if constexpr(std::is_same_v<ST, int16_t>)
      {
        S = S - update_avg_16_53(d, Dm1, D);
        S1 = S1 - update_avg_16_53(d, D, D1);
      }
      else
      {
        S = S - hn::ShiftRight<2>(Dm1 + D + two);
        S1 = S1 - hn::ShiftRight<2>(D + D1 + two);
      }
      hn::Store(S, d, buf + (i << 1) * VERT_PASS_WIDTH + off);
      hn::Store(S1, d, buf + ((i + 1) << 1) * VERT_PASS_WIDTH + off);
      Dm1 = D1;
    }
  }
  return i;
}

// vertical 5/3 predict step (high pass) over the middle rows, same contract as
// hwy_partial_v_update_53
template<typename ST, uint32_t VERT_PASS_WIDTH>
static int64_t hwy_partial_v_predict_53(ST* buf, const int64_t i_start, const int64_t i_max)
{
  if(i_start + 1 >= i_max)
    return i_start;
  const HWY_CAPPED(ST, VERT_PASS_WIDTH) d;
  int64_t i = i_start;
  for(size_t off = 0; off < VERT_PASS_WIDTH; off += hn::Lanes(d))
  {
    i = i_start;
    auto S = hn::Load(d, buf + (i << 1) * VERT_PASS_WIDTH + off);
    for(; i + 1 < i_max; i += 2)
    {
      auto D = hn::Load(d, buf + (1 + (i << 1)) * VERT_PASS_WIDTH + off);
      auto S1 = hn::Load(d, buf + ((i + 1) << 1) * VERT_PASS_WIDTH + off);
      auto D1 = hn::Load(d, buf + (1 + ((i + 1) << 1)) * VERT_PASS_WIDTH + off);
      auto S2 = hn::Load(d, buf + ((i + 2) << 1) * VERT_PASS_WIDTH + off);
      if constexpr(std::is_same_v<ST, int16_t>)
      {
        D = D + predict_avg_16_53(d, S, S1);
        D1 = D1 + predict_avg_16_53(d, S1, S2);
      }
      else
      {
        D = D + hn::ShiftRight<1>(S + S1);
        D1 = D1 + hn::ShiftRight<1>(S1 + S2);
      }
      hn::Store(D, d, buf + (1 + (i << 1)) * VERT_PASS_WIDTH + off);
      hn::Store(D1, d, buf + (1 + ((i + 1) << 1)) * VERT_PASS_WIDTH + off);
      S = S2;
    }
  }
  return i;
}

// 5/3 inverse lifting steps below compute sums of two samples before an
// arithmetic right shift. With int32_t samples, JPEG 2000-compliant inputs
// stay well within range, but fuzzer-crafted coefficients can push the
// intermediate sum past INT32_MAX/MIN and trip UBSan. Widening the sum to
// int64_t before the shift keeps the arithmetic-shift semantics intact
// without changing output for in-range inputs. The SIMD paths use
// wrapping vector adds and are unaffected.
template<typename ST, typename CT, uint32_t FILTER_WIDTH, uint32_t VERT_PASS_WIDTH>
class Partial53 : public PartialInterleaver<ST, CT, FILTER_WIDTH, VERT_PASS_WIDTH>
{
public:
  GRK_NO_SANITIZE_OVERFLOW void h(dwt_scratch<ST>* dwt)
  {
#ifndef GRK_DEBUG_SPARSE
#define get_S(buf, i) buf[(i) << 1]
#define get_D(buf, i) buf[(1 + ((i) << 1))]
#endif

#define S(buf, i) buf[(i) << 1]
#define D(buf, i) buf[(1 + ((i) << 1))]

// parity == 0
#define S_(buf, i) \
  ((i) < -win_l_x0 ? get_S(buf, -win_l_x0) : ((i) >= sn_p ? get_S(buf, sn_p - 1) : get_S(buf, i)))
#define D_(buf, i) \
  ((i) < -win_h_x0 ? get_D(buf, -win_h_x0) : ((i) >= dn_p ? get_D(buf, dn_p - 1) : get_D(buf, i)))

// parity == 1
#define SS_(buf, i) \
  ((i) < -win_h_x0 ? get_S(buf, -win_h_x0) : ((i) >= dn_p ? get_S(buf, dn_p - 1) : get_S(buf, i)))
#define DD_(buf, i) \
  ((i) < -win_l_x0 ? get_D(buf, -win_l_x0) : ((i) >= sn_p ? get_D(buf, sn_p - 1) : get_D(buf, i)))

    int64_t i;
    const int64_t parity = dwt->parity;
    const int64_t win_l_x0 = dwt->win_l.x0;
    const int64_t win_l_x1 = dwt->win_l.x1;
    const int64_t win_h_x0 = dwt->win_h.x0;
    const int64_t win_h_x1 = dwt->win_h.x1;
    assert(dwt->win_l.x0 <= dwt->sn);
    int64_t sn_p = (int64_t)dwt->sn - (int64_t)dwt->win_l.x0;
    const int64_t sn = dwt->sn;
    assert(dwt->win_h.x0 <= dwt->dn);
    int64_t dn_p = (int64_t)dwt->dn - (int64_t)dwt->win_h.x0;
    const int64_t dn = dwt->dn;

    adjust_bounds(dwt, sn, dn, &sn_p, &dn_p);

    assert(dwt->win_l.x1 <= sn && dwt->win_h.x1 <= dn);

    auto buf = dwt->mem;
    if(!parity)
    {
      if((dn != 0) || (sn > 1))
      {
        /* Naive version is :
        for (i = win_l_x0; i < i_max; i++) {
          S(i) -= (D_(i - 1) + D_(i) + 2) >> 2;
        }
        for (i = win_h_x0; i < win_h_x1; i++) {
          D(i) += (S_(i) + S_(i + 1)) >> 1;
        }
        but the compiler doesn't manage to unroll it to avoid bound
        checking in S_ and D_ macros
        */
        i = 0;
        int64_t i_max = win_l_x1 - win_l_x0;
        if(i < i_max)
        {
          /* Left-most case */
          S(buf, i) -= (ST)(((int64_t)D_(buf, i - 1) + D_(buf, i) + 2) >> 2);
          i++;

          if(i_max > dn_p)
            i_max = dn_p;
          for(; i < i_max; i++)
            /* No bound checking */
            S(buf, i) -= (ST)(((int64_t)get_D(buf, i - 1) + get_D(buf, i) + 2) >> 2);
          for(; i < win_l_x1 - win_l_x0; i++)
            /* Right-most case */
            S(buf, i) -= (ST)(((int64_t)D_(buf, i - 1) + D_(buf, i) + 2) >> 2);
        }
        i = 0;
        i_max = win_h_x1 - win_h_x0;
        if(i < i_max)
        {
          if(i_max >= sn_p)
            i_max = sn_p - 1;
          for(; i < i_max; i++)
            /* No bound checking */
            D(buf, i) += (ST)(((int64_t)S(buf, i) + S(buf, i + 1)) >> 1);
          for(; i < win_h_x1 - win_h_x0; i++)
            /* Right-most case */
            D(buf, i) += (ST)(((int64_t)S_(buf, i) + S_(buf, i + 1)) >> 1);
        }
      }
    }
    else
    {
      if(sn == 0 && dn == 1)
      {
        // only do L band (high pass)
        S(buf, 0) >>= 1;
      }
      else
      {
        for(i = 0; i < win_l_x1 - win_l_x0; i++)
          D(buf, i) -= (ST)(((int64_t)SS_(buf, i) + SS_(buf, i + 1) + 2) >> 2);
        for(i = 0; i < win_h_x1 - win_h_x0; i++)
          S(buf, i) += (ST)(((int64_t)DD_(buf, i) + DD_(buf, i - 1)) >> 1);
      }
    }
  }
  GRK_NO_SANITIZE_OVERFLOW void v(dwt_scratch<ST>* dwt)
  {
#ifndef GRK_DEBUG_SPARSE
#define get_S_off(buf, i, off) buf[((i) << 1) * VERT_PASS_WIDTH + off]
#define get_D_off(buf, i, off) buf[(1 + ((i) << 1)) * VERT_PASS_WIDTH + off]
#endif

#define S_off(buf, i, off) buf[((i) << 1) * VERT_PASS_WIDTH + off]
#define D_off(buf, i, off) buf[(1 + ((i) << 1)) * VERT_PASS_WIDTH + off]

// parity == 0
#define S_off_(buf, i, off) (((i) >= sn_p ? get_S_off(buf, sn_p - 1, off) : get_S_off(buf, i, off)))
#define D_off_(buf, i, off) (((i) >= dn_p ? get_D_off(buf, dn_p - 1, off) : get_D_off(buf, i, off)))

#define S_sgnd_off_(buf, i, off) \
  (((i) < (-win_l_x0) ? get_S_off(buf, -win_l_x0, off) : S_off_(buf, i, off)))
#define D_sgnd_off_(buf, i, off) \
  (((i) < (-win_h_x0) ? get_D_off(buf, -win_h_x0, off) : D_off_(buf, i, off)))

// case == 1
#define SS_sgnd_off_(buf, i, off)                     \
  ((i) < (-win_h_x0) ? get_S_off(buf, -win_h_x0, off) \
                     : ((i) >= dn_p ? get_S_off(buf, dn_p - 1, off) : get_S_off(buf, i, off)))
#define DD_sgnd_off_(buf, i, off)                     \
  ((i) < (-win_l_x0) ? get_D_off(buf, -win_l_x0, off) \
                     : ((i) >= sn_p ? get_D_off(buf, sn_p - 1, off) : get_D_off(buf, i, off)))

#define SS_off_(buf, i, off) \
  (((i) >= dn_p ? get_S_off(buf, dn_p - 1, off) : get_S_off(buf, i, off)))
#define DD_off_(buf, i, off) \
  (((i) >= sn_p ? get_D_off(buf, sn_p - 1, off) : get_D_off(buf, i, off)))

    int64_t i;
    const int64_t parity = dwt->parity;
    const int64_t win_l_x0 = dwt->win_l.x0;
    const int64_t win_l_x1 = dwt->win_l.x1;
    const int64_t win_h_x0 = dwt->win_h.x0;
    const int64_t win_h_x1 = dwt->win_h.x1;
    int64_t sn_p = (int64_t)dwt->sn - (int64_t)dwt->win_l.x0;
    const int64_t sn = dwt->sn;
    int64_t dn_p = (int64_t)dwt->dn - (int64_t)dwt->win_h.x0;
    const int64_t dn = dwt->dn;

    adjust_bounds(dwt, sn, dn, &sn_p, &dn_p);

    assert(dwt->win_l.x1 <= sn && dwt->win_h.x1 <= dn);

    auto buf = dwt->mem;
    if(!parity)
    {
      if((dn != 0) || (sn > 1))
      {
        /* Naive version is :
        for (i = win_l_x0; i < i_max; i++) {
          S(i) -= (D_(i - 1) + D_(i) + 2) >> 2;
        }
        for (i = win_h_x0; i < win_h_x1; i++) {
          D(i) += (S_(i) + S_(i + 1)) >> 1;
        }
        but the compiler doesn't manage to unroll it to avoid bound
        checking in S_ and D_ macros
        */

        // 1. low pass
        i = 0;
        int64_t i_max = win_l_x1 - win_l_x0;
        assert(win_l_x1 >= win_l_x0);
        if(i < i_max)
        {
          /* Left-most case */
          for(int64_t off = 0; off < VERT_PASS_WIDTH; off++)
            S_off(buf, i, off) -=
                (ST)(((int64_t)D_sgnd_off_(buf, i - 1, off) + D_off_(buf, i, off) + 2) >> 2);
          i++;
          if(i_max > dn_p)
            i_max = dn_p;
          i = hwy_partial_v_update_53<ST, VERT_PASS_WIDTH>(buf, i, i_max);
          for(; i < i_max; i++)
          {
            /* No bound checking */
            for(uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
              S_off(buf, i, off) -=
                  (ST)(((int64_t)D_sgnd_off_(buf, i - 1, off) + D_off(buf, i, off) + 2) >> 2);
          }
          for(; i < win_l_x1 - win_l_x0; i++)
          {
            /* Right-most case */
            for(uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
              S_off(buf, i, off) -=
                  (ST)(((int64_t)D_sgnd_off_(buf, i - 1, off) + D_off_(buf, i, off) + 2) >> 2);
          }
        }

        // 2. high pass
        i = 0;
        assert(win_h_x1 >= win_h_x0);
        i_max = win_h_x1 - win_h_x0;
        if(i < i_max)
        {
          if(i_max >= sn_p)
            i_max = sn_p - 1;
          i = hwy_partial_v_predict_53<ST, VERT_PASS_WIDTH>(buf, i, i_max);
          for(; i < i_max; i++)
          {
            /* No bound checking */
            for(uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
              D_off(buf, i, off) +=
                  (ST)(((int64_t)S_off(buf, i, off) + S_off(buf, i + 1, off)) >> 1);
          }
          for(; i < win_h_x1 - win_h_x0; i++)
          {
            /* Right-most case */
            for(uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
              D_off(buf, i, off) +=
                  (ST)(((int64_t)S_off_(buf, i, off) + S_off_(buf, i + 1, off)) >> 1);
          }
        }
      }
    }
    else
    {
      if(sn == 0 && dn == 1)
      {
        // edge case at origin
        for(uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
          S_off(buf, 0, off) >>= 1;
      }
      else
      {
        assert((uint64_t)(dwt->memL + (win_l_x1 - win_l_x0) * VERT_PASS_WIDTH) -
                   (uint64_t)dwt->allocatedMem <
               dwt->lenBytes_);
        for(i = 0; i < win_l_x1 - win_l_x0; i++)
        {
          for(uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
            D_off(buf, i, off) -=
                (ST)(((int64_t)SS_off_(buf, i, off) + SS_off_(buf, i + 1, off) + 2) >> 2);
        }
        assert((uint64_t)(dwt->memH + (win_h_x1 - win_h_x0) * VERT_PASS_WIDTH) -
                   (uint64_t)dwt->allocatedMem <
               dwt->lenBytes_);
        for(i = 0; i < win_h_x1 - win_h_x0; i++)
        {
          for(uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
            S_off(buf, i, off) +=
                (ST)(((int64_t)DD_off_(buf, i, off) + DD_sgnd_off_(buf, i - 1, off)) >> 1);
        }
      }
    }
  }

private:
  void adjust_bounds(const dwt_scratch<ST>* dwt, [[maybe_unused]] int64_t sn,
                     [[maybe_unused]] int64_t dn, int64_t* sn_p, int64_t* dn_p)
  {
    if((uint64_t)dwt->memH < (uint64_t)dwt->memL && *sn_p == *dn_p)
    {
      assert(dn == sn - 1);
      (*dn_p)--;
    }
    if((uint64_t)dwt->memL < (uint64_t)dwt->memH && *sn_p == *dn_p)
    {
      assert(sn == dn - 1);
      (*sn_p)--;
    }
  }
#ifdef GRK_DEBUG_SPARSE
  inline T get_S(const T* const buf, int64_t i)
  {
    auto ret = buf[(i) << 1];
    assert(abs(ret) < 0xFFFFFFF);
    return ret;
  }
  inline T get_D(const T* const buf, int64_t i)
  {
    auto ret = buf[(1 + ((i) << 1))];
    assert(abs(ret) < 0xFFFFFFF);
    return ret;
  }
  inline T get_S_off(const T* const buf, int64_t i, int64_t off)
  {
    auto ret = buf[(i) * 2 * VERT_PASS_WIDTH + off];
    assert(abs(ret) < 0xFFFFFFF);
    return ret;
  }
  inline T get_D_off(const T* const buf, int64_t i, int64_t off)
  {
    auto ret = buf[(1 + (i) * 2) * VERT_PASS_WIDTH + off];
    assert(abs(ret) < 0xFFFFFFF);
    return ret;
  }
#endif
};

template<typename T, typename CT, uint32_t FILTER_WIDTH, uint32_t VERT_PASS_WIDTH>
class Partial97 : public PartialInterleaver<T, CT, FILTER_WIDTH, VERT_PASS_WIDTH>
{
public:
  void h(dwt_scratch<T>* dwt)
  {
    WaveletReverse::step_97(dwt);
  }
  void v(dwt_scratch<T>* dwt)
  {
    WaveletReverse::step_97(dwt);
  }
};

template<typename T, typename CT, uint32_t FILTER_WIDTH, uint32_t VERT_PASS_WIDTH>
class Partial97_16 : public PartialInterleaver<T, CT, FILTER_WIDTH, VERT_PASS_WIDTH>
{
public:
  void h(dwt_scratch<T>* dwt)
  {
    WaveletReverse::step_16_97(dwt);
  }
  void v(dwt_scratch<T>* dwt)
  {
    WaveletReverse::step_16_97(dwt);
  }
};

template<typename CT, uint32_t FILTER_WIDTH>
struct PartialBandInfo
{
  // 1. set up windows for horizontal and vertical passes
  Rect32 bandWindowREL_[t1::BAND_NUM_ORIENTATIONS];
  // two windows formed by horizontal pass and used as input for vertical pass
  Rect32 splitWindowREL_[SPLIT_NUM_ORIENTATIONS];
  Rect32 resWindowREL_;

  bool alloc(ISparseCanvas<CT>* sa, uint8_t resno, Resolution* fullRes,
             TileComponentWindow<CT>* tileWindow)
  {
    bandWindowREL_[t1::BAND_ORIENT_LL] =
        tileWindow->getBandWindowBufferPaddedREL(resno, t1::BAND_ORIENT_LL);
    bandWindowREL_[t1::BAND_ORIENT_HL] =
        tileWindow->getBandWindowBufferPaddedREL(resno, t1::BAND_ORIENT_HL);
    bandWindowREL_[t1::BAND_ORIENT_LH] =
        tileWindow->getBandWindowBufferPaddedREL(resno, t1::BAND_ORIENT_LH);
    bandWindowREL_[t1::BAND_ORIENT_HH] =
        tileWindow->getBandWindowBufferPaddedREL(resno, t1::BAND_ORIENT_HH);

    // band windows in band coordinates - needed to pre-allocate sparse blocks
    Rect32 tileBandWindowREL[t1::BAND_NUM_ORIENTATIONS];

    tileBandWindowREL[t1::BAND_ORIENT_LL] = bandWindowREL_[t1::BAND_ORIENT_LL];
    tileBandWindowREL[t1::BAND_ORIENT_HL] =
        bandWindowREL_[t1::BAND_ORIENT_HL].pan(fullRes->width(), 0);
    tileBandWindowREL[t1::BAND_ORIENT_LH] =
        bandWindowREL_[t1::BAND_ORIENT_LH].pan(0, fullRes->height());
    tileBandWindowREL[t1::BAND_ORIENT_HH] =
        bandWindowREL_[t1::BAND_ORIENT_HH].pan(fullRes->width(), fullRes->height());
    // 2. pre-allocate sparse blocks
    auto fullResNext = fullRes + 1;
    for(uint32_t i = 0; i < t1::BAND_NUM_ORIENTATIONS; ++i)
    {
      auto temp = tileBandWindowREL[i];
      if(!sa->alloc(
             temp.grow_IN_PLACE(2 * FILTER_WIDTH, fullResNext->width(), fullResNext->height()),
             true))
        return false;
    }
    resWindowREL_ = tileWindow->getResWindowBufferREL(resno);
    if(!sa->alloc(resWindowREL_, true))
      return false;
    splitWindowREL_[SPLIT_L] = tileWindow->getResWindowBufferSplitREL(resno, SPLIT_L);
    splitWindowREL_[SPLIT_H] = tileWindow->getResWindowBufferSplitREL(resno, SPLIT_H);

    for(uint32_t k = 0; k < SPLIT_NUM_ORIENTATIONS; ++k)
    {
      auto temp = splitWindowREL_[k];
      if(!sa->alloc(
             temp.grow_IN_PLACE(2 * FILTER_WIDTH, fullResNext->width(), fullResNext->height()),
             true))
        return false;
    }

    return true;
  }
};

/**
 * ************************************************************************************
 *
 * ST is the lifting element type, CT the sparse canvas sample type
 *
 * Horizontal pass
 *
 * Each thread processes a strip running the length of the window, with height
 * sizeof(ST)/sizeof(CT): 1 for 5/3, 4 for 9/7
 *
 * Vertical pass
 *
 * Each thread processes a strip of width VERT_PASS_WIDTH: 4 for int32 5/3,
 * 8 for int16 5/3, 1 for 9/7
 *
 ****************************************************************************
 *
 * FILTER_WIDTH value matches the maximum left/right extension given in tables
 * F.2 and F.3 of the standard
 */
template<typename T, typename CT, uint32_t FILTER_WIDTH, uint32_t VERT_PASS_WIDTH, typename D>

bool WaveletReverse::partial_tile(ISparseCanvas<CT>* sa,
                                  std::vector<PartialTaskInfo<T, dwt_scratch<T>>*>& tasks)
{
  uint8_t numresolutions = tilec_->num_resolutions_;
  auto buf = tilec_->typedWindow<CT>();
  auto simpleBuf = buf->getResWindowBufferHighestSimple();
  auto fullRes = tilec_->resolutions_;
  auto fullResTopLevel = tilec_->resolutions_ + numres_ - 1;
  if(!fullResTopLevel->width() || !fullResTopLevel->height())
    return true;

  const uint32_t HORIZ_PASS_HEIGHT = sizeof(T) / sizeof(CT);
  const uint32_t pad =
      FILTER_WIDTH * std::max<uint32_t>(HORIZ_PASS_HEIGHT, VERT_PASS_WIDTH) * HORIZ_PASS_HEIGHT;
  // reduce window
  auto synthesisWindow = unreducedWindow_.scaleDownCeilPow2(numresolutions - numres_);
  assert(fullResTopLevel->intersection(synthesisWindow) == synthesisWindow);
  // shift to relative coordinates
  synthesisWindow =
      synthesisWindow.pan(-(int64_t)fullResTopLevel->x0, -(int64_t)fullResTopLevel->y0);
  if(synthesisWindow.empty())
    return true;
  uint32_t num_threads = (uint32_t)TFSingleton::num_threads();
  auto imageComponentFlow = ((DecompressScheduler*)scheduler_)->getImageComponentFlow(compno_);
  // imageComponentFlow == nullptr ==> no blocks were decompressed for this component
  if(!imageComponentFlow)
    return true;
  // synthesis sink, replacing the standalone dc shift pass: the int16 9/7 lifts in
  // Q-format, so its fractional bits round back out here first, matching
  // hwy_v_synth_16_97. qShift is 0 for every other partial instantiation.
  auto apply_dc_shift = [this, synthesisWindow, simpleBuf]() {
    if(!dcShift_.enabled)
      return;
    // the add can overflow int32 on fuzzer streams
    const int64_t shift = dcShift_.shift;
    const int64_t lowest = dcShift_.min;
    const int64_t highest = dcShift_.max;
    const int qShift = tilec_->qShift();
    for(uint32_t y = 0; y < synthesisWindow.height(); ++y)
    {
      auto row = simpleBuf.buf_ + (size_t)y * simpleBuf.stride_;
      for(uint32_t x = 0; x < synthesisWindow.width(); ++x)
      {
        auto value = row[x];
        if constexpr(std::is_same_v<CT, int16_t>)
          value = rshift_even_16(value, qShift);
        row[x] = (CT)std::clamp((int64_t)value + shift, lowest, highest);
      }
    }
  };
  if(numres_ == 1U)
  {
    auto final_read = [sa, synthesisWindow, simpleBuf, apply_dc_shift]() {
      // final read into tile buffer
      bool ret = sa->read(0, synthesisWindow, simpleBuf.buf_, 1, simpleBuf.stride_);
      apply_dc_shift();

      return ret;
    };
    imageComponentFlow->waveletFinalCopy_->nextTask().work([final_read] { final_read(); });

    return true;
  }
  auto final_read = [this, sa, synthesisWindow, simpleBuf, apply_dc_shift]() {
    // final read into tile buffer
    bool ret = sa->read(numres_ - 1, synthesisWindow, simpleBuf.buf_, 1, simpleBuf.stride_);
    apply_dc_shift();

    return ret;
  };
  // pre-allocate all blocks
  std::vector<PartialBandInfo<CT, FILTER_WIDTH>> resBandInfo;
  for(uint8_t resno = 1; resno < numres_; resno++)
  {
    PartialBandInfo<CT, FILTER_WIDTH> bandInfo;
    if(!bandInfo.alloc(sa, resno, fullRes + resno - 1, buf))
      return false;
    resBandInfo.push_back(bandInfo);
  }
  D decompressor;
  for(uint8_t resno = 1; resno < numres_; resno++)
  {
    dwt_scratch<T> horiz;
    dwt_scratch<T> vert;
    horiz.sn = fullRes->width();
    vert.sn = fullRes->height();
    fullRes++;
    horiz.dn = fullRes->width() - horiz.sn;
    horiz.parity = fullRes->x0 & 1;
    vert.dn = fullRes->height() - vert.sn;
    vert.parity = fullRes->y0 & 1;
    PartialBandInfo<CT, FILTER_WIDTH>& bandInfo = resBandInfo[resno - 1];

    auto executor_h = [resno, sa, bandInfo,
                       &decompressor](PartialTaskInfo<T, dwt_scratch<T>>* taskInfo) {
      for(uint32_t yPos = taskInfo->indexMin_; yPos < taskInfo->indexMax_;
          yPos += HORIZ_PASS_HEIGHT)
      {
        auto height = std::min<uint32_t>((uint32_t)HORIZ_PASS_HEIGHT, taskInfo->indexMax_ - yPos);
        taskInfo->data.memL = taskInfo->data.mem + taskInfo->data.parity;
        taskInfo->data.memH =
            taskInfo->data.mem + (int64_t)(!taskInfo->data.parity) +
            2 * ((int64_t)taskInfo->data.win_h.x0 - (int64_t)taskInfo->data.win_l.x0);
        if(!decompressor.interleave_h(&taskInfo->data, sa, yPos, height))
        {
          return false;
        }
        taskInfo->data.memL = taskInfo->data.mem;
        taskInfo->data.memH = taskInfo->data.mem +
                              ((int64_t)taskInfo->data.win_h.x0 - (int64_t)taskInfo->data.win_l.x0);
        decompressor.h(&taskInfo->data);
        if(!sa->write(
               resno,
               Rect32(bandInfo.resWindowREL_.x0, yPos, bandInfo.resWindowREL_.x1, yPos + height),
               (CT*)(taskInfo->data.mem + (int64_t)bandInfo.resWindowREL_.x0 -
                     2 * (int64_t)taskInfo->data.win_l.x0),
               HORIZ_PASS_HEIGHT, 1))
        {
          return false;
        }
      }

      return true;
    };
    auto executor_v = [resno, sa, bandInfo,
                       &decompressor](PartialTaskInfo<T, dwt_scratch<T>>* taskInfo) {
      for(uint32_t xPos = taskInfo->indexMin_; xPos < taskInfo->indexMax_; xPos += VERT_PASS_WIDTH)
      {
        auto width = std::min<uint32_t>(VERT_PASS_WIDTH, (taskInfo->indexMax_ - xPos));
        taskInfo->data.memL = taskInfo->data.mem + (taskInfo->data.parity) * VERT_PASS_WIDTH;
        taskInfo->data.memH = taskInfo->data.mem +
                              ((!taskInfo->data.parity) + 2 * ((int64_t)taskInfo->data.win_h.x0 -
                                                               (int64_t)taskInfo->data.win_l.x0)) *
                                  VERT_PASS_WIDTH;
        if(!decompressor.interleave_v(&taskInfo->data, sa, xPos, width))
        {
          return false;
        }
        taskInfo->data.memL = taskInfo->data.mem;
        taskInfo->data.memH =
            taskInfo->data.mem +
            ((int64_t)taskInfo->data.win_h.x0 - (int64_t)taskInfo->data.win_l.x0) * VERT_PASS_WIDTH;
        decompressor.v(&taskInfo->data);
        // write to buffer for final res
        if(!sa->write(resno,
                      Rect32(xPos, bandInfo.resWindowREL_.y0, xPos + width,
                             bandInfo.resWindowREL_.y0 + taskInfo->data.win_l.length() +
                                 taskInfo->data.win_h.length()),
                      (CT*)(taskInfo->data.mem + ((int64_t)bandInfo.resWindowREL_.y0 -
                                                  2 * (int64_t)taskInfo->data.win_l.x0) *
                                                     VERT_PASS_WIDTH),
                      1, VERT_PASS_WIDTH * HORIZ_PASS_HEIGHT))
        {
          grklog.error("Sparse array write failure");
          return false;
        }
      }

      return true;
    };

    // 3. calculate synthesis
    horiz.win_l = bandInfo.bandWindowREL_[t1::BAND_ORIENT_LL].dimX();
    horiz.win_h = bandInfo.bandWindowREL_[t1::BAND_ORIENT_HL].dimX();
    horiz.resno = resno;
    size_t dataLength =
        (bandInfo.splitWindowREL_[0].width() + 2 * FILTER_WIDTH) * HORIZ_PASS_HEIGHT;
    auto resFlow = imageComponentFlow->getResflow(resno - 1);
    for(uint32_t k = 0; k < 2 && dataLength; ++k)
    {
      uint32_t numTasks = num_threads;
      uint32_t num_rows = bandInfo.splitWindowREL_[k].height();
      if(num_rows < numTasks)
        numTasks = num_rows;
      uint32_t incrPerJob = numTasks ? (num_rows / numTasks) : 0;
      if(incrPerJob == 0)
        continue;
      for(uint32_t j = 0; j < numTasks; ++j)
      {
        uint32_t indexMin = bandInfo.splitWindowREL_[k].y0 + j * incrPerJob;
        uint32_t indexMax = j < (numTasks - 1U)
                                ? bandInfo.splitWindowREL_[k].y0 + (j + 1U) * incrPerJob
                                : bandInfo.splitWindowREL_[k].y1;
        if(indexMin == indexMax)
          continue;
        auto taskInfo = new PartialTaskInfo<T, dwt_scratch<T>>(horiz, indexMin, indexMax);
        if(!taskInfo->data.alloc(dataLength, pad))
        {
          delete taskInfo;
          return false;
        }
        tasks.push_back(taskInfo);
        resFlow->waveletHoriz_->nextTask().work([taskInfo, executor_h] { executor_h(taskInfo); });
      }
    }
    dataLength =
        (bandInfo.resWindowREL_.height() + 2 * FILTER_WIDTH) * VERT_PASS_WIDTH * HORIZ_PASS_HEIGHT;
    vert.win_l = bandInfo.bandWindowREL_[t1::BAND_ORIENT_LL].dimY();
    vert.win_h = bandInfo.bandWindowREL_[t1::BAND_ORIENT_LH].dimY();
    vert.resno = resno;
    uint32_t numTasks = num_threads;
    uint32_t numColumns = bandInfo.resWindowREL_.width();
    if(numColumns < numTasks)
      numTasks = numColumns;
    uint32_t incrPerJob = numTasks ? (numColumns / numTasks) : 0;
    for(uint32_t j = 0; j < numTasks && incrPerJob > 0 && dataLength; ++j)
    {
      uint32_t indexMin = bandInfo.resWindowREL_.x0 + j * incrPerJob;
      uint32_t indexMax = j < (numTasks - 1U) ? bandInfo.resWindowREL_.x0 + (j + 1U) * incrPerJob
                                              : bandInfo.resWindowREL_.x1;
      if(indexMin == indexMax)
        continue;
      auto taskInfo = new PartialTaskInfo<T, dwt_scratch<T>>(vert, indexMin, indexMax);
      if(!taskInfo->data.alloc(dataLength, pad))
      {
        delete taskInfo;
        return false;
      }
      tasks.push_back(taskInfo);
      resFlow->waveletVert_->nextTask().work([taskInfo, executor_v] { executor_v(taskInfo); });
    }
  }

  imageComponentFlow->waveletFinalCopy_->nextTask().work([final_read] { final_read(); });
  return true;
}

bool WaveletReverse::decompressPartial(void)
{
  if(qmfbid_ == 1)
  {
    if(tilec_->is16BitDwt())
    {
      // one 128-bit vector holds 8 int16 columns
      constexpr uint32_t VERT_PASS_WIDTH = 8;
      return partial_tile<
          int16_t, int16_t, getFilterPad<uint32_t>(true), VERT_PASS_WIDTH,
          Partial53<int16_t, int16_t, getFilterPad<uint32_t>(false), VERT_PASS_WIDTH>>(
          tilec_->getRegionWindow16(), partialTasks16_53_);
    }
    constexpr uint32_t VERT_PASS_WIDTH = 4;
    return partial_tile<
        int32_t, int32_t, getFilterPad<uint32_t>(true), VERT_PASS_WIDTH,
        Partial53<int32_t, int32_t, getFilterPad<uint32_t>(false), VERT_PASS_WIDTH>>(
        tilec_->getRegionWindow(), partialTasks53_);
  }
  else
  {
    constexpr uint32_t VERT_PASS_WIDTH = 1;
    if(tilec_->is16BitDwt())
    {
      return partial_tile<
          vec8s, int16_t, getFilterPad<uint32_t>(false), VERT_PASS_WIDTH,
          Partial97_16<vec8s, int16_t, getFilterPad<uint32_t>(false), VERT_PASS_WIDTH>>(
          tilec_->getRegionWindow16(), partialTasks16_97_);
    }
    return partial_tile<vec4f, int32_t, getFilterPad<uint32_t>(false), VERT_PASS_WIDTH,
                        Partial97<vec4f, int32_t, getFilterPad<uint32_t>(false), VERT_PASS_WIDTH>>(
        tilec_->getRegionWindow(), partialTasks97_);
  }
}

} // namespace grk
