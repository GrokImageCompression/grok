#include "transform/dwt_kernels.h"

#include <cmath>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "transform/hwy_dwt_analysis.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace mercury_native
{
namespace HWY_NAMESPACE
{
  namespace hn = hwy::HWY_NAMESPACE;

  // must match grok's forward 9/7 constants and expressions bit for bit
  static const float alpha = -1.586134342f;
  static const float beta = -0.052980118f;
  static const float gamma = 0.882911075f;
  static const float delta = 0.443506852f;
  static const float K = 1.230174105f;
  static const float invK = (float)(1.0 / 1.230174105);

  template<bool SCALED>
  static void lift_97(float* target, const float* left, const float* right, int samples,
                      float coeff, float scale)
  {
    const hn::ScalableTag<float> d;
    const int lanes = (int)hn::Lanes(d);
    const auto c = hn::Set(d, coeff);
    const auto sf = hn::Set(d, scale);
    int i = 0;
    for(; i + lanes <= samples; i += lanes)
    {
      const auto curr = hn::LoadU(d, target + i);
      const auto prev = hn::LoadU(d, left + i);
      const auto next = hn::LoadU(d, right + i);
      if constexpr(SCALED)
        hn::StoreU((curr + (prev + next) * c) * sf, d, target + i);
      else
        hn::StoreU(curr + (prev + next) * c, d, target + i);
    }
    if(i < samples)
    {
      const size_t rest = (size_t)(samples - i);
      const auto curr = hn::LoadN(d, target + i, rest);
      const auto prev = hn::LoadN(d, left + i, rest);
      const auto next = hn::LoadN(d, right + i, rest);
      if constexpr(SCALED)
        hn::StoreN((curr + (prev + next) * c) * sf, d, target + i, rest);
      else
        hn::StoreN(curr + (prev + next) * c, d, target + i, rest);
    }
  }

  static void Impl_analysis_lift_97(int step, float* target, const float* left, const float* right,
                                    int samples)
  {
    switch(step)
    {
      case 0:
        lift_97<false>(target, left, right, samples, alpha, 1.0f);
        break;
      case 1:
        lift_97<false>(target, left, right, samples, beta, 1.0f);
        break;
      case 2:
        lift_97<true>(target, left, right, samples, gamma, K);
        break;
      case 3:
        // the high pass neighbours were stored already scaled by K
        lift_97<true>(target, left, right, samples, delta * invK, invK);
        break;
      default:
        break;
    }
  }

  static void Impl_analysis_deinterleave_32(const uint32_t* row, uint32_t* even, uint32_t* odd,
                                            int pairs)
  {
    const hn::ScalableTag<uint32_t> d;
    const int lanes = (int)hn::Lanes(d);
    int i = 0;
    for(; i + lanes <= pairs; i += lanes)
    {
      hn::Vec<decltype(d)> even_samples, odd_samples;
      hn::LoadInterleaved2(d, row + 2 * i, even_samples, odd_samples);
      hn::StoreU(even_samples, d, even + i);
      hn::StoreU(odd_samples, d, odd + i);
    }
    for(; i < pairs; ++i)
    {
      even[i] = row[2 * i];
      odd[i] = row[2 * i + 1];
    }
  }

  static int16_t q15(double value)
  {
    return (int16_t)std::lround(value * 32768.0);
  }
  // grok's encode_97_16_v and encode_97_16_h constants and expressions
  static const int16_t fwd97_g = q15(2.0 + alpha);
  static const int16_t fwd97_4beta = q15(4.0 * beta);
  static const int16_t fwd97_half_gamma = q15(gamma / 2.0);
  static const int16_t fwd97_2delta = q15(2.0 * delta);
  static const int16_t fwd97_unhalved_0 = q15(1.0 + alpha);
  static const int16_t fwd97_unhalved_2beta = q15(2.0 * beta);
  static const int16_t fwd97_unhalved_gamma = q15(gamma);
  static const int16_t fwd97_unhalved_delta = q15(delta);
  static const int32_t UNIT_FACTOR_Q15 = 32768;

  template<class D, class V>
  HWY_INLINE V halveTiesToEven(D d, V x)
  {
    return hn::ShiftRight<1>(x + hn::And(hn::ShiftRight<1>(x), hn::Set(d, (int16_t)1)));
  }

  template<class D, class V>
  HWY_INLINE V average16(D d, V a, V b)
  {
    const hn::RebindToUnsigned<D> du;
    const auto sign = hn::Set(du, (uint16_t)0x8000);
    auto roundedUp = hn::BitCast(
        d, hn::Xor(hn::AverageRound(hn::Xor(hn::BitCast(du, a), sign),
                                    hn::Xor(hn::BitCast(du, b), sign)),
                   sign));
    auto oddTie = hn::And(hn::And(hn::Xor(a, b), roundedUp), hn::Set(d, (int16_t)1));
    return roundedUp - oddTie;
  }

  template<class Op>
  static void lift_16(int16_t* target, const int16_t* left, const int16_t* right, int samples,
                      Op op)
  {
    const hn::ScalableTag<int16_t> d;
    const int lanes = (int)hn::Lanes(d);
    int i = 0;
    for(; i + lanes <= samples; i += lanes)
      hn::StoreU(op(d, hn::LoadU(d, target + i), hn::LoadU(d, left + i), hn::LoadU(d, right + i)),
                 d, target + i);
    if(i < samples)
    {
      const size_t rest = (size_t)(samples - i);
      hn::StoreN(op(d, hn::LoadN(d, target + i, rest), hn::LoadN(d, left + i, rest),
                    hn::LoadN(d, right + i, rest)),
                 d, target + i, rest);
    }
  }

  static void Impl_analysis_lift_97_16(int step, int32_t factorQ15, int16_t* target,
                                       const int16_t* left, const int16_t* right, int samples)
  {
    const hn::ScalableTag<int16_t> d;
    const bool unitFactor = factorQ15 == UNIT_FACTOR_Q15;
    const auto halfFactor = hn::Set(d, (int16_t)((factorQ15 + 1) >> 1));
    const auto halfFactorAlpha =
        hn::Set(d, (int16_t)std::lround((double)factorQ15 * alpha / 2.0));
    const auto levelFactor = hn::Set(d, (int16_t)factorQ15);
    switch(step)
    {
      case 0:
        lift_16(target, left, right, samples, [&](auto dd, auto value, auto l, auto r) {
          if(!unitFactor)
            return hn::MulFixedPoint15(value, halfFactor) +
                   hn::MulFixedPoint15(l + r, halfFactorAlpha);
          auto sum = l + r;
          return halveTiesToEven(dd, value + hn::MulFixedPoint15(sum, hn::Set(dd, fwd97_g))) -
                 sum;
        });
        break;
      case 1:
        lift_16(target, left, right, samples, [&](auto dd, auto value, auto l, auto r) {
          auto scaled = unitFactor ? value : hn::MulFixedPoint15(value, levelFactor);
          return scaled + hn::MulFixedPoint15(average16(dd, l, r), hn::Set(dd, fwd97_4beta));
        });
        break;
      case 2:
        lift_16(target, left, right, samples, [](auto dd, auto value, auto l, auto r) {
          return value + hn::MulFixedPoint15(l + r, hn::Set(dd, fwd97_half_gamma));
        });
        break;
      case 3:
        lift_16(target, left, right, samples, [](auto dd, auto value, auto l, auto r) {
          return value + hn::MulFixedPoint15(l + r, hn::Set(dd, fwd97_2delta));
        });
        break;
      default:
        break;
    }
  }

  static void Impl_analysis_lift_97_16_unhalved(int step, int16_t* target, const int16_t* left,
                                                const int16_t* right, int samples)
  {
    switch(step)
    {
      case 0:
        lift_16(target, left, right, samples, [](auto dd, auto value, auto l, auto r) {
          auto sum = l + r;
          return value - sum + hn::MulFixedPoint15(sum, hn::Set(dd, fwd97_unhalved_0));
        });
        break;
      case 1:
        lift_16(target, left, right, samples, [](auto dd, auto value, auto l, auto r) {
          return value +
                 hn::MulFixedPoint15(average16(dd, l, r), hn::Set(dd, fwd97_unhalved_2beta));
        });
        break;
      case 2:
        lift_16(target, left, right, samples, [](auto dd, auto value, auto l, auto r) {
          return value + hn::MulFixedPoint15(l + r, hn::Set(dd, fwd97_unhalved_gamma));
        });
        break;
      case 3:
        lift_16(target, left, right, samples, [](auto dd, auto value, auto l, auto r) {
          return value + hn::MulFixedPoint15(l + r, hn::Set(dd, fwd97_unhalved_delta));
        });
        break;
      default:
        break;
    }
  }

  static void Impl_analysis_deinterleave_16(const uint16_t* row, uint16_t* even, uint16_t* odd,
                                            int pairs)
  {
    const hn::ScalableTag<uint16_t> d;
    const int lanes = (int)hn::Lanes(d);
    int i = 0;
    for(; i + lanes <= pairs; i += lanes)
    {
      hn::Vec<decltype(d)> even_samples, odd_samples;
      hn::LoadInterleaved2(d, row + 2 * i, even_samples, odd_samples);
      hn::StoreU(even_samples, d, even + i);
      hn::StoreU(odd_samples, d, odd + i);
    }
    for(; i < pairs; ++i)
    {
      even[i] = row[2 * i];
      odd[i] = row[2 * i + 1];
    }
  }

} // namespace HWY_NAMESPACE
} // namespace mercury_native
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace mercury_native
{

HWY_EXPORT(Impl_analysis_lift_97);

void hwy_analysis_lift_97(int step, float* target, const float* left, const float* right,
                          int samples)
{
  HWY_DYNAMIC_DISPATCH(Impl_analysis_lift_97)(step, target, left, right, samples);
}

HWY_EXPORT(Impl_analysis_deinterleave_32);

void hwy_analysis_deinterleave_32(const uint32_t* row, uint32_t* even, uint32_t* odd, int pairs)
{
  HWY_DYNAMIC_DISPATCH(Impl_analysis_deinterleave_32)(row, even, odd, pairs);
}

HWY_EXPORT(Impl_analysis_lift_97_16);

void hwy_analysis_lift_97_16(int step, int32_t factorQ15, int16_t* target, const int16_t* left,
                             const int16_t* right, int samples)
{
  HWY_DYNAMIC_DISPATCH(Impl_analysis_lift_97_16)(step, factorQ15, target, left, right, samples);
}

HWY_EXPORT(Impl_analysis_lift_97_16_unhalved);

void hwy_analysis_lift_97_16_unhalved(int step, int16_t* target, const int16_t* left,
                                      const int16_t* right, int samples)
{
  HWY_DYNAMIC_DISPATCH(Impl_analysis_lift_97_16_unhalved)(step, target, left, right, samples);
}

HWY_EXPORT(Impl_analysis_deinterleave_16);

void hwy_analysis_deinterleave_16(const uint16_t* row, uint16_t* even, uint16_t* odd, int pairs)
{
  HWY_DYNAMIC_DISPATCH(Impl_analysis_deinterleave_16)(row, even, odd, pairs);
}

} // namespace mercury_native
#endif
