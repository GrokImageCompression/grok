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
#define HWY_TARGET_INCLUDE "util/XYZTransform.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "grok.h"
#include "XYZTransform.h"
#include "Logger.h"

HWY_BEFORE_NAMESPACE();
namespace grk
{
namespace HWY_NAMESPACE
{
  namespace hn = hwy::HWY_NAMESPACE;

  // ─── DCI companding coefficient (SMPTE 428-1) ───
  // Peak white luminance 48 cd/m² mapped to XYZ encoding range of 52.37
  static constexpr float DCI_COEFFICIENT = 48.0f / 52.37f;

  // ─── Rec.709 linearization: simple gamma 2.2 (matches libdcp/dcpomatic) ───
  static inline float rec709_to_linear(float v)
  {
    if(v <= 0.0f)
      return 0.0f;
    return std::pow(v, 2.2f);
  }

  // ─── DCI 2.6 gamma: linear → X'Y'Z' ───
  static inline float linear_to_dci_gamma(float v)
  {
    if(v <= 0.0f)
      return 0.0f;
    return std::pow(v, 1.0f / 2.6f);
  }

  // ─── Rec.709 → CIE XYZ matrix (D65 white point), pre-multiplied by DCI companding ───
  static constexpr float M00 = 0.4124564f * DCI_COEFFICIENT;
  static constexpr float M01 = 0.3575761f * DCI_COEFFICIENT;
  static constexpr float M02 = 0.1804375f * DCI_COEFFICIENT;
  static constexpr float M10 = 0.2126729f * DCI_COEFFICIENT;
  static constexpr float M11 = 0.7151522f * DCI_COEFFICIENT;
  static constexpr float M12 = 0.0721750f * DCI_COEFFICIENT;
  static constexpr float M20 = 0.0193339f * DCI_COEFFICIENT;
  static constexpr float M21 = 0.1191920f * DCI_COEFFICIENT;
  static constexpr float M22 = 0.9503041f * DCI_COEFFICIENT;

  /**
   * SIMD-accelerated RGB → X'Y'Z' transform.
   *
   * Processes planar int32 component buffers. The gamma curves use scalar
   * LUTs (pre-built for the given precision), while the 3×3 matrix multiply
   * uses Highway SIMD float vectors.
   */
  static void Hwy_rgb_to_xyz(int32_t* HWY_RESTRICT rBuf, int32_t* HWY_RESTRICT gBuf,
                             int32_t* HWY_RESTRICT bBuf, uint64_t numPixels, uint32_t prec)
  {
    const uint32_t maxVal = (1u << prec) - 1;
    const float scale = 1.0f / (float)maxVal;

    // Build linearization LUT: [0..maxVal] → float linear
    const uint32_t lutSize = maxVal + 1;
    auto linLut = std::make_unique<float[]>(lutSize);
    for(uint32_t v = 0; v < lutSize; ++v)
      linLut[v] = rec709_to_linear((float)v * scale);

    // Build DCI gamma LUT at output precision (e.g. 4096 entries for 12-bit)
    // This fits in L1 cache unlike the old 256KB 16-bit LUT
    auto gammaLut = std::make_unique<float[]>(lutSize);
    for(uint32_t v = 0; v < lutSize; ++v)
      gammaLut[v] = linear_to_dci_gamma((float)v * scale);

    const float gammaScale = (float)maxVal;

    // Fused single-pass: linearize → matrix → gamma → quantize per pixel
    // This avoids intermediate float buffers and keeps everything in L1
    for(uint64_t i = 0; i < numPixels; ++i)
    {
      // Linearize via LUT
      uint32_t rv = (uint32_t)rBuf[i];
      uint32_t gv = (uint32_t)gBuf[i];
      uint32_t bv = (uint32_t)bBuf[i];
      if(rv > maxVal)
        rv = maxVal;
      if(gv > maxVal)
        gv = maxVal;
      if(bv > maxVal)
        bv = maxVal;

      float r = linLut[rv];
      float g = linLut[gv];
      float b = linLut[bv];

      // Matrix multiply (scalar — 9 FMAs)
      float x = M00 * r + M01 * g + M02 * b;
      float y = M10 * r + M11 * g + M12 * b;
      float z = M20 * r + M21 * g + M22 * b;

      // Gamma via LUT (quantize linear to output precision for indexing)
      auto quantize = [maxVal, gammaScale](float v) -> uint32_t {
        if(v <= 0.0f)
          return 0;
        if(v >= 1.0f)
          return maxVal;
        return (uint32_t)(v * gammaScale + 0.5f);
      };

      rBuf[i] = (int32_t)(gammaLut[quantize(x)] * gammaScale + 0.5f);
      gBuf[i] = (int32_t)(gammaLut[quantize(y)] * gammaScale + 0.5f);
      bBuf[i] = (int32_t)(gammaLut[quantize(z)] * gammaScale + 0.5f);
    }
  }

} // namespace HWY_NAMESPACE
} // namespace grk
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace grk
{

HWY_EXPORT(Hwy_rgb_to_xyz);

bool applyXYZTransform(grk_image* image)
{
  if(!image || image->numcomps < 3)
  {
    grklog.error("XYZ transform requires an image with at least 3 components");
    return false;
  }

  auto& compR = image->comps[0];
  auto& compG = image->comps[1];
  auto& compB = image->comps[2];

  if(!compR.data || !compG.data || !compB.data)
  {
    grklog.error("XYZ transform: component data is null");
    return false;
  }

  // All components must have same dimensions and precision
  if(compR.w != compG.w || compR.w != compB.w || compR.h != compG.h || compR.h != compB.h ||
     compR.prec != compG.prec || compR.prec != compB.prec)
  {
    grklog.error("XYZ transform: components must have identical dimensions and precision");
    return false;
  }

  uint32_t w = compR.w;
  uint32_t h = compR.h;
  uint32_t prec = compR.prec;

  grklog.info("XYZ transform: %ux%u, %u-bit, strides: R=%u G=%u B=%u, data_type: R=%d G=%d B=%d", w,
              h, prec, compR.stride, compG.stride, compB.stride, (int)compR.data_type,
              (int)compG.data_type, (int)compB.data_type);

  // If data is stored with stride, we need to process row-by-row
  // For contiguous data (stride == w), we can process in one shot
  bool contiguous = (compR.stride == w) && (compG.stride == w) && (compB.stride == w);

  if(contiguous)
  {
    uint64_t numPixels = (uint64_t)w * h;
    auto rBuf = (int32_t*)compR.data;
    auto gBuf = (int32_t*)compG.data;
    auto bBuf = (int32_t*)compB.data;
    HWY_DYNAMIC_DISPATCH(Hwy_rgb_to_xyz)(rBuf, gBuf, bBuf, numPixels, prec);
  }
  else
  {
    // Row-by-row processing for strided data
    // Allocate temporary contiguous buffers
    auto rRow = std::make_unique<int32_t[]>(w);
    auto gRow = std::make_unique<int32_t[]>(w);
    auto bRow = std::make_unique<int32_t[]>(w);

    for(uint32_t y = 0; y < h; ++y)
    {
      auto rPtr = (int32_t*)compR.data + (uint64_t)y * compR.stride;
      auto gPtr = (int32_t*)compG.data + (uint64_t)y * compG.stride;
      auto bPtr = (int32_t*)compB.data + (uint64_t)y * compB.stride;

      // Copy row to contiguous buffer
      memcpy(rRow.get(), rPtr, w * sizeof(int32_t));
      memcpy(gRow.get(), gPtr, w * sizeof(int32_t));
      memcpy(bRow.get(), bPtr, w * sizeof(int32_t));

      HWY_DYNAMIC_DISPATCH(Hwy_rgb_to_xyz)(rRow.get(), gRow.get(), bRow.get(), w, prec);

      // Copy back
      memcpy(rPtr, rRow.get(), w * sizeof(int32_t));
      memcpy(gPtr, gRow.get(), w * sizeof(int32_t));
      memcpy(bPtr, bRow.get(), w * sizeof(int32_t));
    }
  }

  // Update colour space to XYZ
  image->color_space = GRK_CLRSPC_CUSTOM_CIE;

  grklog.info("Applied Rec.709 RGB → DCI X'Y'Z' colour transform (%ux%u, %u-bit)", w, h, prec);
  return true;
}

} // namespace grk
#endif // HWY_ONCE
