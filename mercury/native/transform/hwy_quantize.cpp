#include <cmath>
#include <cstdint>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "transform/hwy_quantize.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace mercury_native
{
namespace HWY_NAMESPACE
{
  namespace hn = hwy::HWY_NAMESPACE;

  // grok's T1_NMSEDEC_FRACBITS
  static const float FRACTION_SCALE = 64.0f;
  static const uint32_t SIGN_BIT = 0x80000000u;

  // must match grok's Coder::preCompress bit for bit
  static int32_t quantize_one(float sample, double quant)
  {
    return (int32_t)std::lrintf((float)((double)sample * quant) * FRACTION_SCALE);
  }

  static uint32_t Impl_quantize_97(const float* stripe, int stride, int columns, int rows,
                                   double quant, int32_t* block)
  {
    const hn::ScalableTag<double> dd;
    const hn::Rebind<float, decltype(dd)> df;
    const hn::Rebind<int32_t, decltype(dd)> di;
    const int lanes = (int)hn::Lanes(dd);
    const auto quantVector = hn::Set(dd, quant);
    const auto fractionScale = hn::Set(df, FRACTION_SCALE);
    const auto signBit = hn::Set(di, (int32_t)SIGN_BIT);
    auto maxVector = hn::Zero(di);
    int32_t maxScalar = 0;
    for(int row = 0; row < rows; ++row)
    {
      const float* source = stripe + (size_t)row * stride;
      int32_t* target = block + (size_t)row * columns;
      int i = 0;
      for(; i + lanes <= columns; i += lanes)
      {
        const auto scaled = hn::Mul(hn::PromoteTo(dd, hn::LoadU(df, source + i)), quantVector);
        const auto value = hn::NearestInt(hn::Mul(hn::DemoteTo(df, scaled), fractionScale));
        const auto magnitude = hn::Abs(value);
        maxVector = hn::Max(maxVector, magnitude);
        hn::StoreU(hn::Or(magnitude, hn::And(value, signBit)), di, target + i);
      }
      for(; i < columns; ++i)
      {
        const int32_t value = quantize_one(source[i], quant);
        const int32_t magnitude = value < 0 ? -value : value;
        if(magnitude > maxScalar)
          maxScalar = magnitude;
        target[i] = (int32_t)((uint32_t)magnitude | ((uint32_t)value & SIGN_BIT));
      }
    }
    const int32_t maxLanes = hn::ReduceMax(di, maxVector);
    return (uint32_t)(maxLanes > maxScalar ? maxLanes : maxScalar);
  }

} // namespace HWY_NAMESPACE
} // namespace mercury_native
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace mercury_native
{

HWY_EXPORT(Impl_quantize_97);

} // namespace mercury_native

extern "C" uint32_t mercury_hwy_quantize_97(const float* stripe, int stride, int columns, int rows,
                                            double quant, int32_t* block)
{
  return HWY_DYNAMIC_DISPATCH(mercury_native::Impl_quantize_97)(stripe, stride, columns, rows,
                                                                quant, block);
}
#endif
