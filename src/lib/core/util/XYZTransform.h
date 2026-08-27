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

#pragma once

#include "grok.h"

namespace grk
{

/**
 * Apply Rec.709 RGB → DCI X'Y'Z' colour transform to an image in-place.
 *
 * Operates on planar int32 component buffers (comp[0..2].data).
 * Pixel values are interpreted as unsigned integers in [0, (1<<prec)-1].
 *
 * Transform pipeline per pixel:
 *   1. Normalize to [0,1]
 *   2. Rec.709 OETF⁻¹ (gamma → linear)
 *   3. 3×3 matrix (linear Rec.709 RGB → linear CIE XYZ, D65 white point)
 *   4. DCI 2.6 gamma (linear → X'Y'Z')
 *   5. Quantize to [0, (1<<targetPrec)-1] and update component precision
 *
 * Uses Highway SIMD for the matrix multiply; scalar powf for gamma curves
 * (with optional LUT acceleration for common precisions).
 *
 * @param image       Image with ≥3 components. Only components 0,1,2 are modified.
 * @param targetPrec  Output precision. 0 keeps the input precision. Any other
 *                    value is what the output is quantized to, whether the
 *                    source is deeper or shallower: linearization always runs
 *                    at the source precision, so a 16-bit source loses nothing
 *                    before the quantization and an 8-bit one is widened.
 * @return true on success, false if image has <3 components.
 */
GRK_API bool applyXYZTransform(grk_image* image, uint8_t targetPrec = 0);

/**
 * Output precision the transform emits for a code stream declaring `rsiz`:
 * the cinema profiles require 12-bit samples, everything else keeps the
 * source precision.
 */
inline uint8_t xyzTargetPrecision(uint16_t rsiz)
{
  return GRK_IS_CINEMA(rsiz) ? 12 : 0;
}

} // namespace grk
