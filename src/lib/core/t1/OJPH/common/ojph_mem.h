//***************************************************************************/
// This software is released under the 2-Clause BSD license, included
// below.
//
// Copyright (c) 2019, Aous Naman 
// Copyright (c) 2019, Kakadu Software Pty Ltd, Australia
// Copyright (c) 2019, The University of New South Wales, Australia
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// 
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
// TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
// PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
// TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//***************************************************************************/
// This file is part of the OpenJPH software implementation.
// File: ojph_mem.h
// Author: Aous Naman
// Date: 28 August 2019
//***************************************************************************/


#ifndef OJPH_MEM_H
#define OJPH_MEM_H

#include <cstdlib>
#include <cassert>
#include <cstring>
#include <type_traits>

#include "ojph_arch.h"

namespace ojph {

  /////////////////////////////////////////////////////////////////////////////
  class mem_fixed_allocator
  {
  public:
    mem_fixed_allocator()
    {
      avail_obj = avail_data = store = NULL;
      avail_size_obj = avail_size_data = size_obj = size_data = 0;
    }
    ~mem_fixed_allocator()
    {
      if (store) free(store);
    }

    template<typename T>
    void pre_alloc_data(size_t num_ele, ui32 pre_size)
    {
      pre_alloc_local<T, byte_alignment>(num_ele, pre_size, size_data);
    }

    template<typename T>
    void pre_alloc_obj(size_t num_ele)
    {
      pre_alloc_local<T, object_alignment>(num_ele, 0, size_obj);
    }

    void alloc()
    {
      assert(store == NULL);
      avail_obj = store = malloc(size_data + size_obj);
      avail_data = (ui8*)store + size_obj;
      if (store == NULL)
        throw "malloc failed";
      avail_size_obj = size_obj;
      avail_size_data = size_data;
    }

    template<typename T>
    T* post_alloc_data(size_t num_ele, ui32 pre_size)
    {
      return post_alloc_local<T, byte_alignment>
        (num_ele, pre_size, avail_size_data, avail_data);
    }

    template<typename T>
    T* post_alloc_obj(size_t num_ele)
    {
      return post_alloc_local<T, object_alignment>
        (num_ele, 0, avail_size_obj, avail_obj);
    }

  private:
    template<typename T, int N>
    void pre_alloc_local(size_t num_ele, ui32 pre_size, size_t& sz)
    {
      assert(store == NULL);
      num_ele = calc_aligned_size<T, N>(num_ele);
      size_t total = (num_ele + pre_size) * sizeof(T);
      total += 2*N - 1;

      sz += total;
    }

    template<typename T, int N>
    T* post_alloc_local(size_t num_ele, ui32 pre_size,
                        size_t& avail_sz, void*& avail_p)
    {
      assert(store != NULL);
      num_ele = calc_aligned_size<T, N>(num_ele);
      size_t total = (num_ele + pre_size) * sizeof(T);
      total += 2*N - 1;

      T* p = align_ptr<T, N>((T*)avail_p + pre_size);
      avail_p = (ui8*)avail_p + total;
      avail_sz -= total;
      assert((avail_sz & 0x8000000000000000llu) == 0);
      return p;
    }

    void *store, *avail_data, *avail_obj;
    size_t size_data, size_obj, avail_size_obj, avail_size_data;
  };

  /////////////////////////////////////////////////////////////////////////////
  struct line_buf
  {
    template<typename T>
    void pre_alloc(mem_fixed_allocator *p, size_t num_ele, ui32 pre_size)
    {
      memset(this, 0, sizeof(line_buf));
      p->pre_alloc_data<T>(num_ele, pre_size);
      size = num_ele;
      this->pre_size = pre_size;
    }
    
    template<typename T>
    void finalize_alloc(mem_fixed_allocator *p);

    template<typename T>
    void wrap(T *buffer, size_t num_ele, ui32 pre_size);

    size_t size;
    ui32 pre_size;
    union {
      si32* i32;
      float* f32;
    };
  };

  /////////////////////////////////////////////////////////////////////////////
  struct coded_lists
  {
    coded_lists(ui32 size)
    {
      next_list = NULL;
      avail_size = buf_size = size;
      this->buf = (ui8*)this + sizeof(coded_lists);
    }

    coded_lists* next_list;
    ui32 buf_size;
    ui32 avail_size;
    ui8* buf;
  };

  /////////////////////////////////////////////////////////////////////////////
  class mem_elastic_allocator
  {
    /*
      advantage: allocate large chunks of memory
    */

  public:
    mem_elastic_allocator(ui32 chunk_size)
    : chunk_size(chunk_size)
    { cur_store = store = NULL; total_allocated = 0; }

    ~mem_elastic_allocator()
    {
      while (store) {
        stores_list* t = store->next_store;
        free(store);
        store = t;
      }
    }

    void get_buffer(ui32 needed_bytes, coded_lists*& p);

  private:
    struct stores_list
    {
      stores_list(ui32 available_bytes)
      {
        this->next_store = NULL;
        this->available = available_bytes;
        this->data = (ui8*)this + sizeof(stores_list);
      }
      static ui32 eval_store_bytes(ui32 available_bytes) 
      { // calculates how many bytes need to be allocated
        return available_bytes + (ui32)sizeof(stores_list);
      }
      stores_list *next_store;
      ui32 available;
      ui8* data;
    };

    stores_list *store, *cur_store;
    size_t total_allocated;
    const ui32 chunk_size;
  };


}


#endif // !OJPH_MEM_H
