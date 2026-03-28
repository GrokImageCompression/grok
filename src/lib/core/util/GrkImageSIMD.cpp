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

#include "hwy_arm_disable_targets.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "util/GrkImageSIMD.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace grk
{
namespace HWY_NAMESPACE
{
  namespace hn = hwy::HWY_NAMESPACE;

  /* ─── clip ─── */
  static void Hwy_clip_i32(int32_t* data, uint32_t w, uint32_t h, uint32_t stride, int32_t minVal,
                           int32_t maxVal)
  {
    const HWY_FULL(int32_t) di;
    const uint32_t L = (uint32_t)Lanes(di);
    const auto vMin = Set(di, minVal);
    const auto vMax = Set(di, maxVal);

    for(uint32_t j = 0; j < h; ++j)
    {
      int32_t* row = data + j * stride;
      uint32_t i = 0;
      for(; i + L <= w; i += L)
      {
        auto v = LoadU(di, row + i);
        StoreU(Clamp(v, vMin, vMax), di, row + i);
      }
      if(i < w)
      {
        auto m = hn::FirstN(di, w - i);
        auto v = hn::MaskedLoad(m, di, row + i);
        hn::BlendedStore(Clamp(v, vMin, vMax), m, di, row + i);
      }
    }
  }

  /* ─── scale multiply ─── */
  static void Hwy_scale_mul_i32(int32_t* data, uint32_t w, uint32_t h, uint32_t stride,
                                int32_t scale)
  {
    const HWY_FULL(int32_t) di;
    const uint32_t L = (uint32_t)Lanes(di);
    const auto vScale = Set(di, scale);

    for(uint32_t j = 0; j < h; ++j)
    {
      int32_t* row = data + j * stride;
      uint32_t i = 0;
      for(; i + L <= w; i += L)
      {
        auto v = LoadU(di, row + i);
        StoreU(Mul(v, vScale), di, row + i);
      }
      if(i < w)
      {
        auto m = hn::FirstN(di, w - i);
        auto v = hn::MaskedLoad(m, di, row + i);
        hn::BlendedStore(Mul(v, vScale), m, di, row + i);
      }
    }
  }

  /* ─── scale divide (truncation toward zero, matching C integer division) ─── */
  static void Hwy_scale_div_i32(int32_t* data, uint32_t w, uint32_t h, uint32_t stride,
                                int32_t scale)
  {
    const HWY_FULL(float) df;
    const HWY_FULL(int32_t) di;
    const uint32_t L = (uint32_t)Lanes(df);
    const auto vScale = Set(df, (float)scale);

    for(uint32_t j = 0; j < h; ++j)
    {
      int32_t* row = data + j * stride;
      uint32_t i = 0;
      for(; i + L <= w; i += L)
      {
        auto v = ConvertTo(df, LoadU(di, row + i));
        /* ConvertTo(di, ...) truncates toward zero, matching C integer division */
        StoreU(ConvertTo(di, Div(v, vScale)), di, row + i);
      }
      if(i < w)
      {
        auto m_i = hn::FirstN(di, w - i);
        auto v = ConvertTo(df, hn::MaskedLoad(m_i, di, row + i));
        hn::BlendedStore(ConvertTo(di, Div(v, vScale)), m_i, di, row + i);
      }
    }
  }

  /* ─── YCC 4:4:4 → RGB ───
   * Matches scalar: cb -= offset; cr -= offset; r = y + (int)(1.402 * cr); etc.
   * Key: truncation to int happens between the float multiply and the int add. */
  static void Hwy_sycc444_to_rgb_i32(const int32_t* y, const int32_t* cb, const int32_t* cr,
                                     int32_t* r, int32_t* g, int32_t* b, uint32_t w, uint32_t h,
                                     uint32_t src_stride, uint32_t dst_stride, int32_t offset,
                                     int32_t upb)
  {
    const HWY_FULL(float) df;
    const HWY_FULL(int32_t) di;
    const uint32_t L = (uint32_t)Lanes(df);

    const auto vOffset_i = Set(di, offset);
    const auto vZero_i = Zero(di);
    const auto vUpb_i = Set(di, upb);

    /* YCC→RGB coefficients (positive values for correct truncation matching) */
    const auto c_cr_r = Set(df, 1.402f);
    const auto c_cb_g = Set(df, 0.344f);
    const auto c_cr_g = Set(df, 0.714f);
    const auto c_cb_b = Set(df, 1.772f);

    for(uint32_t j = 0; j < h; ++j)
    {
      const int32_t* yRow = y + j * src_stride;
      const int32_t* cbRow = cb + j * src_stride;
      const int32_t* crRow = cr + j * src_stride;
      int32_t* rRow = r + j * dst_stride;
      int32_t* gRow = g + j * dst_stride;
      int32_t* bRow = b + j * dst_stride;

      uint32_t i = 0;
      for(; i + L <= w; i += L)
      {
        auto vi_y = LoadU(di, yRow + i);
        /* Integer offset subtraction (matches: cb -= offset) */
        auto vi_cb = Sub(LoadU(di, cbRow + i), vOffset_i);
        auto vi_cr = Sub(LoadU(di, crRow + i), vOffset_i);
        auto fcb = ConvertTo(df, vi_cb);
        auto fcr = ConvertTo(df, vi_cr);

        /* r = y + (int)(1.402 * cr) */
        auto vr = Clamp(Add(vi_y, ConvertTo(di, Mul(c_cr_r, fcr))), vZero_i, vUpb_i);
        /* g = y - (int)(0.344 * cb + 0.714 * cr) */
        auto vg = Clamp(Sub(vi_y, ConvertTo(di, Add(Mul(c_cb_g, fcb), Mul(c_cr_g, fcr)))), vZero_i,
                        vUpb_i);
        /* b = y + (int)(1.772 * cb) */
        auto vb = Clamp(Add(vi_y, ConvertTo(di, Mul(c_cb_b, fcb))), vZero_i, vUpb_i);

        StoreU(vr, di, rRow + i);
        StoreU(vg, di, gRow + i);
        StoreU(vb, di, bRow + i);
      }
      if(i < w)
      {
        auto m_i = hn::FirstN(di, w - i);

        auto vi_y = hn::MaskedLoad(m_i, di, yRow + i);
        auto vi_cb = Sub(hn::MaskedLoad(m_i, di, cbRow + i), vOffset_i);
        auto vi_cr = Sub(hn::MaskedLoad(m_i, di, crRow + i), vOffset_i);
        auto fcb = ConvertTo(df, vi_cb);
        auto fcr = ConvertTo(df, vi_cr);

        auto vr = Clamp(Add(vi_y, ConvertTo(di, Mul(c_cr_r, fcr))), vZero_i, vUpb_i);
        auto vg = Clamp(Sub(vi_y, ConvertTo(di, Add(Mul(c_cb_g, fcb), Mul(c_cr_g, fcr)))), vZero_i,
                        vUpb_i);
        auto vb = Clamp(Add(vi_y, ConvertTo(di, Mul(c_cb_b, fcb))), vZero_i, vUpb_i);

        hn::BlendedStore(vr, m_i, di, rRow + i);
        hn::BlendedStore(vg, m_i, di, gRow + i);
        hn::BlendedStore(vb, m_i, di, bRow + i);
      }
    }
  }

  /* ─── eYCC → RGB (in-place) ───
   * Matches scalar: val = (int32_t)(y - 0.0000368 * cb + 1.40199 * cr + 0.5)
   * Original uses double precision (implicit C++ promotion), so we use double vectors.
   * On AVX2 this gives 4 lanes per vector instead of 8. */
  static void Hwy_esycc_to_rgb_i32(int32_t* yd, int32_t* bd, int32_t* rd, uint32_t w, uint32_t h,
                                   uint32_t stride, int32_t max_value, int32_t flip_value,
                                   bool sign1, bool sign2)
  {
    const HWY_FULL(double) dd;
    const HWY_FULL(int32_t) di;
    const hn::Half<decltype(di)> di_half;
    const uint32_t L = (uint32_t)Lanes(dd);

    const auto vZero = Zero(dd);
    const auto vMax = Set(dd, (double)max_value);
    const auto vFlip1 = sign1 ? Zero(di) : Set(di, flip_value);
    const auto vFlip2 = sign2 ? Zero(di) : Set(di, flip_value);
    const auto vHalf = Set(dd, 0.5);

    /* eYCC→RGB coefficients in double */
    const auto c_cr_r = Set(dd, 1.40199);
    const auto c_cb_y = Set(dd, -0.0000368);
    const auto c_y_g = Set(dd, 1.0003);
    const auto c_cb_g = Set(dd, -0.344125);
    const auto c_cr_g = Set(dd, -0.7141128);
    const auto c_y_b = Set(dd, 0.999823);
    const auto c_cb_b = Set(dd, 1.77204);
    const auto c_cr_b = Set(dd, -0.000008);

    for(uint32_t j = 0; j < h; ++j)
    {
      int32_t* yRow = yd + j * stride;
      int32_t* bRow = bd + j * stride;
      int32_t* rRow = rd + j * stride;

      uint32_t i = 0;
      for(; i + L <= w; i += L)
      {
        /* Load L int32 values, apply sign offset, promote to double */
        auto vi_y = LoadU(di_half, yRow + i);
        auto vi_cb = Sub(LoadU(di_half, bRow + i), LowerHalf(di_half, vFlip1));
        auto vi_cr = Sub(LoadU(di_half, rRow + i), LowerHalf(di_half, vFlip2));

        auto vy = PromoteTo(dd, vi_y);
        auto vcb = PromoteTo(dd, vi_cb);
        auto vcr = PromoteTo(dd, vi_cr);

        /* R = (int)(y - 0.0000368*cb + 1.40199*cr + 0.5) */
        auto vr = Clamp(Add(Add(vy, Add(Mul(c_cb_y, vcb), Mul(c_cr_r, vcr))), vHalf), vZero, vMax);
        /* G = (int)(1.0003*y - 0.344125*cb - 0.7141128*cr + 0.5) */
        auto vg = Clamp(Add(Add(Mul(c_y_g, vy), Add(Mul(c_cb_g, vcb), Mul(c_cr_g, vcr))), vHalf),
                        vZero, vMax);
        /* B = (int)(0.999823*y + 1.77204*cb - 0.000008*cr + 0.5) */
        auto vb = Clamp(Add(Add(Mul(c_y_b, vy), Add(Mul(c_cb_b, vcb), Mul(c_cr_b, vcr))), vHalf),
                        vZero, vMax);

        StoreU(DemoteTo(di_half, vr), di_half, yRow + i);
        StoreU(DemoteTo(di_half, vg), di_half, bRow + i);
        StoreU(DemoteTo(di_half, vb), di_half, rRow + i);
      }
      for(; i < w; ++i)
      {
        double y_val = (double)yRow[i];
        double cb_val = (double)(bRow[i] - (sign1 ? 0 : flip_value));
        double cr_val = (double)(rRow[i] - (sign2 ? 0 : flip_value));

        int32_t rv = (int32_t)(y_val - 0.0000368 * cb_val + 1.40199 * cr_val + 0.5);
        if(rv > max_value)
          rv = max_value;
        else if(rv < 0)
          rv = 0;

        int32_t gv = (int32_t)(1.0003 * y_val - 0.344125 * cb_val - 0.7141128 * cr_val + 0.5);
        if(gv > max_value)
          gv = max_value;
        else if(gv < 0)
          gv = 0;

        int32_t bv = (int32_t)(0.999823 * y_val + 1.77204 * cb_val - 0.000008 * cr_val + 0.5);
        if(bv > max_value)
          bv = max_value;
        else if(bv < 0)
          bv = 0;

        yRow[i] = rv;
        bRow[i] = gv;
        rRow[i] = bv;
      }
    }
  }

  /* ─── Planar int32_t → packed uint8_t RGB ─── */
  static void Hwy_planar_to_packed_8(const int32_t* r, const int32_t* g, const int32_t* b,
                                     uint8_t* out, uint32_t w, uint32_t h, uint32_t src_stride)
  {
    const HWY_FULL(int32_t) di;
    const uint32_t L = (uint32_t)Lanes(di);

    for(uint32_t j = 0; j < h; ++j)
    {
      const int32_t* rRow = r + j * src_stride;
      const int32_t* gRow = g + j * src_stride;
      const int32_t* bRow = b + j * src_stride;
      uint8_t* dst = out + j * w * 3;

      uint32_t i = 0;
      for(; i + L <= w; i += L)
      {
        auto vr = LoadU(di, rRow + i);
        auto vg = LoadU(di, gRow + i);
        auto vb = LoadU(di, bRow + i);
        /* Scalar store for the narrowing conversion — simple and correct */
        for(uint32_t k = 0; k < L; ++k)
        {
          dst[(i + k) * 3 + 0] = (uint8_t)ExtractLane(vr, k);
          dst[(i + k) * 3 + 1] = (uint8_t)ExtractLane(vg, k);
          dst[(i + k) * 3 + 2] = (uint8_t)ExtractLane(vb, k);
        }
      }
      for(; i < w; ++i)
      {
        dst[i * 3 + 0] = (uint8_t)rRow[i];
        dst[i * 3 + 1] = (uint8_t)gRow[i];
        dst[i * 3 + 2] = (uint8_t)bRow[i];
      }
    }
  }

  /* ─── Packed uint8_t RGB → planar int32_t ─── */
  static void Hwy_packed_to_planar_8(const uint8_t* in, int32_t* r, int32_t* g, int32_t* b,
                                     uint32_t w, uint32_t h, uint32_t dst_stride)
  {
    const HWY_FULL(int32_t) di;
    const uint32_t L = (uint32_t)Lanes(di);

    for(uint32_t j = 0; j < h; ++j)
    {
      const uint8_t* src = in + j * w * 3;
      int32_t* rRow = r + j * dst_stride;
      int32_t* gRow = g + j * dst_stride;
      int32_t* bRow = b + j * dst_stride;

      uint32_t i = 0;
      for(; i + L <= w; i += L)
      {
        /* Scalar load for the widening conversion */
        HWY_ALIGN int32_t tmpR[HWY_MAX_LANES_D(HWY_FULL(int32_t))];
        HWY_ALIGN int32_t tmpG[HWY_MAX_LANES_D(HWY_FULL(int32_t))];
        HWY_ALIGN int32_t tmpB[HWY_MAX_LANES_D(HWY_FULL(int32_t))];
        for(uint32_t k = 0; k < L; ++k)
        {
          tmpR[k] = (int32_t)src[(i + k) * 3 + 0];
          tmpG[k] = (int32_t)src[(i + k) * 3 + 1];
          tmpB[k] = (int32_t)src[(i + k) * 3 + 2];
        }
        StoreU(Load(di, tmpR), di, rRow + i);
        StoreU(Load(di, tmpG), di, gRow + i);
        StoreU(Load(di, tmpB), di, bRow + i);
      }
      for(; i < w; ++i)
      {
        rRow[i] = (int32_t)src[i * 3 + 0];
        gRow[i] = (int32_t)src[i * 3 + 1];
        bRow[i] = (int32_t)src[i * 3 + 2];
      }
    }
  }

  /* ─── Planar int32_t → packed uint16_t RGB ─── */
  static void Hwy_planar_to_packed_16(const int32_t* r, const int32_t* g, const int32_t* b,
                                      uint16_t* out, uint32_t w, uint32_t h, uint32_t src_stride)
  {
    const HWY_FULL(int32_t) di;
    const uint32_t L = (uint32_t)Lanes(di);

    for(uint32_t j = 0; j < h; ++j)
    {
      const int32_t* rRow = r + j * src_stride;
      const int32_t* gRow = g + j * src_stride;
      const int32_t* bRow = b + j * src_stride;
      uint16_t* dst = out + j * w * 3;

      uint32_t i = 0;
      for(; i + L <= w; i += L)
      {
        auto vr = LoadU(di, rRow + i);
        auto vg = LoadU(di, gRow + i);
        auto vb = LoadU(di, bRow + i);
        for(uint32_t k = 0; k < L; ++k)
        {
          dst[(i + k) * 3 + 0] = (uint16_t)ExtractLane(vr, k);
          dst[(i + k) * 3 + 1] = (uint16_t)ExtractLane(vg, k);
          dst[(i + k) * 3 + 2] = (uint16_t)ExtractLane(vb, k);
        }
      }
      for(; i < w; ++i)
      {
        dst[i * 3 + 0] = (uint16_t)rRow[i];
        dst[i * 3 + 1] = (uint16_t)gRow[i];
        dst[i * 3 + 2] = (uint16_t)bRow[i];
      }
    }
  }

  /* ─── Packed uint16_t RGB → planar int32_t ─── */
  static void Hwy_packed_to_planar_16(const uint16_t* in, int32_t* r, int32_t* g, int32_t* b,
                                      uint32_t w, uint32_t h, uint32_t dst_stride)
  {
    const HWY_FULL(int32_t) di;
    const uint32_t L = (uint32_t)Lanes(di);

    for(uint32_t j = 0; j < h; ++j)
    {
      const uint16_t* src = in + j * w * 3;
      int32_t* rRow = r + j * dst_stride;
      int32_t* gRow = g + j * dst_stride;
      int32_t* bRow = b + j * dst_stride;

      uint32_t i = 0;
      for(; i + L <= w; i += L)
      {
        HWY_ALIGN int32_t tmpR[HWY_MAX_LANES_D(HWY_FULL(int32_t))];
        HWY_ALIGN int32_t tmpG[HWY_MAX_LANES_D(HWY_FULL(int32_t))];
        HWY_ALIGN int32_t tmpB[HWY_MAX_LANES_D(HWY_FULL(int32_t))];
        for(uint32_t k = 0; k < L; ++k)
        {
          tmpR[k] = (int32_t)src[(i + k) * 3 + 0];
          tmpG[k] = (int32_t)src[(i + k) * 3 + 1];
          tmpB[k] = (int32_t)src[(i + k) * 3 + 2];
        }
        StoreU(Load(di, tmpR), di, rRow + i);
        StoreU(Load(di, tmpG), di, gRow + i);
        StoreU(Load(di, tmpB), di, bRow + i);
      }
      for(; i < w; ++i)
      {
        rRow[i] = (int32_t)src[i * 3 + 0];
        gRow[i] = (int32_t)src[i * 3 + 1];
        bRow[i] = (int32_t)src[i * 3 + 2];
      }
    }
  }

  /* ─── int32_t row → uint8_t row (GDALCopyWords clamp to [0, 255]) ─── */
  static void Hwy_copy_i32_to_u8_row(const int32_t* HWY_RESTRICT src, uint8_t* HWY_RESTRICT dst,
                                     uint32_t n)
  {
    const HWY_FULL(int32_t) di;
    const uint32_t L = (uint32_t)Lanes(di);
    const auto vZero = Zero(di);
    const auto vMax = Set(di, 255);

    uint32_t i = 0;
    for(; i + L <= n; i += L)
    {
      auto v = Clamp(LoadU(di, src + i), vZero, vMax);
      for(uint32_t k = 0; k < L; ++k)
        dst[i + k] = (uint8_t)ExtractLane(v, k);
    }
    for(; i < n; ++i)
    {
      int32_t v = src[i];
      dst[i] = v <= 0 ? (uint8_t)0 : v >= 255 ? (uint8_t)255 : (uint8_t)v;
    }
  }

  /* ─── int32_t row → int16_t row (GDALCopyWords clamp to [-32768, 32767]) ─── */
  static void Hwy_copy_i32_to_i16_row(const int32_t* HWY_RESTRICT src, int16_t* HWY_RESTRICT dst,
                                      uint32_t n)
  {
    const HWY_FULL(int32_t) di;
    const uint32_t L = (uint32_t)Lanes(di);
    const auto vMin = Set(di, -32768);
    const auto vMax = Set(di, 32767);

    uint32_t i = 0;
    for(; i + L <= n; i += L)
    {
      auto v = Clamp(LoadU(di, src + i), vMin, vMax);
      for(uint32_t k = 0; k < L; ++k)
        dst[i + k] = (int16_t)ExtractLane(v, k);
    }
    for(; i < n; ++i)
    {
      int32_t v = src[i];
      dst[i] = v <= -32768 ? (int16_t)-32768 : v >= 32767 ? (int16_t)32767 : (int16_t)v;
    }
  }

  /* ─── int32_t row → uint16_t row (GDALCopyWords clamp to [0, 65535]) ─── */
  static void Hwy_copy_i32_to_u16_row(const int32_t* HWY_RESTRICT src, uint16_t* HWY_RESTRICT dst,
                                      uint32_t n)
  {
    const HWY_FULL(int32_t) di;
    const uint32_t L = (uint32_t)Lanes(di);
    const auto vZero = Zero(di);
    const auto vMax = Set(di, 65535);

    uint32_t i = 0;
    for(; i + L <= n; i += L)
    {
      auto v = Clamp(LoadU(di, src + i), vZero, vMax);
      for(uint32_t k = 0; k < L; ++k)
        dst[i + k] = (uint16_t)ExtractLane(v, k);
    }
    for(; i < n; ++i)
    {
      int32_t v = src[i];
      dst[i] = v <= 0 ? (uint16_t)0 : v >= 65535 ? (uint16_t)65535 : (uint16_t)v;
    }
  }

  /* ─── int32_t row → uint32_t row (GDALCopyWords clamp negative to 0) ─── */
  static void Hwy_copy_i32_to_u32_row(const int32_t* HWY_RESTRICT src, uint32_t* HWY_RESTRICT dst,
                                      uint32_t n)
  {
    const HWY_FULL(int32_t) di;
    const uint32_t L = (uint32_t)Lanes(di);
    const auto vZero = Zero(di);

    uint32_t i = 0;
    for(; i + L <= n; i += L)
    {
      auto v = Max(LoadU(di, src + i), vZero);
      for(uint32_t k = 0; k < L; ++k)
        dst[i + k] = (uint32_t)ExtractLane(v, k);
    }
    for(; i < n; ++i)
      dst[i] = src[i] < 0 ? 0u : (uint32_t)src[i];
  }

} // namespace HWY_NAMESPACE
} // namespace grk
HWY_AFTER_NAMESPACE();

#if HWY_ONCE

#include <algorithm>
#include <cstring>
#include "grok.h"

namespace grk
{

HWY_EXPORT(Hwy_clip_i32);
HWY_EXPORT(Hwy_scale_mul_i32);
HWY_EXPORT(Hwy_scale_div_i32);
HWY_EXPORT(Hwy_sycc444_to_rgb_i32);
HWY_EXPORT(Hwy_esycc_to_rgb_i32);
HWY_EXPORT(Hwy_planar_to_packed_8);
HWY_EXPORT(Hwy_packed_to_planar_8);
HWY_EXPORT(Hwy_planar_to_packed_16);
HWY_EXPORT(Hwy_packed_to_planar_16);
HWY_EXPORT(Hwy_copy_i32_to_u8_row);
HWY_EXPORT(Hwy_copy_i32_to_i16_row);
HWY_EXPORT(Hwy_copy_i32_to_u16_row);
HWY_EXPORT(Hwy_copy_i32_to_u32_row);

void hwy_clip_i32(int32_t* data, uint32_t w, uint32_t h, uint32_t stride, int32_t minVal,
                  int32_t maxVal)
{
  HWY_DYNAMIC_DISPATCH(Hwy_clip_i32)(data, w, h, stride, minVal, maxVal);
}

void hwy_scale_mul_i32(int32_t* data, uint32_t w, uint32_t h, uint32_t stride, int32_t scale)
{
  HWY_DYNAMIC_DISPATCH(Hwy_scale_mul_i32)(data, w, h, stride, scale);
}

void hwy_scale_div_i32(int32_t* data, uint32_t w, uint32_t h, uint32_t stride, int32_t scale)
{
  HWY_DYNAMIC_DISPATCH(Hwy_scale_div_i32)(data, w, h, stride, scale);
}

void hwy_sycc444_to_rgb_i32(const int32_t* y, const int32_t* cb, const int32_t* cr, int32_t* r,
                            int32_t* g, int32_t* b, uint32_t w, uint32_t h, uint32_t src_stride,
                            uint32_t dst_stride, int32_t offset, int32_t upb)
{
  HWY_DYNAMIC_DISPATCH(Hwy_sycc444_to_rgb_i32)
  (y, cb, cr, r, g, b, w, h, src_stride, dst_stride, offset, upb);
}

void hwy_esycc_to_rgb_i32(int32_t* yd, int32_t* bd, int32_t* rd, uint32_t w, uint32_t h,
                          uint32_t stride, int32_t max_value, int32_t flip_value, bool sign1,
                          bool sign2)
{
  HWY_DYNAMIC_DISPATCH(Hwy_esycc_to_rgb_i32)
  (yd, bd, rd, w, h, stride, max_value, flip_value, sign1, sign2);
}

void hwy_planar_to_packed_8(const int32_t* r, const int32_t* g, const int32_t* b, uint8_t* out,
                            uint32_t w, uint32_t h, uint32_t src_stride)
{
  HWY_DYNAMIC_DISPATCH(Hwy_planar_to_packed_8)(r, g, b, out, w, h, src_stride);
}

void hwy_packed_to_planar_8(const uint8_t* in, int32_t* r, int32_t* g, int32_t* b, uint32_t w,
                            uint32_t h, uint32_t dst_stride)
{
  HWY_DYNAMIC_DISPATCH(Hwy_packed_to_planar_8)(in, r, g, b, w, h, dst_stride);
}

void hwy_planar_to_packed_16(const int32_t* r, const int32_t* g, const int32_t* b, uint16_t* out,
                             uint32_t w, uint32_t h, uint32_t src_stride)
{
  HWY_DYNAMIC_DISPATCH(Hwy_planar_to_packed_16)(r, g, b, out, w, h, src_stride);
}

void hwy_packed_to_planar_16(const uint16_t* in, int32_t* r, int32_t* g, int32_t* b, uint32_t w,
                             uint32_t h, uint32_t dst_stride)
{
  HWY_DYNAMIC_DISPATCH(Hwy_packed_to_planar_16)(in, r, g, b, w, h, dst_stride);
}

void hwy_copy_tile_to_swath(const grk_image* tile_img, const grk_swath_buffer* buf)
{
  if(!tile_img || !buf || !buf->data)
    return;

  const uint32_t swath_x0 = buf->x0;
  const uint32_t swath_x1 = buf->x1;
  const uint32_t swath_y0 = buf->y0;
  const uint32_t swath_y1 = buf->y1;
  uint8_t* const base = static_cast<uint8_t*>(buf->data);

  for(uint16_t i = 0; i < buf->numcomps; ++i)
  {
    // Resolve source component index via band_map (1-based) or identity
    const int compIdx = buf->band_map ? (buf->band_map[i] - 1) : (int)i;
    if(compIdx < 0 || compIdx >= (int)tile_img->numcomps)
      continue;

    const grk_image_comp& comp = tile_img->comps[compIdx];
    if(!comp.data)
      continue;
    const int32_t* src = static_cast<const int32_t*>(comp.data);

    // Clip to swath y range
    const uint32_t tileY0 = comp.y0;
    const uint32_t tileY1 = comp.y0 + comp.h;
    const uint32_t copyY0 = std::max(tileY0, swath_y0);
    const uint32_t copyY1 = std::min(tileY1, swath_y1);
    if(copyY0 >= copyY1)
      continue;

    // Clip to swath x range — tile may extend beyond the window
    const uint32_t tileX0 = comp.x0;
    const uint32_t tileX1 = comp.x0 + comp.w;
    const uint32_t copyX0 = std::max(tileX0, swath_x0);
    const uint32_t copyX1 = std::min(tileX1, swath_x1);
    if(copyX0 >= copyX1)
      continue;

    const uint32_t cols = copyX1 - copyX0;
    const uint32_t srcXOff = copyX0 - tileX0; // offset within tile row
    const uint32_t dstXOff = copyX0 - swath_x0; // offset in output pixels

    const int elem_bytes = (buf->prec <= 8) ? 1 : (buf->prec <= 16) ? 2 : 4;
    const bool packed = (buf->pixel_space == static_cast<int64_t>(elem_bytes));

    const bool doPromote = (buf->promote_alpha >= 0 && buf->promote_alpha == compIdx);

    for(uint32_t y = copyY0; y < copyY1; ++y)
    {
      const uint32_t srcRowIdx = y - tileY0;
      const uint32_t dstRowIdx = y - swath_y0;

      const int32_t* srcPtr = src + static_cast<size_t>(srcRowIdx) * comp.stride + srcXOff;

      uint8_t* dstRowBase = base + static_cast<int64_t>(i) * buf->band_space +
                            static_cast<int64_t>(dstRowIdx) * buf->line_space +
                            static_cast<int64_t>(dstXOff) * buf->pixel_space;

      if(packed && !doPromote)
      {
        // Fast path: contiguous output — use SIMD dispatch
        if(buf->prec <= 8)
          HWY_DYNAMIC_DISPATCH(Hwy_copy_i32_to_u8_row)(srcPtr, dstRowBase, cols);
        else if(buf->prec <= 16 && buf->sgnd)
          HWY_DYNAMIC_DISPATCH(Hwy_copy_i32_to_i16_row)(
              srcPtr, reinterpret_cast<int16_t*>(dstRowBase), cols);
        else if(buf->prec <= 16)
          HWY_DYNAMIC_DISPATCH(Hwy_copy_i32_to_u16_row)(
              srcPtr, reinterpret_cast<uint16_t*>(dstRowBase), cols);
        else if(buf->sgnd)
          memcpy(dstRowBase, srcPtr, static_cast<size_t>(cols) * sizeof(int32_t));
        else
          HWY_DYNAMIC_DISPATCH(Hwy_copy_i32_to_u32_row)(
              srcPtr, reinterpret_cast<uint32_t*>(dstRowBase), cols);
      }
      else
      {
        // Slow path: strided output or alpha promotion
        for(uint32_t x = 0; x < cols; ++x)
        {
          int32_t v = srcPtr[x];
          if(doPromote)
            v *= 255;
          uint8_t* dstPx = dstRowBase + static_cast<int64_t>(x) * buf->pixel_space;
          if(buf->prec <= 8)
          {
            *dstPx = v <= 0 ? 0 : v >= 255 ? 255 : (uint8_t)v;
          }
          else if(buf->prec <= 16 && buf->sgnd)
          {
            int16_t w = v <= -32768 ? (int16_t)-32768 : v >= 32767 ? (int16_t)32767 : (int16_t)v;
            memcpy(dstPx, &w, sizeof(int16_t));
          }
          else if(buf->prec <= 16)
          {
            uint16_t w = v <= 0 ? (uint16_t)0 : v >= 65535 ? (uint16_t)65535 : (uint16_t)v;
            memcpy(dstPx, &w, sizeof(uint16_t));
          }
          else if(buf->sgnd)
          {
            memcpy(dstPx, &v, sizeof(int32_t));
          }
          else
          {
            uint32_t w = v < 0 ? 0u : (uint32_t)v;
            memcpy(dstPx, &w, sizeof(uint32_t));
          }
        }
      }
    }
  }
}

} // namespace grk
#endif // HWY_ONCE
