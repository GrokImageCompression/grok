/*
 *
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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */
#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cassert>

#include "grk_config_private.h"

namespace grk_bin
{
uint32_t grkMakeAlignedWidth(uint32_t width);
/**
 Allocate memory aligned to a 16 byte boundary
 @param size Bytes to allocate
 @return a void pointer to the allocated space, or nullptr if there is insufficient memory available
 */
void* grk_aligned_malloc(size_t size);
void grk_aligned_free(void* ptr);

} // namespace grk_bin
