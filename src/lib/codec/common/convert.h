/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#pragma once

#include "packer.h"

/* planar / interleaved conversions */
typedef void (*cvtInterleavedToPlanar)(const int32_t* src, int32_t* const* dest, size_t w);
extern const cvtInterleavedToPlanar cvtInterleavedToPlanar_LUT[10];

/* bit depth conversions */
typedef void (*cvtTo32)(const uint8_t* src, int32_t* dest, size_t w, bool invert);
extern const cvtTo32 cvtTo32_LUT[9]; /* up to 8bpp */
extern const cvtTo32 cvtsTo32_LUT[9]; /* up to 8bpp */

void _3uto32s(const uint8_t* src, int32_t* dest, size_t w, bool invert);
void _5uto32s(const uint8_t* src, int32_t* dest, size_t w, bool invert);
void _7uto32s(const uint8_t* src, int32_t* dest, size_t w, bool invert);
void _9uto32s(const uint8_t* src, int32_t* dest, size_t w, bool invert);
void _10sto32s(const uint8_t* src, int32_t* dest, size_t w, bool invert);
void _10uto32s(const uint8_t* src, int32_t* dest, size_t w, bool invert);
void _11uto32s(const uint8_t* src, int32_t* dest, size_t w, bool invert);
void _12sto32s(const uint8_t* src, int32_t* dest, size_t w, bool invert);
void _12uto32s(const uint8_t* src, int32_t* dest, size_t w, bool invert);
void _13uto32s(const uint8_t* src, int32_t* dest, size_t w, bool invert);
void _14uto32s(const uint8_t* src, int32_t* dest, size_t w, bool invert);
void _15uto32s(const uint8_t* src, int32_t* dest, size_t w, bool invert);
void _16uto32s(const uint16_t* src, int32_t* dest, size_t w, bool invert);
void _16u32s(const uint8_t* src, int32_t* dest, size_t w, bool invert);

int32_t sign_extend(int32_t val, uint8_t shift);
