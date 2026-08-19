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

// a tiny codestream can declare a multi-gigapixel canvas; decoding it would
// trip libfuzzer's malloc/rss limits before any codec bug is reached, so
// skip decode when the declared image exceeds this sample budget
static const uint64_t FUZZ_MAX_DECODE_SAMPLES = 64ULL * 1024 * 1024;

static inline bool fuzz_image_too_large(const grk_image* image)
{
  if(image->x1 <= image->x0 || image->y1 <= image->y0)
    return false;
  uint64_t samples = (uint64_t)(image->x1 - image->x0) * (image->y1 - image->y0);
  return samples * image->numcomps > FUZZ_MAX_DECODE_SAMPLES;
}
