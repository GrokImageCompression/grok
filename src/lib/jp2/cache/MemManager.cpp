/*
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
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
#define GROK_SKIP_POISON
#include "grk_includes.h"

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

namespace grk
{
const size_t grk_buffer_alignment = 64;
uint32_t grk_make_aligned_width(uint32_t width)
{
	assert(width);
	return (uint32_t)((((uint64_t)width + grk_buffer_alignment - 1) / grk_buffer_alignment) *
					  grk_buffer_alignment);
}
static inline void* grk_aligned_alloc_N(size_t alignment, size_t size)
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
	ptr = _aligned_alloc(size,alignment);
#else
	ptr = std::aligned_alloc(alignment, size);
#endif
	return ptr;
}
void* grk_malloc(size_t size)
{
	if(size == 0U) /* prevent implementation defined behavior of realloc */
		return nullptr;

	return malloc(size);
}
void* grk_calloc(size_t num, size_t size)
{
	if(num == 0 || size == 0)
		/* prevent implementation defined behavior of realloc */
		return nullptr;

	return calloc(num, size);
}
void* grk_aligned_malloc(size_t size)
{
	return grk_aligned_alloc_N(grk_buffer_alignment, size);
}
void grk_aligned_free(void* ptr)
{
	free(ptr);
}
void* grk_realloc(void* ptr, size_t new_size)
{
	if(new_size == 0U) /* prevent implementation defined behavior of realloc */
		return nullptr;

	return realloc(ptr, new_size);
}
void grk_free(void* ptr)
{
	free(ptr);
}
} // namespace grk
