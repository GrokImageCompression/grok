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

#include <cstdint>

#include "grok.h"
#include "Quantizer.h"

namespace grk
{

const uint8_t maxQfactor = 100;

enum class ChromaSubsampling : uint8_t
{
  full, // 4:4:4
  halfBoth, // 4:2:0
  halfHorizontal // 4:2:2
};

ChromaSubsampling chromaSubsampling(const grk_image* image);

/**
 * @brief Expounded 9/7 step sizes for a JPEG style quality factor (1..100)
 *
 * Follows the OpenHTJ2K / Kakadu Qfactor model: a reference step from the quality
 * factor, divided by the 9/7 synthesis gain of each band, a visual weight per band
 * and an ICT gain per component. Step sizes are written in codestream order,
 * LL first then HL, LH, HH per resolution from the coarsest.
 */
void generateQfactorStepsizes(uint8_t qfactor, uint8_t numDecompositions, uint8_t bitDepth,
                              uint16_t compno, ChromaSubsampling subsampling,
                              grk_stepsize* stepsizes);

} // namespace grk
