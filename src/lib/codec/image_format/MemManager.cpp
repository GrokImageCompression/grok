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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "MemManager.h"
#include <cstdio>

#ifdef _WIN32
#include <malloc.h>
#endif

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

namespace grk_bin
{
const uint32_t grkWidthAlignment = 32;
const size_t grkBufferALignment = 64;

uint32_t grkMakeAlignedWidth(uint32_t width)
{
   assert(width);
   return (uint32_t)((((uint64_t)width + grkWidthAlignment - 1) / grkWidthAlignment) *
					 grkWidthAlignment);
}
static inline void* grkAlignedAllocN(size_t alignment, size_t size)
{
   void* ptr;

   /* alignment shall be power of 2 */
   assert((alignment != 0U) && ((alignment & (alignment - 1U)) == 0U));
   /* alignment shall be at least sizeof(void*) */
   assert(alignment >= sizeof(void*));

   if(size == 0U) /* prevent implementation defined behavior of realloc */
	  return nullptr;

   // make new_size a multiple of alignment
   size = ((size + alignment - 1) / alignment) * alignment;

#ifdef _WIN32
   ptr = _aligned_malloc(size, alignment);
#else
   ptr = std::aligned_alloc(alignment, size);
#endif

   return ptr;
}
void* grk_aligned_malloc(size_t size)
{
   return grkAlignedAllocN(grkBufferALignment, size);
}
void grk_aligned_free(void* ptr)
{
#ifdef _WIN32
   _aligned_free(ptr);
#else
   free(ptr);
#endif
}
} // namespace grk_bin
