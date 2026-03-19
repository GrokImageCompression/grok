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

/* Clamp int32_t image data to [minVal, maxVal] using Highway SIMD */
void hwy_clip_i32(int32_t* data, uint32_t w, uint32_t h, uint32_t stride, int32_t minVal,
                  int32_t maxVal);

/* Scale int32_t image data: multiply each element by scale */
void hwy_scale_mul_i32(int32_t* data, uint32_t w, uint32_t h, uint32_t stride, int32_t scale);

/* Scale int32_t image data: divide each element by scale (truncation toward zero) */
void hwy_scale_div_i32(int32_t* data, uint32_t w, uint32_t h, uint32_t stride, int32_t scale);

/* YCC 4:4:4 to RGB conversion using Highway SIMD.
 * Reads from planar y/cb/cr, writes to planar r/g/b.
 * offset = 1 << (prec - 1), upb = (1 << prec) - 1 */
void hwy_sycc444_to_rgb_i32(const int32_t* y, const int32_t* cb, const int32_t* cr, int32_t* r,
                            int32_t* g, int32_t* b, uint32_t w, uint32_t h, uint32_t src_stride,
                            uint32_t dst_stride, int32_t offset, int32_t upb);

/* eYCC to RGB conversion using Highway SIMD.
 * In-place: reads/writes from the same yd/bd/rd arrays. */
void hwy_esycc_to_rgb_i32(int32_t* yd, int32_t* bd, int32_t* rd, uint32_t w, uint32_t h,
                          uint32_t stride, int32_t max_value, int32_t flip_value, bool sign1,
                          bool sign2);

/* Planar int32_t (3 components) → packed uint8_t RGB */
void hwy_planar_to_packed_8(const int32_t* r, const int32_t* g, const int32_t* b, uint8_t* out,
                            uint32_t w, uint32_t h, uint32_t src_stride);

/* Packed uint8_t RGB → planar int32_t (3 components) */
void hwy_packed_to_planar_8(const uint8_t* in, int32_t* r, int32_t* g, int32_t* b, uint32_t w,
                            uint32_t h, uint32_t dst_stride);

/* Planar int32_t (3 components) → packed uint16_t RGB */
void hwy_planar_to_packed_16(const int32_t* r, const int32_t* g, const int32_t* b, uint16_t* out,
                             uint32_t w, uint32_t h, uint32_t src_stride);

/* Packed uint16_t RGB → planar int32_t (3 components) */
void hwy_packed_to_planar_16(const uint16_t* in, int32_t* r, int32_t* g, int32_t* b, uint32_t w,
                             uint32_t h, uint32_t dst_stride);

} // namespace grk
