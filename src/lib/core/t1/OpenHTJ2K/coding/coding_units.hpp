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

#pragma once

#include <cstdint>
#include <vector>

#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wparentheses"
#endif

#include "open_htj2k_typedef.hpp"
#include <cassert>
#include <string>
#include <memory>
#include "utils.hpp"
#include <cstring>
#include <functional>

/********************************************************************************
 * j2k_region
 *******************************************************************************/
class j2k_region {
 public:
  // top-left coordinate (inclusive) of a region in the reference grid
  element_siz pos0;
  // bottom-right coordinate (exclusive) of a region in the reference grid
  element_siz pos1;
  // return top-left coordinate (inclusive)
  element_siz get_pos0() const { return pos0; }
  // return bottom-right coordinate (exclusive)
  element_siz get_pos1() const { return pos1; }
  // get size of a region
  void get_size(element_siz &out) const {
    out.x = pos1.x - pos0.x;
    out.y = pos1.y - pos0.y;
  }
  // set top-left coordinate (inclusive)
  void set_pos0(element_siz in) { pos0 = in; }
  // set bottom-right coordinate (exclusive)
  void set_pos1(element_siz in) { pos1 = in; }
  j2k_region() = default;
  j2k_region(element_siz p0, element_siz p1) : pos0(p0), pos1(p1) {}
};

/********************************************************************************
 * j2k_codeblock
 *******************************************************************************/
class j2k_codeblock : public j2k_region {
 public:
  const element_siz size;

 private:
  const uint32_t index;
  const uint8_t band;
  const uint8_t M_b;
  std::unique_ptr<uint8_t[]> compressed_data;
  uint8_t *current_address;

 public:
  std::unique_ptr<uint8_t[]> block_states;
  const uint8_t R_b;
  const uint8_t transformation;
  const float stepsize;
  const uint32_t band_stride;
  const uint16_t num_layers;
  std::unique_ptr<int32_t[]> sample_buf;
  sprec_t *const i_samples;
  float *const f_samples;
  uint32_t length;
  uint16_t Cmodes;
  uint8_t num_passes;
  uint8_t num_ZBP;
  uint8_t fast_skip_passes;
  uint32_t Lblock;
  // length of a coding pass in byte
  std::vector<uint32_t> pass_length;
  // index of the coding-pass from which layer starts
  std::unique_ptr<uint8_t[]> layer_start;
  // number of coding-passes included in a layer
  std::unique_ptr<uint8_t[]> layer_passes;
  bool already_included;

  j2k_codeblock(const uint32_t &idx, uint8_t orientation, uint8_t M_b, uint8_t R_b, uint8_t transformation,
                float stepsize, uint32_t band_stride, sprec_t *ibuf, float *fbuf, uint32_t offset,
                const uint16_t &numlayers, const uint8_t &codeblock_style, const element_siz &p0,
                const element_siz &p1, const element_siz &s);
  void modify_state(const std::function<void(uint8_t &, uint8_t)> &callback, uint8_t val, int16_t j1,
                    int16_t j2) {
    callback(block_states[(j1 + 1) * (size.x + 2) + (j2 + 1)], val);
  }
  uint8_t get_state(const std::function<uint8_t(uint8_t &)> &callback, int16_t j1, int16_t j2) const {
    return callback(block_states[(j1 + 1) * (size.x + 2) + (j2 + 1)]);
  }
  // DEBUG FUNCTION, SOON BE DELETED
  uint8_t get_orientation() const { return band; }
  uint8_t get_context_label_sig(const uint16_t &j1, const uint16_t &j2) const;
  uint8_t get_signLUT_index(const uint16_t &j1, const uint16_t &j2) const;
  uint8_t get_Mb() const;
  uint8_t *get_compressed_data();
  void set_compressed_data(uint8_t *buf, uint16_t size);
  float *get_fsample_addr(const int16_t &j1, const int16_t &j2) const;
  void update_sample(const uint8_t &symbol, const uint8_t &p, const uint16_t &j1, const uint16_t &j2) const;
  void update_sign(const int8_t &val, const uint16_t &j1, const uint16_t &j2) const;
  uint8_t get_sign(const uint16_t &j1, const uint16_t &j2) const;
  void set_MagSgn_and_sigma(uint32_t &or_val);
  void calc_mbr(uint8_t &mbr, uint16_t i, uint16_t j, uint32_t mbr_info, uint8_t causal_cond) const;
};

int32_t htj2k_encode(j2k_codeblock *block, uint8_t ROIshift) noexcept;


#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif
