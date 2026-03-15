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
        auto vg = Clamp(Sub(vi_y, ConvertTo(di, Add(Mul(c_cb_g, fcb), Mul(c_cr_g, fcr)))),
                         vZero_i, vUpb_i);
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
        auto vg = Clamp(Sub(vi_y, ConvertTo(di, Add(Mul(c_cb_g, fcb), Mul(c_cr_g, fcr)))),
                         vZero_i, vUpb_i);
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
        auto vr = Clamp(Add(Add(vy, Add(Mul(c_cb_y, vcb), Mul(c_cr_r, vcr))), vHalf), vZero,
                         vMax);
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
        if(rv > max_value) rv = max_value;
        else if(rv < 0) rv = 0;

        int32_t gv = (int32_t)(1.0003 * y_val - 0.344125 * cb_val - 0.7141128 * cr_val + 0.5);
        if(gv > max_value) gv = max_value;
        else if(gv < 0) gv = 0;

        int32_t bv = (int32_t)(0.999823 * y_val + 1.77204 * cb_val - 0.000008 * cr_val + 0.5);
        if(bv > max_value) bv = max_value;
        else if(bv < 0) bv = 0;

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

} // namespace HWY_NAMESPACE
} // namespace grk
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
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
                             int32_t* g, int32_t* b, uint32_t w, uint32_t h,
                             uint32_t src_stride, uint32_t dst_stride, int32_t offset,
                             int32_t upb)
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

} // namespace grk
#endif // HWY_ONCE
