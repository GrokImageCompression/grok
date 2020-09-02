/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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


// OSX is missing C++11 aligned_alloc (stdlib.h version)
#if defined(__APPLE__)
#undef GROK_HAVE_ALIGNED_ALLOC
#endif


#ifndef SIZE_MAX
# define SIZE_MAX ((size_t) -1)
#endif

namespace grk {

const uint32_t grk_alignment = 32;

uint32_t grk_make_aligned_width(uint32_t width){
	return (uint32_t)((((uint64_t)width + grk_alignment - 1)/grk_alignment) * grk_alignment);
}

static inline void* grk_aligned_alloc_n(size_t alignment, size_t size) {
	void *ptr;

	/* alignment shall be power of 2 */
	assert((alignment != 0U) && ((alignment & (alignment - 1U)) == 0U));
	/* alignment shall be at least sizeof(void*) */
	assert(alignment >= sizeof(void*));

	if (size == 0U)  /* prevent implementation defined behavior of realloc */
		return nullptr;

	// make new_size a multiple of alignment
	size = ((size + alignment - 1)/alignment) * alignment;

#if defined(GROK_HAVE_ALIGNED_ALLOC)
	ptr = aligned_alloc(alignment, size);
#elif defined(GROK_HAVE_POSIX_MEMALIGN)
    /* aligned_alloc requires c11, restrict to posix_memalign for now. Quote:
     * This function was introduced in POSIX 1003.1d. Although this function is
     * superseded by aligned_alloc, it is more portable to older POSIX systems
     * that do not support ISO C11.  */
    if (posix_memalign (&ptr, alignment, size))
        ptr = nullptr;
    /* older linux */
#elif defined(GROK_HAVE_MEMALIGN)
    ptr = memalign( alignment, size );
    /* _MSC_VER */
#elif defined(GROK_HAVE__ALIGNED_MALLOC)
    ptr = _aligned_malloc(size, alignment);
#else
    /*
     * Generic aligned malloc implementation.
     * Uses size_t offset for the integer manipulation of the pointer,
     * as uintptr_t is not available in C89 to do
     * bitwise operations on the pointer itself.
     */
    alignment--;
    {
        /* Room for padding and extra pointer stored in front of allocated area */
        size_t overhead = alignment + sizeof(void *);

        /* let's be extra careful */
        assert(alignment <= (SIZE_MAX - sizeof(void *)));

        /* Avoid integer overflow */
        if (size > (SIZE_MAX - overhead))
            return nullptr;

        uint8_t *mem = (uint8_t*)malloc(size + overhead);
        if (mem == nullptr)
            return mem;
        /* offset = ((alignment + 1U) - ((size_t)(mem + sizeof(void*)) & alignment)) & alignment; */
        /* Use the fact that alignment + 1U is a power of 2 */
        size_t offset = ((alignment ^ ((size_t)(mem + sizeof(void*)) & alignment)) + 1U) & alignment;
        ptr = (void *)(mem + sizeof(void*) + offset);
        ((void**) ptr)[-1] = mem;
    }
#endif
	return ptr;
}
void* grk_malloc(size_t size) {
	if (size == 0U) /* prevent implementation defined behavior of realloc */
		return nullptr;

	return malloc(size);
}
void* grk_calloc(size_t num, size_t size) {
	if (num == 0 || size == 0)
		/* prevent implementation defined behavior of realloc */
		return nullptr;

	return calloc(num, size);
}

void* grk_aligned_malloc(size_t size) {
	return grk_aligned_alloc_n(default_align, size);
}

void grk_aligned_free(void *ptr) {
#if defined(GROK_HAVE_POSIX_MEMALIGN) || defined(GROK_HAVE_ALIGNED_ALLOC) ||  defined(GROK_HAVE_MEMALIGN)
	free(ptr);
#elif defined(GROK_HAVE__ALIGNED_MALLOC)
    _aligned_free( ptr );
#else
    /* Generic implementation has malloced pointer stored in front of used area */
    if (ptr != nullptr)
        free(((void**) ptr)[-1]);
#endif
}

void* grk_realloc(void *ptr, size_t new_size) {
	if (new_size == 0U)/* prevent implementation defined behavior of realloc */
		return nullptr;

	return realloc(ptr, new_size);
}
void grk_free(void *ptr) {
	if (ptr)
		free(ptr);
}
}
