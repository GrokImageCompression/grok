#include <cmath>
#include <cstdint>
#include <vector>

#ifndef MERCURY_XYZ_TABLES
#define MERCURY_XYZ_TABLES
// grok's Hwy_rgb_to_xyz lookup tables
struct MercuryXyzTables
{
  uint32_t maxValue;
  uint32_t gammaLutMax;
  float gammaScale;
  float outScale;
  std::vector<float> linear;
  std::vector<float> gamma;
};
#endif

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

  template<class DF, class V>
  HWY_INLINE void ict(DF df, V r, V g, V b, V& y, V& u, V& v)
  {
    y = Set(df, a_r) * r + Set(df, a_g) * g + Set(df, a_b) * b;
    u = Set(df, cb) * (b - y);
    v = Set(df, cr) * (r - y);
  }

  static void Impl_forward_ict(const int32_t* red, const int32_t* green, const int32_t* blue,
                               int32_t dcShift, float* luma, float* chromaBlue, float* chromaRed,
                               int samples)
  {
    const HWY_FULL(float) df;
    const HWY_FULL(int32_t) di;
    auto vdc = Set(di, dcShift);
    const int lanes = (int)Lanes(di);
    Vec<decltype(df)> y, u, v;
    int j = 0;
    for(; j + lanes <= samples; j += lanes)
    {
      auto r = ConvertTo(df, LoadU(di, red + j) + vdc);
      auto g = ConvertTo(df, LoadU(di, green + j) + vdc);
      auto b = ConvertTo(df, LoadU(di, blue + j) + vdc);
      ict(df, r, g, b, y, u, v);
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
      ict(df, r, g, b, y, u, v);
      StoreN(y, df, luma + j, count);
      StoreN(u, df, chromaBlue + j, count);
      StoreN(v, df, chromaRed + j, count);
    }
  }

  // grok's Hwy_rgb_to_xyz matrix, which gcc contracts to fma(M02, b, fma(M00, r, M01 * g))
  static constexpr float DCI_COEFFICIENT = 48.0f / 52.37f;
  static constexpr float M00 = 0.4124564f * DCI_COEFFICIENT;
  static constexpr float M01 = 0.3575761f * DCI_COEFFICIENT;
  static constexpr float M02 = 0.1804375f * DCI_COEFFICIENT;
  static constexpr float M10 = 0.2126729f * DCI_COEFFICIENT;
  static constexpr float M11 = 0.7151522f * DCI_COEFFICIENT;
  static constexpr float M12 = 0.0721750f * DCI_COEFFICIENT;
  static constexpr float M20 = 0.0193339f * DCI_COEFFICIENT;
  static constexpr float M21 = 0.1191920f * DCI_COEFFICIENT;
  static constexpr float M22 = 0.9503041f * DCI_COEFFICIENT;

  // gcc and clang fuse classic's scalar a * b + c, MSVC does not
  template<class V>
  HWY_INLINE V classic_mul_add(V a, V b, V c)
  {
#if defined(_MSC_VER) && !defined(__clang__)
    return Add(Mul(a, b), c);
#else
    return MulAdd(a, b, c);
#endif
  }

  template<class DF, class DI, class VI>
  HWY_INLINE VI xyz_component(DF df, DI di, const MercuryXyzTables& tables, VI r, VI g, VI b,
                              float m0, float m1, float m2)
  {
    auto rl = GatherIndex(df, tables.linear.data(), r);
    auto gl = GatherIndex(df, tables.linear.data(), g);
    auto bl = GatherIndex(df, tables.linear.data(), b);
    auto linear =
        classic_mul_add(Set(df, m2), bl, classic_mul_add(Set(df, m0), rl, Mul(Set(df, m1), gl)));
    auto half = Set(df, 0.5f);
    auto index = ConvertTo(di, classic_mul_add(linear, Set(df, tables.gammaScale), half));
    index = IfThenElse(RebindMask(di, Ge(linear, Set(df, 1.0f))),
                       Set(di, (int32_t)tables.gammaLutMax), index);
    index = IfThenZeroElse(RebindMask(di, Le(linear, Zero(df))), index);
    auto gamma = GatherIndex(df, tables.gamma.data(), index);
    return ConvertTo(di, classic_mul_add(gamma, Set(df, tables.outScale), half));
  }

  template<class DF, class DI, class VI>
  HWY_INLINE void xyz_ict(DF df, DI di, const MercuryXyzTables& tables, VI r, VI g, VI b,
                          VI dcShift, Vec<DF>& y, Vec<DF>& u, Vec<DF>& v)
  {
    auto x = xyz_component(df, di, tables, r, g, b, M00, M01, M02);
    auto yy = xyz_component(df, di, tables, r, g, b, M10, M11, M12);
    auto z = xyz_component(df, di, tables, r, g, b, M20, M21, M22);
    ict(df, ConvertTo(df, x + dcShift), ConvertTo(df, yy + dcShift), ConvertTo(df, z + dcShift),
        y, u, v);
  }

  static void Impl_forward_xyz_ict(const MercuryXyzTables* tables, const int32_t* red,
                                   const int32_t* green, const int32_t* blue, int32_t dcShift,
                                   float* luma, float* chromaBlue, float* chromaRed, int samples)
  {
    const HWY_FULL(float) df;
    const HWY_FULL(int32_t) di;
    const HWY_FULL(uint32_t) du;
    auto vdc = Set(di, dcShift);
    auto maxValue = Set(du, tables->maxValue);
    // a negative sample clamps to the maximum, as in classic's unsigned read
    auto clamp = [&](Vec<decltype(di)> sample) {
      return BitCast(di, Min(BitCast(du, sample), maxValue));
    };
    const int lanes = (int)Lanes(di);
    Vec<decltype(df)> y, u, v;
    int j = 0;
    for(; j + lanes <= samples; j += lanes)
    {
      auto r = clamp(LoadU(di, red + j));
      auto g = clamp(LoadU(di, green + j));
      auto b = clamp(LoadU(di, blue + j));
      xyz_ict(df, di, *tables, r, g, b, vdc, y, u, v);
      StoreU(y, df, luma + j);
      StoreU(u, df, chromaBlue + j);
      StoreU(v, df, chromaRed + j);
    }
    if(j < samples)
    {
      const size_t count = (size_t)(samples - j);
      auto r = clamp(LoadN(di, red + j, count));
      auto g = clamp(LoadN(di, green + j, count));
      auto b = clamp(LoadN(di, blue + j, count));
      xyz_ict(df, di, *tables, r, g, b, vdc, y, u, v);
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
HWY_EXPORT(Impl_forward_xyz_ict);

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

extern "C" MERCURY_ICT_API MercuryXyzTables* mercury_xyz_tables_new(uint32_t precision,
                                                                    uint32_t out_precision)
{
  auto tables = new MercuryXyzTables;
  tables->maxValue = (1u << precision) - 1;
  const float scale = 1.0f / (float)tables->maxValue;
  tables->outScale = (float)((1u << out_precision) - 1);
  tables->linear.resize(tables->maxValue + 1);
  for(uint32_t v = 0; v <= tables->maxValue; ++v)
  {
    float value = (float)v * scale;
    tables->linear[v] = value <= 0.0f ? 0.0f : std::pow(value, 2.2f);
  }
  tables->gammaLutMax = (1u << (precision > out_precision ? precision : out_precision)) - 1;
  const float gammaIndexScale = 1.0f / (float)tables->gammaLutMax;
  tables->gamma.resize(tables->gammaLutMax + 1);
  for(uint32_t v = 0; v <= tables->gammaLutMax; ++v)
  {
    float value = (float)v * gammaIndexScale;
    tables->gamma[v] = value <= 0.0f ? 0.0f : std::pow(value, 1.0f / 2.6f);
  }
  tables->gammaScale = (float)tables->gammaLutMax;
  return tables;
}

extern "C" MERCURY_ICT_API void mercury_xyz_tables_free(MercuryXyzTables* tables)
{
  delete tables;
}

extern "C" MERCURY_ICT_API void
    mercury_hwy_forward_xyz_ict(const MercuryXyzTables* tables, const int32_t* red,
                                const int32_t* green, const int32_t* blue, int32_t dc_shift,
                                float* luma, float* chroma_blue, float* chroma_red, int samples)
{
  HWY_DYNAMIC_DISPATCH(mercury_native::Impl_forward_xyz_ict)(
      tables, red, green, blue, dc_shift, luma, chroma_blue, chroma_red, samples);
}
#endif
