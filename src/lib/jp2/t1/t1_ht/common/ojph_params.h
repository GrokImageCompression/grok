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
// File: ojph_params.h
// Author: Aous Naman
// Date: 28 August 2019
//***************************************************************************/


#ifndef OJPH_PARAMS_H
#define OJPH_PARAMS_H

#include "ojph_base.h"

namespace ojph {

  ////////////////////////////////////////////////////////////////////////////
  //prototyping from local
  namespace local {
    struct param_siz;
    struct param_cod;
    struct param_qcd;
    struct param_cap;
  }

  ////////////////////////////////////////////////////////////////////////////
  class param_siz
  {
  public:
    OJPH_EXPORT
    param_siz(local::param_siz *p) : state(p) {}

    //setters
    OJPH_EXPORT
    void set_image_extent(point extent);
    OJPH_EXPORT
    void set_tile_size(size s);
    OJPH_EXPORT
    void set_image_offset(point offset);
    OJPH_EXPORT
    void set_tile_offset(point offset);
    OJPH_EXPORT
    void set_num_components(int num_comps);
    OJPH_EXPORT
    void set_component(int comp_num, const point& downsampling,
                       int bit_depth, bool is_signed);

    //getters
    OJPH_EXPORT
    point get_image_extent() const;
    OJPH_EXPORT
    point get_image_offset() const;
    OJPH_EXPORT
    size get_tile_size() const;
    OJPH_EXPORT
    point get_tile_offset() const;
    OJPH_EXPORT
    si32 get_num_components() const;
    OJPH_EXPORT
    si32 get_bit_depth(si32 comp_num) const;
    OJPH_EXPORT
    bool is_signed(si32 comp_num) const;
    OJPH_EXPORT
    point get_downsampling(si32 comp_num) const;

  private:
    local::param_siz* state;
  };

  ////////////////////////////////////////////////////////////////////////////
  class param_cod
  {
  public:
    OJPH_EXPORT
    param_cod(local::param_cod* p) : state(p) {}

    OJPH_EXPORT
    void set_num_decomposition(ui8 num_decompositions);
    OJPH_EXPORT
    void set_block_dims(int width, int height);
    OJPH_EXPORT
    void set_precinct_size(int num_levels, size* precinct_size);
    OJPH_EXPORT
    void set_progression_order(const char *name);
    OJPH_EXPORT
    void set_color_transform(bool color_transform);
    OJPH_EXPORT
    void set_reversible(bool reversible);

    OJPH_EXPORT
    int get_num_decompositions() const;
    OJPH_EXPORT
    size get_block_dims() const;
    OJPH_EXPORT
    size get_log_block_dims() const;
    OJPH_EXPORT
    bool is_reversible() const;
    OJPH_EXPORT
    size get_precinct_size(int level_num) const;
    OJPH_EXPORT
    size get_log_precinct_size(int level_num) const;
    OJPH_EXPORT
    int get_progression_order() const;
    OJPH_EXPORT
    const char* get_progression_order_as_string() const;
    OJPH_EXPORT
    int get_num_layers() const;
    OJPH_EXPORT
    bool is_using_color_transform() const;
    OJPH_EXPORT
    bool packets_may_use_sop() const;
    OJPH_EXPORT
    bool packets_use_eph() const;

  private:
    local::param_cod* state;
  };

  ////////////////////////////////////////////////////////////////////////////
  class param_qcd
  {
  public:
    OJPH_EXPORT
    param_qcd(local::param_qcd* p) : state(p) {}

    OJPH_EXPORT
    void set_irrev_quant(float delta);

  private:
    local::param_qcd* state;
  };

}

#endif // !OJPH_PARAMS_H
