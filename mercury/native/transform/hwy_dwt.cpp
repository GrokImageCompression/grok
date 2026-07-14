#include "transform/dwt_kernels.h"
#include <assert.h>
#include <cmath>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "transform/hwy_dwt.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace mercury_native
{
namespace HWY_NAMESPACE
{
  namespace hn = hwy::HWY_NAMESPACE;

  static void Impl_interleave_16(int16_t* src1, int16_t* src2, int16_t* dst, int pairs)
  {
    const hn::ScalableTag<int16_t> d;
    const size_t N = hn::Lanes(d);
    for(int i = 0; i < pairs; i += (int)N)
    {
      auto a = hn::Load(d, src1 + i);
      auto b = hn::Load(d, src2 + i);
      auto lo = hn::InterleaveLower(d, a, b);
      auto hi = hn::InterleaveUpper(d, a, b);
      hn::Store(hn::ConcatLowerLower(d, hi, lo), d, dst + 2 * i);
      hn::Store(hn::ConcatUpperUpper(d, hi, lo), d, dst + 2 * i + (int)N);
    }
  }

  static void Impl_interleave_32(int32_t* src1, int32_t* src2, int32_t* dst, int pairs)
  {
    const hn::ScalableTag<int32_t> d;
    const size_t N = hn::Lanes(d);
    for(int i = 0; i < pairs; i += (int)N)
    {
      auto a = hn::Load(d, src1 + i);
      auto b = hn::Load(d, src2 + i);
      auto lo = hn::InterleaveLower(d, a, b);
      auto hi = hn::InterleaveUpper(d, a, b);
      hn::Store(hn::ConcatLowerLower(d, hi, lo), d, dst + 2 * i);
      hn::Store(hn::ConcatUpperUpper(d, hi, lo), d, dst + 2 * i + (int)N);
    }
  }

  static void Impl_vlift_16_5x3_synth_s0(int16_t** src, int16_t* dst_in, int16_t* dst_out,
                                         int samples)
  {
    // W5X3 step 0 is always downshift=1, icoeffs [-1,-1]
    int16_t *src1 = src[0], *src2 = src[1];
    const hn::ScalableTag<int16_t> d;
    const hn::ScalableTag<uint16_t> du;
    const size_t N = hn::Lanes(d);
    // 17-bit-safe floor((a+b)/2): naive (a+b) wraps int16 (BIBO keeps a,b in
    // int16 but their SUM needs 17 bits). AverageRound in the biased-unsigned
    // domain yields (a+b+1)>>1 exactly; subtracting the odd bit ((a^b)&1) floors.
    auto msk_smin = hn::Set(d, (int16_t)0x8000u);
    auto vec_one = hn::Set(d, (int16_t)1);
    for(int c = 0; c < samples; c += (int)N)
    {
      auto a = hn::Load(d, src1 + c);
      auto b = hn::Load(d, src2 + c);
      auto au = hn::BitCast(du, hn::Xor(a, msk_smin));
      auto bu = hn::BitCast(du, hn::Xor(b, msk_smin));
      auto avg = hn::Xor(hn::BitCast(d, hn::AverageRound(au, bu)), msk_smin);
      auto m = hn::Sub(avg, hn::And(hn::Xor(a, b), vec_one));
      hn::Store(hn::Add(hn::Load(d, dst_in + c), m), d, dst_out + c);
    }
  }

  static void Impl_vlift_16_5x3_synth_s1(int16_t** src, int16_t* dst_in, int16_t* dst_out,
                                         int samples)
  {
    const hn::ScalableTag<int16_t> d;
    const hn::ScalableTag<uint16_t> du;
    const size_t N = hn::Lanes(d);
    int16_t *src1 = src[0], *src2 = src[1];
    auto msk_smin = hn::Set(d, (int16_t)0x8000u);
    auto msk_smax = hn::Set(d, (int16_t)0x7FFF);
    for(int c = 0; c < samples; c += (int)N)
    {
      auto val1 = hn::Xor(hn::Load(d, src1 + c), msk_smin);
      auto val2 = hn::Add(hn::Load(d, src2 + c), msk_smax);
      auto avg = hn::BitCast(d, hn::AverageRound(hn::BitCast(du, val1), hn::BitCast(du, val2)));
      avg = hn::ShiftRight<1>(hn::Sub(avg, msk_smax));
      auto tgt = hn::Sub(hn::Load(d, dst_in + c), avg);
      hn::Store(tgt, d, dst_out + c);
    }
  }

  static void Impl_vlift_32_2tap_irrev(int32_t** src, int32_t* dst_in, int32_t* dst_out,
                                       int samples, merc_lifting_step* step)
  {
    const hn::ScalableTag<float> df;
    const size_t N = hn::Lanes(df);
    const float sign = -1.0f; // synthesis negates the lifting coeffs
    auto vec_l0 = hn::Set(df, sign * step->coeffs[0]);
    auto vec_l1 = hn::Set(df, sign * ((step->support_length == 2) ? step->coeffs[1] : 0.0f));
    float *sp0 = (float*)src[0], *sp1 = (step->support_length == 2) ? (float*)src[1] : sp0;
    float *dp_in = (float*)dst_in, *dp_out = (float*)dst_out;
    for(int c = 0; c < samples; c += (int)N)
    {
      auto tgt = hn::Load(df, dp_in + c);
      tgt = hn::MulAdd(hn::Load(df, sp0 + c), vec_l0, tgt);
      tgt = hn::MulAdd(hn::Load(df, sp1 + c), vec_l1, tgt);
      hn::Store(tgt, df, dp_out + c);
    }
  }

  static void Impl_vlift_32_5x3_synth_s0(int32_t** src, int32_t* dst_in, int32_t* dst_out,
                                         int samples, merc_lifting_step* step)
  {
    const hn::ScalableTag<int32_t> d;
    const size_t N = hn::Lanes(d);
    auto vec_offset = hn::Set(d, (int32_t)((1 << step->downshift) >> 1));
    int32_t *src1 = src[0], *src2 = src[1];
    for(int c = 0; c < samples; c += (int)N)
    {
      auto val = hn::Sub(vec_offset, hn::Load(d, src1 + c));
      val = hn::Sub(val, hn::Load(d, src2 + c));
      val = hn::ShiftRight<1>(val);
      hn::Store(hn::Sub(hn::Load(d, dst_in + c), val), d, dst_out + c);
    }
  }

  static void Impl_vlift_32_5x3_synth_s1(int32_t** src, int32_t* dst_in, int32_t* dst_out,
                                         int samples, merc_lifting_step* step)
  {
    const hn::ScalableTag<int32_t> d;
    const size_t N = hn::Lanes(d);
    auto vec_offset = hn::Set(d, (int32_t)((1 << step->downshift) >> 1));
    int32_t *src1 = src[0], *src2 = src[1];
    for(int c = 0; c < samples; c += (int)N)
    {
      auto val = hn::Add(vec_offset, hn::Load(d, src1 + c));
      val = hn::Add(val, hn::Load(d, src2 + c));
      val = hn::ShiftRight<2>(val);
      hn::Store(hn::Sub(hn::Load(d, dst_in + c), val), d, dst_out + c);
    }
  }

  static void Impl_hlift_16_5x3_synth_s0(int16_t* src, int16_t* dst, int samples)
  {
    // W5X3 step 0 is always downshift=1, icoeffs [-1,-1]
    const hn::ScalableTag<int16_t> d;
    const hn::ScalableTag<uint16_t> du;
    const size_t N = hn::Lanes(d);
    // 17-bit-safe floor((a+b)/2): naive (a+b) wraps int16 (BIBO keeps a,b in
    // int16 but their SUM needs 17 bits). AverageRound in the biased-unsigned
    // domain yields (a+b+1)>>1 exactly; subtracting the odd bit ((a^b)&1) floors.
    auto msk_smin = hn::Set(d, (int16_t)0x8000u);
    auto vec_one = hn::Set(d, (int16_t)1);
    for(int c = 0; c < samples; c += (int)N)
    {
      auto a = hn::LoadU(d, src + c);
      auto b = hn::LoadU(d, src + c + 1);
      auto au = hn::BitCast(du, hn::Xor(a, msk_smin));
      auto bu = hn::BitCast(du, hn::Xor(b, msk_smin));
      auto avg = hn::Xor(hn::BitCast(d, hn::AverageRound(au, bu)), msk_smin);
      auto m = hn::Sub(avg, hn::And(hn::Xor(a, b), vec_one));
      hn::Store(hn::Add(hn::Load(d, dst + c), m), d, dst + c);
    }
  }

  static void Impl_hlift_16_5x3_synth_s1(int16_t* src, int16_t* dst, int samples)
  {
    const hn::ScalableTag<int16_t> d;
    const hn::ScalableTag<uint16_t> du;
    const size_t N = hn::Lanes(d);
    auto msk_smin = hn::Set(d, (int16_t)0x8000u);
    auto msk_smax = hn::Set(d, (int16_t)0x7FFF);
    for(int c = 0; c < samples; c += (int)N)
    {
      auto val1 = hn::Xor(hn::LoadU(d, src + c), msk_smin);
      auto val2 = hn::Add(hn::LoadU(d, src + c + 1), msk_smax);
      auto avg = hn::BitCast(d, hn::AverageRound(hn::BitCast(du, val1), hn::BitCast(du, val2)));
      avg = hn::ShiftRight<1>(hn::Sub(avg, msk_smax));
      hn::Store(hn::Sub(hn::Load(d, dst + c), avg), d, dst + c);
    }
  }

  static void Impl_hlift_32_2tap_irrev(int32_t* src, int32_t* dst, int samples,
                                       merc_lifting_step* step)
  {
    const hn::ScalableTag<float> df;
    const size_t N = hn::Lanes(df);
    const float sign = -1.0f; // synthesis negates the lifting coeffs
    auto vl0 = hn::Set(df, sign * step->coeffs[0]);
    auto vl1 = hn::Set(df, sign * ((step->support_length == 2) ? step->coeffs[1] : 0.0f));
    float* sp = (float*)src;
    for(int c = 0; c < samples; c += (int)N)
    {
      auto tgt = hn::Load(df, (float*)dst + c);
      tgt = hn::MulAdd(hn::LoadU(df, sp + c), vl0, tgt);
      tgt = hn::MulAdd(hn::LoadU(df, sp + c + 1), vl1, tgt);
      hn::Store(tgt, df, (float*)dst + c);
    }
  }

  static void Impl_hlift_32_5x3_synth_s0(int32_t* src, int32_t* dst, int samples,
                                         merc_lifting_step* step)
  {
    const hn::ScalableTag<int32_t> d;
    const size_t N = hn::Lanes(d);
    auto vec_offset = hn::Set(d, (int32_t)((1 << step->downshift) >> 1));
    for(int c = 0; c < samples; c += (int)N)
    {
      auto val = hn::Sub(vec_offset, hn::LoadU(d, src + c));
      val = hn::Sub(val, hn::LoadU(d, src + c + 1));
      val = hn::ShiftRight<1>(val);
      hn::Store(hn::Sub(hn::Load(d, dst + c), val), d, dst + c);
    }
  }

  static void Impl_hlift_32_5x3_synth_s1(int32_t* src, int32_t* dst, int samples,
                                         merc_lifting_step* step)
  {
    const hn::ScalableTag<int32_t> d;
    const size_t N = hn::Lanes(d);
    auto vec_offset = hn::Set(d, (int32_t)((1 << step->downshift) >> 1));
    for(int c = 0; c < samples; c += (int)N)
    {
      auto val = hn::Add(hn::LoadU(d, src + c), vec_offset);
      val = hn::Add(val, hn::LoadU(d, src + c + 1));
      val = hn::ShiftRight<2>(val);
      hn::Store(hn::Sub(hn::Load(d, dst + c), val), d, dst + c);
    }
  }

} // namespace HWY_NAMESPACE
} // namespace mercury_native
HWY_AFTER_NAMESPACE();

// ═══════════════════════════════════════════════════════════════════════════════
// Public API wrappers (only compiled once)
// ═══════════════════════════════════════════════════════════════════════════════

#if HWY_ONCE
namespace mercury_native
{

HWY_EXPORT(Impl_interleave_16);
HWY_EXPORT(Impl_interleave_32);
HWY_EXPORT(Impl_vlift_16_5x3_synth_s0);
HWY_EXPORT(Impl_vlift_16_5x3_synth_s1);
HWY_EXPORT(Impl_vlift_32_2tap_irrev);
HWY_EXPORT(Impl_vlift_32_5x3_synth_s0);
HWY_EXPORT(Impl_vlift_32_5x3_synth_s1);
HWY_EXPORT(Impl_hlift_16_5x3_synth_s0);
HWY_EXPORT(Impl_hlift_16_5x3_synth_s1);
HWY_EXPORT(Impl_hlift_32_2tap_irrev);
HWY_EXPORT(Impl_hlift_32_5x3_synth_s0);
HWY_EXPORT(Impl_hlift_32_5x3_synth_s1);

void hwy_interleave_16(int16_t* s1, int16_t* s2, int16_t* d, int p)
{
  HWY_DYNAMIC_DISPATCH(Impl_interleave_16)(s1, s2, d, p);
}
void hwy_interleave_32(int32_t* s1, int32_t* s2, int32_t* d, int p)
{
  HWY_DYNAMIC_DISPATCH(Impl_interleave_32)(s1, s2, d, p);
}
void hwy_vlift_16_5x3_synth_s0(int16_t** s, int16_t* di, int16_t* d_o, int n)
{
  HWY_DYNAMIC_DISPATCH(Impl_vlift_16_5x3_synth_s0)(s, di, d_o, n);
}
void hwy_vlift_16_5x3_synth_s1(int16_t** s, int16_t* di, int16_t* d_o, int n)
{
  HWY_DYNAMIC_DISPATCH(Impl_vlift_16_5x3_synth_s1)(s, di, d_o, n);
}
void hwy_vlift_32_2tap_irrev(int32_t** s, int32_t* di, int32_t* d_o, int n, merc_lifting_step* st)
{
  HWY_DYNAMIC_DISPATCH(Impl_vlift_32_2tap_irrev)(s, di, d_o, n, st);
}
void hwy_vlift_32_5x3_synth_s0(int32_t** s, int32_t* di, int32_t* d_o, int n, merc_lifting_step* st)
{
  HWY_DYNAMIC_DISPATCH(Impl_vlift_32_5x3_synth_s0)(s, di, d_o, n, st);
}
void hwy_vlift_32_5x3_synth_s1(int32_t** s, int32_t* di, int32_t* d_o, int n, merc_lifting_step* st)
{
  HWY_DYNAMIC_DISPATCH(Impl_vlift_32_5x3_synth_s1)(s, di, d_o, n, st);
}
void hwy_hlift_16_5x3_synth_s0(int16_t* s, int16_t* d, int n)
{
  HWY_DYNAMIC_DISPATCH(Impl_hlift_16_5x3_synth_s0)(s, d, n);
}
void hwy_hlift_16_5x3_synth_s1(int16_t* s, int16_t* d, int n)
{
  HWY_DYNAMIC_DISPATCH(Impl_hlift_16_5x3_synth_s1)(s, d, n);
}
void hwy_hlift_32_2tap_irrev(int32_t* s, int32_t* d, int n, merc_lifting_step* st)
{
  HWY_DYNAMIC_DISPATCH(Impl_hlift_32_2tap_irrev)(s, d, n, st);
}
void hwy_hlift_32_5x3_synth_s0(int32_t* s, int32_t* d, int n, merc_lifting_step* st)
{
  HWY_DYNAMIC_DISPATCH(Impl_hlift_32_5x3_synth_s0)(s, d, n, st);
}
void hwy_hlift_32_5x3_synth_s1(int32_t* s, int32_t* d, int n, merc_lifting_step* st)
{
  HWY_DYNAMIC_DISPATCH(Impl_hlift_32_5x3_synth_s1)(s, d, n, st);
}
} // namespace mercury_native
#endif
