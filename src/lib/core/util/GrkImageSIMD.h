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

/* Visibility macro for functions that need to be accessible from the codec library */
#if defined(_WIN32) && defined(GRK_EXPORTS)
#define GRK_SIMD_API __declspec(dllexport)
#elif defined(_WIN32)
#define GRK_SIMD_API __declspec(dllimport)
#elif !defined(GRK_STATIC)
#define GRK_SIMD_API __attribute__((visibility("default")))
#else
#define GRK_SIMD_API
#endif

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

/**
 * Copy a decoded tile image (int32_t planar) into a swath output buffer described
 * by grk_swath_buffer.  Handles any output type (uint8, int16, uint16, int32, uint32)
 * and any GDAL-style pixel/line/band spacing (BIP, BSQ, etc.).
 * Conversion follows GDALCopyWords semantics: values are clamped to the output range.
 * Assumes component subsampling dx == dy == 1.
 */
void hwy_copy_tile_to_swath(const grk_image* tile_img, const grk_swath_buffer* buf);

/* ─── Format-level conversion SIMD primitives ─── */

/* Unpack packed uint8 bytes → int32 array, with optional bitwise invert (XOR 0xFF).
 * Equivalent to the N=8 unsigned path of convertToOutput. */
GRK_SIMD_API void hwy_unpack_8u_to_i32(const uint8_t* src, int32_t* dest, size_t w, bool invert);

/* Unpack packed uint8 bytes → int32 array with sign extension from 8 bits,
 * plus optional bitwise invert.
 * Equivalent to the N=8 signed path of convertToOutput. */
GRK_SIMD_API void hwy_unpack_8s_to_i32(const uint8_t* src, int32_t* dest, size_t w, bool invert);

/* Unpack big-endian uint16 pairs → int32 array, with optional 16-bit XOR invert.
 * Used by PNG decode (N=16 path). */
GRK_SIMD_API void hwy_unpack_16be_to_i32(const uint8_t* src, int32_t* dest, size_t w, bool invert);

/* Unpack machine-endian uint16 → int32 array, with optional 16-bit XOR invert.
 * Used by TIFF decode (N=16 path, libtiff already decoded to native byte order). */
GRK_SIMD_API void hwy_unpack_16le_to_i32(const uint16_t* src, int32_t* dest, size_t w, bool invert);

/* Deinterleave packed int32 buffer [R0,G0,B0,R1,G1,B1,...] into separate component
 * planes. Optimised for numComps == 3 and 4; falls back to scalar for others. */
GRK_SIMD_API void hwy_deinterleave_i32(const int32_t* src, int32_t* const* dest, uint32_t w,
                                       uint16_t numComps);

/* Pack N planar int32 components into interleaved uint8 output, one row at a time.
 * Each src[k] points to the start of the k-th component for this row.
 * adjust is added to each sample before narrowing to uint8. */
GRK_SIMD_API void hwy_pack_planar_to_8(const int32_t* const* src, uint32_t numPlanes, uint8_t* dest,
                                       uint32_t w, int32_t adjust);

/* Pack N planar int32 components into interleaved machine-endian uint16 output.
 * Same semantics as hwy_pack_planar_to_8 but for 16-bit output. */
GRK_SIMD_API void hwy_pack_planar_to_16(const int32_t* const* src, uint32_t numPlanes,
                                        uint16_t* dest, uint32_t w, int32_t adjust);

/* Scale int32 component data by power-of-two multiply, with stride. */
GRK_SIMD_API void hwy_scale_component_up(int32_t* data, uint32_t w, uint32_t h, uint32_t stride,
                                         int32_t scale);

/* Scale int32 component data by power-of-two divide, with stride. */
GRK_SIMD_API void hwy_scale_component_down(int32_t* data, uint32_t w, uint32_t h, uint32_t stride,
                                           int32_t scale);

} // namespace grk
