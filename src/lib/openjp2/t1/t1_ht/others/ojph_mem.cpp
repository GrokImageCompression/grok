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
// File: ojph_mem.cpp
// Author: Aous Naman
// Date: 28 August 2019
//***************************************************************************/


#include <new>
#include "ojph_mem.h"

namespace ojph {

  ////////////////////////////////////////////////////////////////////////////
  //
  //
  //
  //
  //
  ////////////////////////////////////////////////////////////////////////////

  ////////////////////////////////////////////////////////////////////////////
  template<>
  void line_buf::finalize_alloc<si32>(mem_fixed_allocator *p)
  {
    assert(p != 0 && size != 0);
    i32 = p->post_alloc_data<si32>(size, pre_size);
  }

  ////////////////////////////////////////////////////////////////////////////
  template<>
  void line_buf::finalize_alloc<float>(mem_fixed_allocator *p)
  {
    assert(p != 0 && size != 0);
    f32 = p->post_alloc_data<float>(size, pre_size);
  }

  ////////////////////////////////////////////////////////////////////////////
  template<>
  void line_buf::wrap(si32 *buffer, size_t num_ele, int pre_size)
  {
    i32 = buffer;
    this->size = num_ele;
    this->pre_size = pre_size;
  }

  ////////////////////////////////////////////////////////////////////////////
  template<>
  void line_buf::wrap(float *buffer, size_t num_ele, int pre_size)
  {
    f32 = buffer;
    this->size = num_ele;
    this->pre_size = pre_size;
  }

  ////////////////////////////////////////////////////////////////////////////
  //
  //
  //
  //
  //
  ////////////////////////////////////////////////////////////////////////////

  ////////////////////////////////////////////////////////////////////////////
  void mem_elastic_allocator::get_buffer(int needed_bytes, coded_lists* &p)
  {
    int extended_bytes = needed_bytes + sizeof(coded_lists);

    if (store == NULL)
    {
      int bytes = ojph_max(extended_bytes, chunk_size);
      store = (stores_list*)malloc(bytes);
      cur_store = store = new (store) stores_list(bytes);
      total_allocated += bytes;
    }

    if (cur_store->available < extended_bytes)
    {
      int bytes = ojph_max(extended_bytes, chunk_size);
      cur_store->next_store = (stores_list*)malloc(bytes);
      cur_store = new (cur_store->next_store) stores_list(bytes);
      total_allocated += bytes;
    }

    p = new (cur_store->data) coded_lists(needed_bytes);

    cur_store->available -= extended_bytes;
    cur_store->data += extended_bytes;
  }

}
