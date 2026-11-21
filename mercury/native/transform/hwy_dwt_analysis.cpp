#include "transform/dwt_kernels.h"

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

} // namespace mercury_native
#endif
