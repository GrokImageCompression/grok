/*
 *    Copyright (C) 2016-2023 Grok Image Compression Inc.
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

#if defined(__GNUC__) && !defined(GROK_SKIP_POISON)
#pragma GCC poison malloc calloc realloc free
#endif

namespace grk
{
uint32_t grk_make_aligned_width(uint32_t width);
/**
 Allocate an uninitialized memory block
 @param size Bytes to allocate
 @return a void pointer to the allocated space, or nullptr if there is insufficient memory available
 */
void* grk_malloc(size_t size);
/**
 Allocate a memory block with elements initialized to 0
 @param numOfElements  Blocks to allocate
 @param sizeOfElements Bytes per block to allocate
 @return a void pointer to the allocated space, or nullptr if there is insufficient memory available
 */
void* grk_calloc(size_t numOfElements, size_t sizeOfElements);
/**
 Allocate memory aligned to a 16 byte boundary
 @param size Bytes to allocate
 @return a void pointer to the allocated space, or nullptr if there is insufficient memory available
 */
void* grk_aligned_malloc(size_t size);
void grk_aligned_free(void* ptr);
/**
 Reallocate memory blocks.
 @param m Pointer to previously allocated memory block
 @param s New size in bytes
 @return a void pointer to the reallocated (and possibly moved) memory block
 */
void* grk_realloc(void* m, size_t s);
/**
 Deallocates or frees a memory block.
 @param m Previously allocated memory block to be freed
 */
void grk_free(void* m);

} // namespace grk
