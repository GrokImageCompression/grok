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

  // must match grok's hwy_quantize16_to_sign_magnitude bit for bit
  static uint32_t Impl_quantize_16(const int16_t* stripe, int stride, int columns, int rows,
                                   double inverseStep, int32_t* block)
  {
    const hn::ScalableTag<int32_t> di;
    const hn::Rebind<int16_t, decltype(di)> d16;
    const hn::Rebind<float, decltype(di)> df;
    const size_t lanes = hn::Lanes(di);
    const auto scale = hn::Set(df, (float)(inverseStep * FRACTION_SCALE));
    const auto signBit = hn::Set(di, (int32_t)SIGN_BIT);
    auto maximum = hn::Zero(di);
    auto convert = [&](auto samples) {
      auto value = hn::NearestInt(hn::Mul(hn::ConvertTo(df, hn::PromoteTo(di, samples)), scale));
      auto magnitude = hn::Abs(value);
      maximum = hn::Max(maximum, magnitude);
      return hn::Or(magnitude, hn::And(value, signBit));
    };
    for(int row = 0; row < rows; ++row)
    {
      const int16_t* source = stripe + (size_t)row * stride;
      int32_t* target = block + (size_t)row * columns;
      size_t i = 0;
      for(; i + lanes <= (size_t)columns; i += lanes)
        hn::StoreU(convert(hn::LoadU(d16, source + i)), di, target + i);
      if(i < (size_t)columns)
        hn::StoreN(convert(hn::LoadN(d16, source + i, columns - i)), di, target + i, columns - i);
    }
    return (uint32_t)hn::ReduceMax(di, maximum);
  }

} // namespace HWY_NAMESPACE
} // namespace mercury_native
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace mercury_native
{

HWY_EXPORT(Impl_quantize_97);
HWY_EXPORT(Impl_quantize_16);

} // namespace mercury_native

extern "C" uint32_t mercury_hwy_quantize_97(const float* stripe, int stride, int columns, int rows,
                                            double quant, int32_t* block)
{
  return HWY_DYNAMIC_DISPATCH(mercury_native::Impl_quantize_97)(stripe, stride, columns, rows,
                                                                quant, block);
}

extern "C" uint32_t mercury_hwy_quantize_16(const int16_t* stripe, int stride, int columns, int rows,
                                            double inverse_step, int32_t* block)
{
  return HWY_DYNAMIC_DISPATCH(mercury_native::Impl_quantize_16)(stripe, stride, columns, rows,
                                                                inverse_step, block);
}
#endif
