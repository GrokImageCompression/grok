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
 *   5. Quantize back to [0, (1<<prec)-1]
 *
 * Uses Highway SIMD for the matrix multiply; scalar powf for gamma curves
 * (with optional LUT acceleration for common precisions).
 *
 * @param image  Image with ≥3 components. Only components 0,1,2 are modified.
 * @return true on success, false if image has <3 components.
 */
GRK_API bool applyXYZTransform(grk_image* image);

} // namespace grk
