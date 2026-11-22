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

namespace grk
{

constexpr float alpha = -1.586134342f;
constexpr float beta = -0.052980118f;
constexpr float gamma = 0.882911075f;
constexpr float delta = 0.443506852f;
constexpr float K = 1.230174105f;
constexpr float invK = (float)(1.0 / 1.230174105);

constexpr int16_t q15(double value)
{
  double scaled = value * 32768.0;
  return (int16_t)(scaled < 0 ? scaled - 0.5 : scaled + 0.5);
}
constexpr int16_t fwd97_g = q15(2.0 + alpha);
constexpr int16_t fwd97_4beta = q15(4.0 * beta);
constexpr int16_t fwd97_half_gamma = q15(gamma / 2.0);
constexpr int16_t fwd97_2delta = q15(2.0 * delta);
constexpr int16_t fwd97_unhalved_0 = q15(1.0 + alpha);
constexpr int16_t fwd97_unhalved_2beta = q15(2.0 * beta);
constexpr int16_t fwd97_unhalved_gamma = q15(gamma);
constexpr int16_t fwd97_unhalved_delta = q15(delta);

} // namespace grk
