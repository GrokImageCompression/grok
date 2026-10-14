#include <cstdint>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "transform/hwy_ict.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace mercury_native
{
namespace HWY_NAMESPACE
{
  namespace hn = hwy::HWY_NAMESPACE;
  using namespace hn;

  // grok's CompressIrrev constants and expressions, which must match bit for bit
  static const float a_r = 0.299f;
  static const float a_g = 0.587f;
  static const float a_b = 0.114f;
  static const float cb = 0.5f / (1.0f - a_b);
  static const float cr = 0.5f / (1.0f - a_r);

  static void Impl_forward_ict(const int32_t* red, const int32_t* green, const int32_t* blue,
                               int32_t dcShift, float* luma, float* chromaBlue, float* chromaRed,
                               int samples)
  {
    const HWY_FULL(float) df;
    const HWY_FULL(int32_t) di;
    auto va_r = Set(df, a_r);
    auto va_g = Set(df, a_g);
    auto va_b = Set(df, a_b);
    auto vcb = Set(df, cb);
    auto vcr = Set(df, cr);
    auto vdc = Set(di, dcShift);
    const int lanes = (int)Lanes(di);
    int j = 0;
    for(; j + lanes <= samples; j += lanes)
    {
      auto r = ConvertTo(df, LoadU(di, red + j) + vdc);
      auto g = ConvertTo(df, LoadU(di, green + j) + vdc);
      auto b = ConvertTo(df, LoadU(di, blue + j) + vdc);
      auto y = va_r * r + va_g * g + va_b * b;
      auto u = vcb * (b - y);
      auto v = vcr * (r - y);
      StoreU(y, df, luma + j);
      StoreU(u, df, chromaBlue + j);
      StoreU(v, df, chromaRed + j);
    }
    if(j < samples)
    {
      const size_t count = (size_t)(samples - j);
      auto r = ConvertTo(df, LoadN(di, red + j, count) + vdc);
      auto g = ConvertTo(df, LoadN(di, green + j, count) + vdc);
      auto b = ConvertTo(df, LoadN(di, blue + j, count) + vdc);
      auto y = va_r * r + va_g * g + va_b * b;
      auto u = vcb * (b - y);
      auto v = vcr * (r - y);
      StoreN(y, df, luma + j, count);
      StoreN(u, df, chromaBlue + j, count);
      StoreN(v, df, chromaRed + j, count);
    }
  }

} // namespace HWY_NAMESPACE
} // namespace mercury_native
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace mercury_native
{

HWY_EXPORT(Impl_forward_ict);

} // namespace mercury_native

#if defined(_WIN32)
#define MERCURY_ICT_API __declspec(dllexport)
#else
#define MERCURY_ICT_API __attribute__((visibility("default")))
#endif

extern "C" MERCURY_ICT_API void mercury_hwy_forward_ict(const int32_t* red, const int32_t* green,
                                                        const int32_t* blue, int32_t dc_shift,
                                                        float* luma, float* chroma_blue,
                                                        float* chroma_red, int samples)
{
  HWY_DYNAMIC_DISPATCH(mercury_native::Impl_forward_ict)(red, green, blue, dc_shift, luma,
                                                         chroma_blue, chroma_red, samples);
}
#endif
