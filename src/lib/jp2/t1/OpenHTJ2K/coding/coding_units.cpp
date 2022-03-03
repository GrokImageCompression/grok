// Copyright (c) 2019 - 2021, Osamu Watanabe
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
//    modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
//    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
//    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
//    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
//    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include "coding_units.hpp"


/********************************************************************************
 * j2k_codeblock
 *******************************************************************************/

j2k_codeblock::j2k_codeblock(const uint32_t &idx, uint8_t orientation, uint8_t M_b, uint8_t R_b,
                             uint8_t transformation, float stepsize, uint32_t band_stride, sprec_t *ibuf,
                             float *fbuf, uint32_t offset, const uint16_t &numlayers,
                             const uint8_t &codeblock_style, const element_siz &p0, const element_siz &p1,
                             const element_siz &s)
    : j2k_region(p0, p1),
      // public
      size(s),
      // private
      index(idx),
      band(orientation),
      M_b(M_b),
      compressed_data(nullptr),
      current_address(nullptr),
      block_states(std::make_unique<uint8_t[]>((size.x + 2) * (size.y + 2))),
      // public
      R_b(R_b),
      transformation(transformation),
      stepsize(stepsize),
      band_stride(band_stride),
      num_layers(numlayers),
      sample_buf(std::make_unique<int32_t[]>(size.x * size.y)),
      i_samples(ibuf + offset),
      f_samples(fbuf + offset),
      length(0),
      Cmodes(codeblock_style),
      num_passes(0),
      num_ZBP(0),
      fast_skip_passes(0),
      Lblock(0),
      already_included(false) {
  memset(sample_buf.get(), 0, sizeof(int32_t) * size.x * size.y);
  memset(block_states.get(), 0, (size.x + 2) * (size.y + 2));
  this->layer_start  = std::make_unique<uint8_t[]>(num_layers);
  this->layer_passes = std::make_unique<uint8_t[]>(num_layers);
  this->pass_length.reserve(109);
  this->pass_length = std::vector<uint32_t>(num_layers, 0);  // critical section
}

uint8_t j2k_codeblock::get_Mb() const { return this->M_b; }

uint8_t *j2k_codeblock::get_compressed_data() { return this->compressed_data.get(); }

void j2k_codeblock::set_compressed_data(uint8_t *buf, uint16_t bufsize) {
  if (this->compressed_data != nullptr) {
    printf(
        "ERROR: illegal attempt to allocate codeblock's compressed data but the data is not "
        "null.\n");
    //exit(EXIT_FAILURE);
  }
  this->compressed_data = std::make_unique<uint8_t[]>(bufsize);
  memcpy(this->compressed_data.get(), buf, bufsize);
  this->current_address = this->compressed_data.get();
}
