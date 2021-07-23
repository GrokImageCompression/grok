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
#include "block_decoding.hpp"
#include "dwt.hpp"
#include "color.hpp"
#ifdef _OPENMP
  #include <omp.h>
#endif

#if defined(_MSC_VER) || defined(__MINGW64__)
  #include <intrin.h>
#elif defined(__x86_64__)
  #include <x86intrin.h>
#endif

float bibo_step_gains[32][5] = {{1.00000000, 4.17226868, 1.44209458, 2.10966980, 1.69807026},
                                {1.38034954, 4.58473765, 1.83866981, 2.13405021, 1.63956779},
                                {1.33279329, 4.58985327, 1.75793599, 2.07403081, 1.60751898},
                                {1.30674103, 4.48819441, 1.74087517, 2.00811395, 1.60270904},
                                {1.30283106, 4.44564235, 1.72542071, 2.00171155, 1.59940161},
                                {1.30014247, 4.43925026, 1.72264700, 1.99727052, 1.59832420},
                                {1.29926666, 4.43776733, 1.72157554, 1.99642626, 1.59828968},
                                {1.29923860, 4.43704105, 1.72132351, 1.99619334, 1.59826880},
                                {1.29922163, 4.43682858, 1.72125886, 1.99616484, 1.59826245},
                                {1.29921646, 4.43680359, 1.72124892, 1.99615185, 1.59826037},
                                {1.29921477, 4.43679132, 1.72124493, 1.99614775, 1.59825980},
                                {1.29921431, 4.43678921, 1.72124414, 1.99614684, 1.59825953},
                                {1.29921409, 4.43678858, 1.72124384, 1.99614656, 1.59825948},
                                {1.29921405, 4.43678831, 1.72124381, 1.99614653, 1.59825947},
                                {1.29921404, 4.43678829, 1.72124381, 1.99614652, 1.59825947},
                                {1.29921404, 4.43678829, 1.72124381, 1.99614652, 1.59825947},
                                {1.29921404, 4.43678829, 1.72124381, 1.99614652, 1.59825947},
                                {1.29921404, 4.43678829, 1.72124381, 1.99614652, 1.59825947},
                                {1.29921404, 4.43678829, 1.72124381, 1.99614652, 1.59825947},
                                {1.29921404, 4.43678829, 1.72124381, 1.99614652, 1.59825947},
                                {1.29921404, 4.43678829, 1.72124381, 1.99614652, 1.59825947},
                                {1.29921404, 4.43678829, 1.72124381, 1.99614652, 1.59825947},
                                {1.29921404, 4.43678829, 1.72124381, 1.99614652, 1.59825947},
                                {1.29921404, 4.43678829, 1.72124381, 1.99614652, 1.59825947},
                                {1.29921404, 4.43678829, 1.72124381, 1.99614652, 1.59825947},
                                {1.29921404, 4.43678829, 1.72124381, 1.99614652, 1.59825947},
                                {1.29921404, 4.43678829, 1.72124381, 1.99614652, 1.59825947},
                                {1.29921404, 4.43678829, 1.72124381, 1.99614652, 1.59825947},
                                {1.29921404, 4.43678829, 1.72124381, 1.99614652, 1.59825947},
                                {1.29921404, 4.43678829, 1.72124381, 1.99614652, 1.59825947},
                                {1.29921404, 4.43678829, 1.72124381, 1.99614652, 1.59825947},
                                {1.29921404, 4.43678829, 1.72124381, 1.99614652, 1.59825947}};

static void find_child_ranges(float *child_ranges, uint8_t &normalizing_upshift, float &normalization,
                              uint8_t lev, uint32_t u0, uint32_t u1, uint32_t v0, uint32_t v1) {
  if (u0 == u1 || v0 == v1) {
    return;
  }
  // constants
  constexpr float K         = 1.230174104914001;
  constexpr float low_gain  = 1.0 / K;
  constexpr float high_gain = K / 2;

  // initialization
  const bool unit_width  = (u0 == u1 - 1);
  const bool unit_height = (v0 == v1 - 1);
  float bibo_max         = normalization;
  normalizing_upshift    = 0;
  for (uint8_t b = 0; b < 4; ++b) {
    child_ranges[b] = normalization;
  }

  // vertical analysis gain, if any
  if (!unit_height) {
    child_ranges[BAND_LL] /= low_gain;
    child_ranges[BAND_HL] /= low_gain;
    child_ranges[BAND_LH] /= high_gain;
    child_ranges[BAND_HH] /= high_gain;
    float bibo_prev, bibo_in, bibo_out;
    bibo_prev = bibo_step_gains[lev][0] * normalization;
    bibo_in   = bibo_prev * bibo_step_gains[lev][0];
    for (uint8_t n = 0; n < 4; ++n) {
      bibo_out = bibo_prev * bibo_step_gains[lev][n + 1];
      bibo_max = std::max(bibo_max, bibo_out);
      bibo_max = std::max(bibo_max, bibo_in);
      bibo_in  = bibo_out;
    }
  }
  // horizontal analysis gain, if any
  if (!unit_width) {
    child_ranges[BAND_LL] /= low_gain;
    child_ranges[BAND_HL] /= high_gain;
    child_ranges[BAND_LH] /= low_gain;
    child_ranges[BAND_HH] /= high_gain;
    float bibo_prev, bibo_in, bibo_out;
    bibo_prev = std::max(bibo_step_gains[lev][4], bibo_step_gains[lev][3]);
    bibo_prev *= normalization;
    bibo_in = bibo_prev * bibo_step_gains[lev][0];
    for (uint8_t n = 0; n < 4; ++n) {
      bibo_out = bibo_prev * bibo_step_gains[lev][n + 1];
      bibo_max = std::max(bibo_max, bibo_out);
      bibo_max = std::max(bibo_max, bibo_in);
      bibo_in  = bibo_out;
    }
  }

  float overflow_limit = 1.0 * (1 << (16 - 13));
  while (bibo_max > 0.95 * overflow_limit) {
    normalizing_upshift++;
    for (uint8_t b = 0; b < 4; ++b) {
      child_ranges[b] *= 0.5;
    }
    bibo_max *= 0.5;
  }
  normalization = child_ranges[BAND_LL];
}

float band_nominal_ranges[32][4] = {
    {0.756664, 1.000000, 1.000000, 1.321590}, {1.145081, 1.513328, 1.513328, 2.000000},
    {0.866442, 1.145081, 1.145081, 1.513328}, {0.655606, 0.866442, 0.866442, 1.145081},
    {0.992147, 1.311211, 1.311211, 1.732884}, {0.750722, 0.992147, 0.992147, 1.311211},
    {1.136088, 1.501443, 1.501443, 1.984293}, {0.859637, 1.136088, 1.136088, 1.501443},
    {0.650457, 0.859637, 0.859637, 1.136088}, {0.984355, 1.300914, 1.300914, 1.719275},
    {0.744826, 0.984355, 0.984355, 1.300914}, {1.127166, 1.489652, 1.489652, 1.968710},
    {0.852886, 1.127166, 1.127166, 1.489652}, {1.290697, 1.705773, 1.705773, 2.254332},
    {0.976624, 1.290697, 1.290697, 1.705773}, {0.738977, 0.976624, 0.976624, 1.290697},
    {1.118314, 1.477953, 1.477953, 1.953248}, {0.846188, 1.118314, 1.118314, 1.477953},
    {1.280561, 1.692376, 1.692376, 2.236628}, {0.968954, 1.280561, 1.280561, 1.692376},
    {0.733173, 0.968954, 0.968954, 1.280561}, {1.109531, 1.466346, 1.466346, 1.937909},
    {0.839543, 1.109531, 1.109531, 1.466346}, {1.270504, 1.679085, 1.679085, 2.219063},
    {0.961345, 1.270504, 1.270504, 1.679085}, {0.727415, 0.961345, 0.961345, 1.270504},
    {1.100818, 1.454830, 1.454830, 1.922689}, {0.832949, 1.100818, 1.100818, 1.454830},
    {1.260526, 1.665899, 1.665899, 2.201636}, {0.953795, 1.260526, 1.260526, 1.665899},
    {0.721702, 0.953795, 0.953795, 1.260526}, {1.092173, 1.443405, 1.443405, 1.907590}};

uint8_t band_normalizing_upshift[32] = {1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 1,
                                        0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0};

uint8_t normalizing_upshift[32] = {0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1,
                                   1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1};

/********************************************************************************
 * j2k_codeblock
 *******************************************************************************/

j2k_codeblock::j2k_codeblock(const uint32_t &idx, uint8_t orientation, uint8_t M_b, uint8_t R_b,
                             uint8_t transformation, float stepsize, uint32_t band_stride, int16_t *ibuf,
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
    exit(EXIT_FAILURE);
  }
  this->compressed_data = std::make_unique<uint8_t[]>(bufsize);
  memcpy(this->compressed_data.get(), buf, bufsize);
  this->current_address = this->compressed_data.get();
}

void j2k_codeblock::create_compressed_buffer(buf_chain *tile_buf, uint16_t buf_limit,
                                             const uint16_t &layer) {
  uint32_t layer_length = 0;
  uint16_t l0, l1;
  if (this->layer_passes[layer] > 0) {
    l0 = this->layer_start[layer];
    l1 = l0 + this->layer_passes[layer];
    for (int i = l0; i < l1; i++) {
      layer_length += this->pass_length[i];
    }
    // allocate buffer one once for the first contributing layer
    if (this->compressed_data == nullptr) {
      this->compressed_data = std::make_unique<uint8_t[]>(buf_limit);
      this->current_address = this->compressed_data.get();
    }
    if (layer_length != 0) {
      while (this->length + layer_length > buf_limit) {
        // extend buffer size, if necessary
        uint8_t *old_buf = this->compressed_data.release();
        buf_limit += 8192;
        this->compressed_data = std::make_unique<uint8_t[]>(buf_limit);
        memcpy(this->compressed_data.get(), old_buf, sizeof(uint8_t) * (buf_limit));
        this->current_address = this->compressed_data.get() + (this->length);
        delete[] old_buf;
      }
      // we assume that the size of the compressed data is less than or equal to that of buf_chain node.
      tile_buf->copy_N_bytes(this->current_address, layer_length);
      this->length += layer_length;
    }
  }
}

float *j2k_codeblock::get_fsample_addr(const int16_t &j1, const int16_t &j2) const {
  return (this->f_samples + (j2 + 1) + (j1 + 1) * this->band_stride);
}

/********************************************************************************
 * j2k_precinct_subband
 *******************************************************************************/
j2k_precinct_subband::j2k_precinct_subband(uint8_t orientation, uint8_t M_b, uint8_t R_b,
                                           uint8_t transformation, float stepsize, int16_t *ibuf,
                                           float *fbuf, const element_siz &bp0, const element_siz &bp1,
                                           const element_siz &p0, const element_siz &p1,
                                           const uint16_t &num_layers, const element_siz &codeblock_size,
                                           const uint8_t &Cmodes)
    : j2k_region(p0, p1), orientation(orientation), inclusion_info(nullptr), ZBP_info(nullptr) {
  if (this->pos1.x > this->pos0.x) {
    this->num_codeblock_x =
        ceil_int(this->pos1.x - 0, codeblock_size.x) - (this->pos0.x - 0) / codeblock_size.x;
  } else {
    this->num_codeblock_x = 0;
  }
  if (this->pos1.y > this->pos0.y) {
    this->num_codeblock_y =
        ceil_int(this->pos1.y - 0, codeblock_size.y) - (this->pos0.y - 0) / codeblock_size.y;
  } else {
    this->num_codeblock_y = 0;
  }

  const uint32_t num_codeblocks = this->num_codeblock_x * this->num_codeblock_y;
  const uint32_t band_stride    = bp1.x - bp0.x;
  if (num_codeblocks != 0) {
    inclusion_info =
        std::make_unique<tagtree>(this->num_codeblock_x, this->num_codeblock_y);         // critical section
    ZBP_info = std::make_unique<tagtree>(this->num_codeblock_x, this->num_codeblock_y);  // critical section
    this->codeblocks =
        std::make_unique<std::unique_ptr<j2k_codeblock>[]>(num_codeblocks);  // critical section
    for (uint32_t cb = 0; cb < num_codeblocks; cb++) {
      const uint32_t x = cb % this->num_codeblock_x;
      const uint32_t y = cb / this->num_codeblock_x;

      const element_siz cblkpos0(std::max(pos0.x, codeblock_size.x * (x + pos0.x / codeblock_size.x)),
                                 std::max(pos0.y, codeblock_size.y * (y + pos0.y / codeblock_size.y)));
      const element_siz cblkpos1(std::min(pos1.x, codeblock_size.x * (x + 1 + pos0.x / codeblock_size.x)),
                                 std::min(pos1.y, codeblock_size.y * (y + 1 + pos0.y / codeblock_size.y)));
      const element_siz cblksize(cblkpos1.x - cblkpos0.x, cblkpos1.y - cblkpos0.y);
      const uint32_t offset = cblkpos0.x - bp0.x + (cblkpos0.y - bp0.y) * band_stride;
      this->codeblocks[cb]  = std::make_unique<j2k_codeblock>(
          cb, orientation, M_b, R_b, transformation, stepsize, band_stride, ibuf, fbuf, offset, num_layers,
          Cmodes, cblkpos0, cblkpos1, cblksize);
    }
  } else {
    // this->codeblocks = {};
  }
}

tagtree_node *j2k_precinct_subband::get_inclusion_node(uint32_t i) {
  return &this->inclusion_info->node[i];
}
tagtree_node *j2k_precinct_subband::get_ZBP_node(uint32_t i) { return &this->ZBP_info->node[i]; }

j2k_codeblock *j2k_precinct_subband::access_codeblock(uint32_t i) { return this->codeblocks[i].get(); }

void j2k_precinct_subband::parse_packet_header(buf_chain *packet_header, uint16_t layer_idx,
                                               uint16_t Ccap15) {
  // if no codeblock exists, nothing to do is left
  if (this->num_codeblock_x * this->num_codeblock_y == 0) {
    return;
  }
  uint8_t bit;
  bool is_included = false;
  uint16_t threshold;

  for (uint32_t idx = 0; idx < this->num_codeblock_x * this->num_codeblock_y; ++idx) {
    j2k_codeblock *block   = this->access_codeblock(idx);
    uint32_t cumsum_layers = 0;
    for (uint32_t i = 0; i < block->num_layers; ++i) {
      cumsum_layers += block->layer_passes[i];
    }
    uint32_t number_of_bytes      = 0;  // initialize to zero in case of `not included`.
    block->layer_start[layer_idx] = cumsum_layers;

    tagtree_node *current_node, *parent_node;

    if (!block->already_included) {
      // Flags for placeholder passes and mixed mode
      if (block->Cmodes >= HT) {
        // adding HT_PHLD flag because an HT codeblock may include placeholder passes
        block->Cmodes |= HT_PHLD;
        // If both Bits14 and 15 of Ccap15 is true, Mixed mode.
        if (Ccap15 & 0xC000) {
          block->Cmodes |= HT_MIXED;
        }
      }
      // Retrieve codeblock inclusion
      assert(block->fast_skip_passes == 0);

      // build tagtree search path
      std::vector<uint32_t> tree_path;
      current_node           = this->get_inclusion_node(idx);
      uint8_t max_tree_level = current_node->get_level();
      max_tree_level += (max_tree_level == 0xFF) ? 0 : 1;
      tree_path.reserve(max_tree_level);

      tree_path.push_back(current_node->get_index());
      while (current_node->get_parent_index() >= 0) {
        current_node = this->get_inclusion_node(current_node->get_parent_index());
        tree_path.push_back(current_node->get_index());
      }
      is_included = false;

      if (layer_idx > 0) {
        // Special case; A codeblock is not included in the first layer (layer 0).
        // Inclusion information of layer 0 (i.e. before the first contribution) shall be decoded.
        threshold = 0;

        for (int32_t i = tree_path.size() - 1; i >= 0; --i) {
          current_node = this->get_inclusion_node(tree_path[i]);
          if (current_node->get_state() == 0) {
            if (current_node->get_parent_index() < 0) {
              parent_node = nullptr;
            } else {
              parent_node = this->get_inclusion_node(current_node->get_parent_index());
            }
            if (current_node->get_level() > 0 && parent_node != nullptr) {
              if (current_node->get_current_value() < parent_node->get_current_value()) {
                current_node->set_current_value(parent_node->get_current_value());
              }
            }
            if (current_node->get_current_value() <= threshold) {
              bit = packet_header->get_bit();
              if (bit == 1) {
                current_node->set_value(current_node->get_current_value());
                current_node->set_state(1);
                is_included = true;
              } else {
                current_node->set_current_value(current_node->get_current_value() + 1);
                is_included = false;
              }
            }
          }
        }
      }

      // Normal case of inclusion information
      threshold = layer_idx;

      for (int32_t i = tree_path.size() - 1; i >= 0; --i) {
        current_node = this->get_inclusion_node(tree_path[i]);
        if (current_node->get_state() == 0) {
          if (current_node->get_parent_index() < 0) {
            parent_node = nullptr;
          } else {
            parent_node = this->get_inclusion_node(current_node->get_parent_index());
          }
          if (current_node->get_level() > 0 && parent_node != nullptr) {
            if (current_node->get_current_value() < parent_node->get_current_value()) {
              current_node->set_current_value(parent_node->get_current_value());
            }
          }
          if (current_node->get_current_value() <= threshold) {
            bit = packet_header->get_bit();
            if (bit == 1) {
              current_node->set_value(current_node->get_current_value());
              current_node->set_state(1);
              is_included = true;
            } else {
              current_node->set_current_value(current_node->get_current_value() + 1);
              is_included = false;
            }
          }
        }
      }

      // Retrieve number of zero bit planes
      if (is_included) {
        block->already_included = true;
        for (int32_t i = tree_path.size() - 1; i >= 0; --i) {
          current_node = this->get_ZBP_node(tree_path[i]);
          if (current_node->get_state() == 0) {
            if (current_node->get_parent_index() < 0) {
              parent_node = nullptr;
            } else {
              parent_node = this->get_ZBP_node(current_node->get_parent_index());
            }
            if (current_node->get_level() > 0) {
              if (current_node->get_current_value() < parent_node->get_current_value()) {
                current_node->set_current_value(parent_node->get_current_value());
              }
            }
            while (current_node->get_state() == 0) {
              bit = packet_header->get_bit();
              if (bit == 0) {
                current_node->set_current_value(current_node->get_current_value() + 1);
              } else {
                current_node->set_value(current_node->get_current_value());
                current_node->set_state(1);
              }
            }
          }
        }
        block->num_ZBP = current_node->get_value();
        block->Lblock  = 3;
      }
    } else {
      // this codeblock has been already included in previous packets
      bit = packet_header->get_bit();
      if (bit) {
        is_included = true;
      } else {
        is_included = false;
      }
    }

    if (is_included) {
      // Retrieve number of coding passes in this layer
      uint8_t new_passes = 1;
      bit                = packet_header->get_bit();
      new_passes += bit;
      if (new_passes >= 2) {
        bit = packet_header->get_bit();
        new_passes += bit;
        if (new_passes >= 3) {
          new_passes += static_cast<uint8_t>(packet_header->get_N_bits(2));
          if (new_passes >= 6) {
            new_passes += static_cast<uint8_t>(packet_header->get_N_bits(5));
            if (new_passes >= 37) {
              new_passes += static_cast<uint8_t>(packet_header->get_N_bits(7));
            }
          }
        }
      }
      block->layer_passes[layer_idx] = new_passes;
      // Retrieve Lblock
      while ((bit = packet_header->get_bit()) == 1) {
        block->Lblock++;
      }
      uint8_t bypass_term_threshold = 0;
      uint8_t bits_to_read          = 0;
      uint8_t pass_index            = block->num_passes;
      uint32_t segment_bytes        = 0;
      int32_t segment_passes        = 0;
      uint8_t next_segment_passes   = 0;
      int32_t href_passes, pass_bound;
      if (block->Cmodes & HT_PHLD) {
        href_passes    = (pass_index + new_passes - 1) % 3;
        segment_passes = new_passes - href_passes;
        pass_bound     = 2;
        bits_to_read   = block->Lblock;
        if (segment_passes < 1) {
          // No possible HT Cleanup pass here; may have placeholder passes
          // or an original J2K block bit-stream (in MIXED mode).
          segment_passes = new_passes;
          while (pass_bound <= segment_passes) {
            bits_to_read++;
            pass_bound += pass_bound;
          }
          segment_bytes = packet_header->get_N_bits(bits_to_read);
          if (segment_bytes) {
            if (block->Cmodes & HT_MIXED) {
              block->Cmodes &= ~(HT_PHLD | HT);
            } else {
              printf("ERROR: Length information for a HT-codeblock is invalid\n");
              exit(EXIT_FAILURE);
            }
          }
        } else {
          while (pass_bound <= segment_passes) {
            bits_to_read++;
            pass_bound += pass_bound;
          }
          segment_bytes = packet_header->get_N_bits(bits_to_read);
          if (segment_bytes) {
            // No more placeholder passes
            if (!(block->Cmodes & HT_MIXED)) {
              // Must be the first HT Cleanup pass
              if (segment_bytes < 2) {
                printf(
                    "ERROR: Length information for a HT-codeblock is "
                    "invalid\n");
                exit(EXIT_FAILURE);
              }
              next_segment_passes = 2;
              block->Cmodes &= ~(HT_PHLD);
            } else if (block->Lblock > 3 && segment_bytes > 1
                       && (segment_bytes >> (bits_to_read - 1)) == 0) {
              // Must be the first HT Cleanup pass, since length MSB is 0
              next_segment_passes = 2;
              block->Cmodes &= ~(HT_PHLD);
            } else {
              // Must have an original (non-HT) block coding pass
              block->Cmodes &= ~(HT_PHLD | HT);
              segment_passes = new_passes;
              while (pass_bound <= segment_passes) {
                bits_to_read++;
                pass_bound += pass_bound;
                segment_bytes <<= 1;
                segment_bytes += packet_header->get_bit();
              }
            }
          } else {
            // Probably parsing placeholder passes, but we need to read an
            // extra length bit to verify this, since prior to the first
            // HT Cleanup pass, the number of length bits read for a
            // contributing code-block is dependent on the number of passes
            // being included, as if it were a non-HT code-block.
            segment_passes = new_passes;
            if (pass_bound <= segment_passes) {
              while (true) {
                bits_to_read++;
                pass_bound += pass_bound;
                segment_bytes <<= 1;
                segment_bytes += packet_header->get_bit();
                if (pass_bound > segment_passes) {
                  break;
                }
              }
              if (segment_bytes) {
                if (block->Cmodes & HT_MIXED) {
                  block->Cmodes &= ~(HT_PHLD | HT);
                } else {
                  printf(
                      "ERROR: Length information for a HT-codeblock is "
                      "invalid\n");
                  exit(EXIT_FAILURE);
                }
              }
            }
          }
        }
      } else if (block->Cmodes & HT) {
        // Quality layer commences with a non-initial HT coding pass
        assert(bits_to_read == 0);
        segment_passes = block->num_passes % 3;
        if (segment_passes == 0) {
          // num_passes is a HT Cleanup pass; next segment has refinement passes
          segment_passes      = 1;
          next_segment_passes = 2;
          if (segment_bytes == 1) {
            printf("ERROR: something wrong 943.\n");
            exit(EXIT_FAILURE);
          }
        } else {
          // new pass = 1 means num_passes is HT SigProp; 2 means num_passes is
          // HT MagRef pass
          if (new_passes > 1) {
            segment_passes = 3 - segment_passes;
          } else {
            segment_passes = 1;
          }
          next_segment_passes = 1;
          bits_to_read        = segment_passes - 1;
        }
        bits_to_read += block->Lblock;
        segment_bytes = packet_header->get_N_bits(bits_to_read);
      } else if (!(block->Cmodes & (RESTART | BYPASS))) {
        // Common case for non-HT code-blocks; we have only one segment
        bits_to_read   = block->Lblock + int_log2(new_passes);
        segment_bytes  = packet_header->get_N_bits(bits_to_read);
        segment_passes = new_passes;
      } else if (block->Cmodes & RESTART) {
        // RESTART MODE
        bits_to_read        = block->Lblock;
        segment_bytes       = packet_header->get_N_bits(bits_to_read);
        segment_passes      = 1;
        next_segment_passes = 1;
      } else {
        // BYPASS MODE
        bypass_term_threshold = 10;
        assert(bits_to_read == 0);
        if (block->num_passes < bypass_term_threshold) {
          // May have from 1 to 10 uninterrupted passes before 1st RAW SigProp
          segment_passes = bypass_term_threshold - block->num_passes;
          if (segment_passes > new_passes) {
            segment_passes = new_passes;
          }
          while ((2 << bits_to_read) <= segment_passes) {
            bits_to_read++;
          }
          next_segment_passes = 2;
        } else if ((block->num_passes - bypass_term_threshold) % 3 < 2) {
          // new_passes = 0 means `num_passes' is a RAW SigProp; 1 means
          // `num_passes' is a RAW MagRef pass
          if (new_passes > 1) {
            segment_passes = 2 - (block->num_passes - bypass_term_threshold) % 3;
          } else {
            segment_passes = 1;
          }
          bits_to_read        = segment_passes - 1;
          next_segment_passes = 1;
        } else {
          // `num_passes' is an isolated Cleanup pass that precedes a RAW
          // SigProp pass
          segment_passes      = 1;
          next_segment_passes = 2;
        }
        bits_to_read += block->Lblock;
        segment_bytes = packet_header->get_N_bits(bits_to_read);
      }

      block->num_passes += segment_passes;
      while (block->pass_length.size() < block->num_passes) {
        block->pass_length.push_back(0);
      }
      block->pass_length[block->num_passes - 1] = segment_bytes;
      number_of_bytes += segment_bytes;

      uint8_t primary_passes, secondary_passes;
      uint32_t primary_bytes, secondary_bytes, fast_skip_bytes = 0;
      bool empty_set;
      if ((block->Cmodes & (HT | HT_PHLD)) == HT) {
        new_passes -= segment_passes;
        primary_passes          = segment_passes + block->fast_skip_passes;
        block->fast_skip_passes = 0;
        primary_bytes           = segment_bytes;
        secondary_passes        = 0;
        secondary_bytes         = 0;
        empty_set               = false;
        if (next_segment_passes == 2 && segment_bytes == 0) {
          empty_set = true;
        }
        while (new_passes > 0) {
          if (new_passes > 1) {
            segment_passes = next_segment_passes;
          } else {
            segment_passes = 1;
          }
          next_segment_passes = 3 - next_segment_passes;
          bits_to_read        = block->Lblock + segment_passes - 1;
          segment_bytes       = packet_header->get_N_bits(bits_to_read);
          new_passes -= segment_passes;
          if (next_segment_passes == 2) {
            // This is a FAST Cleanup pass
            assert(segment_passes == 1);
            if (segment_bytes != 0) {
              // This will have to be the new primary
              if (segment_bytes < 2) {
                printf("ERROR: Something wrong 1037\n");
                exit(EXIT_FAILURE);
              }
              fast_skip_bytes += primary_bytes + secondary_bytes;
              primary_passes += 1 + secondary_passes;
              primary_bytes    = segment_bytes;
              secondary_bytes  = 0;
              secondary_passes = 0;
              primary_passes += block->fast_skip_passes;
              block->fast_skip_passes = 0;
              empty_set               = false;
            } else {
              // Starting a new empty set
              block->fast_skip_passes++;
              empty_set = true;
            }
          } else {
            // This is a FAST Refinement pass
            if (empty_set) {
              if (segment_bytes != 0) {
                printf("ERROR: Something wrong 1225\n");
                exit(EXIT_FAILURE);
              }
              block->fast_skip_passes += segment_passes;
            } else {
              secondary_passes = segment_passes;
              secondary_bytes  = segment_bytes;
            }
          }

          block->num_passes += segment_passes;
          while (block->pass_length.size() < block->num_passes) {
            block->pass_length.push_back(0);
          }
          block->pass_length[block->num_passes - 1] = segment_bytes;
          number_of_bytes += segment_bytes;
        }
      } else {
        new_passes -= segment_passes;
        block->pass_length[block->num_passes - 1] = segment_bytes;
        while (new_passes > 0) {
          if (bypass_term_threshold != 0) {
            if (new_passes > 1) {
              segment_passes = next_segment_passes;
            } else {
              segment_passes = 1;
            }
            next_segment_passes = 3 - next_segment_passes;
            bits_to_read        = block->Lblock + segment_passes - 1;
          } else {
            assert((block->Cmodes & RESTART) != 0);
            segment_passes = 1;
            bits_to_read   = block->Lblock;
          }
          segment_bytes = packet_header->get_N_bits(bits_to_read);
          new_passes -= segment_passes;
          block->num_passes += segment_passes;
          while (block->pass_length.size() < block->num_passes) {
            block->pass_length.push_back(0);
          }
          block->pass_length[block->num_passes - 1] = segment_bytes;
          number_of_bytes += segment_bytes;
        }
      }
    } else {
      // this layer has no contribution from this codeblock
      block->layer_passes[layer_idx] = 0;
    }
  }
}

void j2k_precinct_subband::generate_packet_header(packet_header_writer &header, uint16_t layer_idx) {
  // if no codeblock exists, nothing to do is left
  if (this->num_codeblock_x * this->num_codeblock_y == 0) {
    return;
  }

  uint16_t threshold;
  j2k_codeblock *blk;

  // set value for each leaf node
  for (uint32_t idx = 0; idx < this->num_codeblock_x * this->num_codeblock_y; ++idx) {
    blk = this->access_codeblock(idx);
    if (blk->length) {
      this->inclusion_info->node[idx].set_value(blk->layer_start[0]);
    } else {
      this->inclusion_info->node[idx].set_value(1);
    }
    this->ZBP_info->node[idx].set_value(blk->num_ZBP);
  }
  // Building tagtree structures
  this->inclusion_info->build();
  this->ZBP_info->build();

  tagtree_node *current_node, *parent_node;

  for (uint32_t idx = 0; idx < this->num_codeblock_x * this->num_codeblock_y; ++idx) {
    blk                            = this->access_codeblock(idx);
    uint8_t preceding_layer_passes = 0;
    for (int i = 0; i < layer_idx; ++i) {
      preceding_layer_passes += blk->layer_passes[i];
    }

    if (preceding_layer_passes == 0) {
      // this is the first contribution
      current_node = this->get_inclusion_node(idx);

      // build tagtree search path
      std::vector<int32_t> tree_path;
      uint8_t max_tree_level = current_node->get_level();
      max_tree_level += (max_tree_level == 0xFF) ? 0 : 1;
      tree_path.reserve(max_tree_level);
      tree_path.push_back(current_node->get_index());
      while (current_node->get_parent_index() >= 0) {
        current_node = this->get_inclusion_node(current_node->get_parent_index());
        tree_path.push_back(current_node->get_index());
      }

      // inclusion tagtree coding
      threshold = layer_idx;
      for (int32_t i = tree_path.size() - 1; i >= 0; --i) {
        current_node = this->get_inclusion_node(tree_path[i]);
        if (current_node->get_state() == 0) {
          if (current_node->get_parent_index() < 0) {
            parent_node = nullptr;
          } else {
            parent_node = this->get_inclusion_node(current_node->get_parent_index());
          }
          if (current_node->get_level() > 0 && parent_node != nullptr) {
            if (current_node->get_current_value() < parent_node->get_current_value()) {
              current_node->set_current_value(parent_node->get_current_value());
            }
          }
          if (current_node->get_current_value() <= threshold) {
            if (current_node->get_value() <= threshold) {
              header.put_bit(1);
              current_node->set_state(1);
            } else {
              header.put_bit(0);
              current_node->set_current_value(current_node->get_current_value() + 1);
            }
          }
        }
      }

      // number of zero bit plane tagtree coding
      if (blk->layer_passes[layer_idx] > 0) {
        blk->already_included = true;
        blk->Lblock           = 3;

        for (int32_t i = tree_path.size() - 1; i >= 0; --i) {
          current_node = this->get_ZBP_node(tree_path[i]);
          if (current_node->get_parent_index() < 0) {
            threshold = 0;
          } else {
            threshold = this->get_ZBP_node(current_node->get_parent_index())->get_value();
          }
          while (current_node->get_state() == 0) {
            while (threshold < current_node->get_value()) {
              header.put_bit(0);
              threshold++;
            }
            current_node->set_state(1);
            header.put_bit(1);
          }
        }
      }
    } else {
      // if we get here, this codeblock has been included in at least one of preceding layers
      header.put_bit(std::min((uint8_t)1, blk->layer_passes[layer_idx]));
    }

    const uint8_t num_passes = blk->layer_passes[layer_idx];
    if (num_passes) {
      // number of coding passes encoding
      if (blk->layer_passes[layer_idx] > 0) {
        assert(num_passes < 165);
        if (num_passes == 1) {
          header.put_bit(0);
        } else if (num_passes == 2) {
          header.put_Nbits(0x2, 2);
        } else if (num_passes < 6) {
          header.put_Nbits(0x3, 2);
          header.put_Nbits(num_passes - 3, 2);
        } else if (num_passes < 37) {
          header.put_Nbits(0xF, 4);
          header.put_Nbits(num_passes - 6, 5);
        } else {
          header.put_Nbits(0x1FF, 9);
          header.put_Nbits(num_passes - 37, 7);
        }
      }

      // compute number of coded bytes in this layer_idx
      uint8_t l0 = blk->layer_start[layer_idx];
      uint8_t l1 = blk->layer_passes[layer_idx];

      uint32_t buf_start = 0, buf_end = 0;
      // NOTE: the following code to derive number_of_bytes shall be improved
      if (l0) {
        for (int i = 0; i < l0; ++i) {
          buf_start += blk->pass_length[i];
        }
      }
      for (int i = 0; i < l0 + l1; ++i) {
        buf_end += blk->pass_length[i];
      }
      uint32_t number_of_bytes = buf_end - buf_start;

      // length coding: currently only for HT Cleanup pass
      int new_passes         = static_cast<int32_t>(num_passes);
      uint8_t bits_to_write  = 0;
      uint8_t pass_idx       = l0;
      uint32_t segment_bytes = 0;
      uint8_t segment_passes = 0;
      uint32_t total_bytes   = 0;
      uint8_t length_bits    = 0;

      while (new_passes > 0) {
        assert(blk->Cmodes & HT);
        segment_passes = (pass_idx == 0) ? 1 : new_passes;

        length_bits = 0;
        // length_bits = floor(log2(segment_passes))
        while ((2 << length_bits) <= segment_passes) {
          length_bits++;
        }
        length_bits += blk->Lblock;

        segment_bytes = 0;
        auto val      = static_cast<uint32_t>(segment_passes);
        while (val > 0) {
          segment_bytes += blk->pass_length[pass_idx + val - 1];
          val--;
        }

        while (segment_bytes >= (1 << length_bits)) {
          header.put_bit(1);
          length_bits++;
          blk->Lblock++;
        }
        new_passes -= segment_passes;
        pass_idx += segment_passes;
        total_bytes += segment_bytes;
      }
      header.put_bit(0);

      bits_to_write  = 0;
      pass_idx       = l0;
      segment_bytes  = 0;
      segment_passes = 0;
      new_passes     = num_passes;
      total_bytes    = 0;

      while (new_passes > 0) {
        assert(blk->Cmodes & HT);
        segment_passes = (pass_idx == 0) ? 1 : new_passes;

        length_bits = 0;
        // length_bits = floor(log2(segment_passes))
        while ((2 << length_bits) <= segment_passes) {
          length_bits++;
        }
        length_bits += blk->Lblock;

        segment_bytes = 0;
        auto val      = static_cast<uint32_t>(segment_passes);
        while (val > 0) {
          segment_bytes += blk->pass_length[pass_idx + val - 1];
          val--;
        }

        for (int i = length_bits - 1; i >= 0; --i) {
          header.put_bit((segment_bytes & (1 << i)) >> i);
        }
        new_passes -= segment_passes;
        pass_idx += segment_passes;
        total_bytes += segment_bytes;
      }
    }
  }  // end of outer for loop
}

/********************************************************************************
 * j2k_precinct
 *******************************************************************************/
j2k_precinct::j2k_precinct(const uint8_t &r, const uint32_t &idx, const element_siz &p0,
                           const element_siz &p1,
                           const std::unique_ptr<std::unique_ptr<j2k_subband>[]> &subband,
                           const uint16_t &num_layers, const element_siz &codeblock_size,
                           const uint8_t &Cmodes)
    : j2k_region(p0, p1),
      index(idx),
      resolution(r),
      num_bands((resolution == 0) ? 1 : 3),
      length(0),
      packet_header(nullptr),
      packet_header_length(0) {
  length = 0;  // for encoder only

  this->pband          = std::make_unique<std::unique_ptr<j2k_precinct_subband>[]>(num_bands);
  const uint8_t xob[4] = {0, 1, 0, 1};
  const uint8_t yob[4] = {0, 0, 1, 1};
  for (unsigned long i = 0; i < num_bands; ++i) {
    const uint8_t sr = (subband[i]->orientation == BAND_LL) ? 1 << 0 : 1 << 1;
    const element_siz pbpos0(ceil_int(pos0.x - xob[subband[i]->orientation], sr),
                             ceil_int(pos0.y - yob[subband[i]->orientation], sr));
    const element_siz pbpos1(ceil_int(pos1.x - xob[subband[i]->orientation], sr),
                             ceil_int(pos1.y - yob[subband[i]->orientation], sr));
    this->pband[i] = std::make_unique<j2k_precinct_subband>(
        subband[i]->orientation, subband[i]->M_b, subband[i]->R_b, subband[i]->transformation,
        subband[i]->delta * subband[i]->nominal_range, subband[i]->i_samples, subband[i]->f_samples,
        subband[i]->pos0, subband[i]->pos1, pbpos0, pbpos1, num_layers, codeblock_size, Cmodes);
  }
}

j2k_precinct_subband *j2k_precinct::access_pband(uint8_t b) {
  assert(b < num_bands);
  return this->pband[b].get();
}

/********************************************************************************
 * j2k_subband
 *******************************************************************************/
j2k_subband::j2k_subband(element_siz p0, element_siz p1, uint8_t orientation, uint8_t transformation,
                         uint8_t R_b, uint8_t epsilon_b, uint16_t mantissa_b, uint8_t M_b, float delta,
                         float nominal_range, int16_t *ibuf, float *fbuf)
    : j2k_region(p0, p1),
      orientation(orientation),
      transformation(transformation),
      R_b(R_b),
      epsilon_b(epsilon_b),
      mantissa_b(mantissa_b),
      M_b(M_b),
      delta(delta),
      nominal_range(nominal_range),
      i_samples(nullptr),
      f_samples(nullptr) {
  // TODO: consider reduce_NL value
  const uint32_t num_samples = (pos1.x - pos0.x) * (pos1.y - pos0.y);
  if (num_samples) {
    if (orientation != BAND_LL) {
      // If not the lowest resolution, buffers for subbands shall be created.
      i_samples = static_cast<int16_t *>(aligned_mem_alloc(sizeof(int16_t) * num_samples, 32));
      f_samples = static_cast<float *>(aligned_mem_alloc(sizeof(float) * num_samples, 32));
      memset(i_samples, 0, sizeof(int16_t) * num_samples);
#if defined(__AVX2__)
      __m256 mZero = _mm256_setzero_ps();
      for (uint32_t n = 0; n < round_down(num_samples, SIMD_LEN_F32); n += SIMD_LEN_F32) {
        __m256 tmp = _mm256_load_ps(f_samples + n);  //*((__m256 *)(f_samples + n));
        _mm256_store_ps(f_samples + n, mZero);
      }
      for (uint32_t n = round_down(num_samples, SIMD_LEN_F32); n < num_samples; ++n) {
        f_samples[n] = 0.0;
      }
#else
      for (uint32_t n = 0; n < num_samples; ++n) {
        f_samples[n] = 0.0;
      }
#endif
    } else {
      i_samples = ibuf;
      f_samples = fbuf;
    }
  }
}

j2k_subband::~j2k_subband() {
  // printf("INFO: destructor of j2k_subband %d is called\n", orientation);
  if (orientation != BAND_LL) {
    aligned_mem_free(i_samples);
    aligned_mem_free(f_samples);
  }
}

void j2k_subband::quantize() {
  // if lossless, no quantization
  if (this->transformation) {
    return;
  }
  // Define scaling values for lossy transformation.
  // The value for LL should be 1.0 because LL bands should not be quantized except for the lowest LL.
  // Those LL bands which are not quantized are scaled in j2k_resolution::scale() in j2k_tile::encode()
  constexpr float K     = 1.2301741 / 2;
  constexpr float K1    = 0.8128931;
  constexpr float KK[4] = {1.0, K * K1, K1 * K, K * K};

  uint32_t length = (this->pos1.x - this->pos0.x) * (this->pos1.y - this->pos0.y);
  // TODO: The following code works correctly, but needs to be improved for speed
  float fscale = static_cast<float>(1 << this->R_b) / this->delta;
  // if HL or LH or HH, scaling values should be included
  fscale *= KK[this->orientation];
  fscale /= (1 << (FRACBITS));
  for (uint32_t n = 0; n < length; ++n) {
    auto fval = static_cast<float>(this->i_samples[n]);
    fval *= fscale;
    this->i_samples[n] = static_cast<int16_t>(floorf(fabs(fval)));
    if (fval < 0.0) {
      this->i_samples[n] *= -1;
    }
  }
}

/********************************************************************************
 * j2k_resolution
 *******************************************************************************/
j2k_resolution::j2k_resolution(const uint8_t &r, const element_siz &p0, const element_siz &p1,
                               const uint32_t &w, const uint32_t &h)
    : j2k_region(p0, p1),
      index(r),
      precincts(nullptr),
      subbands(nullptr),
      // public
      num_bands((index == 0) ? 1 : 3),
      npw(w),
      nph(h),
      is_empty((npw * nph == 0)),
      i_samples(nullptr),
      f_samples(nullptr) {
  const uint32_t num_samples = (pos1.x - pos0.x) * (pos1.y - pos0.y);
  // create buffer of LL band
  i_samples = nullptr;
  if (!is_empty) {
    if (index == 0) {
      i_samples = static_cast<int16_t *>(aligned_mem_alloc(sizeof(int16_t) * num_samples, 32));
      f_samples = static_cast<float *>(aligned_mem_alloc(sizeof(float) * num_samples, 32));
      memset(i_samples, 0, sizeof(int16_t) * num_samples);
#if defined(__AVX2__)
      __m256 mZero = _mm256_setzero_ps();
      for (uint32_t n = 0; n < round_down(num_samples, SIMD_LEN_F32); n += SIMD_LEN_F32) {
        _mm256_store_ps(f_samples + n, mZero);
      }
      for (uint32_t n = round_down(num_samples, SIMD_LEN_F32); n < num_samples; ++n) {
        f_samples[n] = 0.0;
      }
#else
      for (uint32_t n = 0; n < num_samples; ++n) {
        f_samples[n] = 0.0;
      }
#endif

    } else {
      i_samples = static_cast<int16_t *>(aligned_mem_alloc(sizeof(int16_t) * num_samples, 32));
      f_samples = static_cast<float *>(aligned_mem_alloc(sizeof(float) * num_samples, 32));
    }
  }
}

j2k_resolution::~j2k_resolution() {
  aligned_mem_free(i_samples);
  aligned_mem_free(f_samples);
}

void j2k_resolution::create_subbands(element_siz &p0, element_siz &p1, uint8_t NL, uint8_t transformation,
                                     std::vector<uint8_t> &exponents, std::vector<uint16_t> &mantissas,
                                     uint8_t num_guard_bits, uint8_t qstyle, uint8_t bitdepth) {
  subbands = std::make_unique<std::unique_ptr<j2k_subband>[]>(num_bands);
  uint8_t i;
  uint8_t b;
  uint8_t xob[4]    = {0, 1, 0, 1};
  uint8_t yob[4]    = {0, 0, 1, 1};
  uint8_t gain_b[4] = {0, 1, 1, 2};
  uint8_t bstart    = (index == 0) ? 0 : 1;
  uint8_t bstop     = (index == 0) ? 0 : 3;
  uint8_t nb        = (index == 0) ? NL - index : NL - index + 1;
  uint8_t nb_1 = nb_1 = (nb > 0) ? (nb - 1) : 0;
  uint8_t epsilon_b, R_b, M_b;
  uint16_t mantissa_b;
  float delta, nominal_range;

  for (i = 0, b = bstart; b <= bstop; b++, i++) {
    const element_siz pos0(ceil_int(p0.x - (1 << (nb_1)) * xob[b], 1 << nb),
                           ceil_int(p0.y - (1 << (nb_1)) * yob[b], 1 << nb));
    const element_siz pos1(ceil_int(p1.x - (1 << (nb_1)) * xob[b], 1 << nb),
                           ceil_int(p1.y - (1 << (nb_1)) * yob[b], 1 << nb));

    // nominal range does not have any effect to lossless path and LL(lowest) in lossy path
#ifndef BIBO
    nominal_range = 1.0;
#endif
    if (transformation == 1) {
      // lossless
      epsilon_b = exponents[3 * (NL - nb) + b];
      M_b       = epsilon_b + num_guard_bits - 1;
      delta     = 1.0;
    } else {
      assert(transformation == 0);
      // lossy compression
      if (qstyle == 1) {
        // dervied
        epsilon_b  = exponents[0] - NL + nb;
        mantissa_b = mantissas[0];
      } else {
        // expounded
        assert(qstyle == 2);
        epsilon_b  = exponents[3 * (NL - nb) + b];
        mantissa_b = mantissas[3 * (NL - nb) + b];
      }
      M_b   = epsilon_b + num_guard_bits - 1;
      R_b   = bitdepth + gain_b[b];
      delta = (static_cast<float>(1 << (((int32_t)R_b))) / (static_cast<float>(1 << epsilon_b)))
              * (1.0f + (static_cast<float>(mantissa_b)) / (static_cast<float>(1 << 11)));
#ifndef BIBO
      nominal_range = band_nominal_ranges[nb_1][b];
#endif
    }
    subbands[i] = std::make_unique<j2k_subband>(pos0, pos1, b, transformation, R_b, epsilon_b, mantissa_b,
                                                M_b, delta, nominal_range, i_samples, f_samples);
  }
}

void j2k_resolution::create_precincts(element_siz log2PP, uint16_t numlayers, element_siz codeblock_size,
                                      uint8_t Cmodes) {
  // precinct size signalled in header
  const element_siz PP(1 << log2PP.x, 1 << log2PP.y);
  // offset of horizontal precinct index
  const uint32_t idxoff_x = (pos0.x - 0) / PP.x;
  // offset of vertical precinct index
  const uint32_t idxoff_y = (pos0.y - 0) / PP.y;

  if (!is_empty) {
    precincts = std::make_unique<std::unique_ptr<j2k_precinct>[]>(npw * nph);
    for (uint32_t i = 0; i < npw * nph; i++) {
      uint32_t x, y;
      x = i % npw;
      y = i / npw;
      const element_siz prcpos0(std::max(pos0.x, 0 + PP.x * (x + idxoff_x)),
                                std::max(pos0.y, 0 + PP.y * (y + idxoff_y)));
      const element_siz prcpos1(std::min(pos1.x, 0 + PP.x * (x + 1 + idxoff_x)),
                                std::min(pos1.y, 0 + PP.y * (y + 1 + idxoff_y)));
      precincts[i] = std::make_unique<j2k_precinct>(index, i, prcpos0, prcpos1, subbands, numlayers,
                                                    codeblock_size, Cmodes);
    }
  }
}

j2k_precinct *j2k_resolution::access_precinct(uint32_t p) {
  if (p > npw * nph) {
    printf("ERROR: attempt to access precinct whose index is out of the valid range.\n");
    exit(EXIT_FAILURE);
  }
  return this->precincts[p].get();
}

j2c_packet::j2c_packet(const uint16_t l, const uint8_t r, const uint16_t c, const uint32_t p,
                       j2k_precinct *const cp, uint8_t num_bands)
    : layer(l), resolution(r), component(c), precinct(p), header(nullptr), body(nullptr) {
  // get length of the corresponding precinct
  length = cp->get_length();
  // create buffer to accommodate packet header and body
  buf        = std::make_unique<uint8_t[]>(length);
  size_t pos = cp->packet_header_length;
  // copy packet header to packet buffer
  for (int i = 0; i < pos; ++i) {
    buf[i] = cp->packet_header[i];
  }
  // copy packet body to packet buffer
  for (uint8_t b = 0; b < num_bands; ++b) {
    j2k_precinct_subband *cpb = cp->access_pband(b);
    const uint32_t num_cblks  = cpb->num_codeblock_x * cpb->num_codeblock_y;
    for (uint32_t block_index = 0; block_index < num_cblks; ++block_index) {
      j2k_codeblock *block = cpb->access_codeblock(block_index);
      memcpy(&buf[pos], block->get_compressed_data(), sizeof(uint8_t) * block->length);
      pos += block->length;
    }
  }
}

j2k_subband *j2k_resolution::access_subband(uint8_t b) { return this->subbands[b].get(); }

void j2k_resolution::scale() {
  // if lossless, no scaling
  if (this->subbands[0]->transformation) {
    return;
  }
  constexpr float K     = 1.2301741 / 2;
  constexpr float K1    = 0.8128931;
  constexpr float KK[4] = {K1 * K1, K * K1, K1 * K, K * K};
  uint32_t length       = (this->pos1.x - this->pos0.x) * (this->pos1.y - this->pos0.y);
  // TODO: The following code works correctly, but needs to be improved for speed
  float fscale = KK[0];
  for (uint32_t n = 0; n < length; ++n) {
    int16_t sign = this->i_samples[n] & 0x8000;
    float fval   = fabs(static_cast<float>(this->i_samples[n]));
    fval *= fscale;
    this->i_samples[n] = static_cast<int16_t>(fval + 0.5);
    if (sign) {
      this->i_samples[n] *= -1;
    }
  }
}

/********************************************************************************
 * j2k_tile_part
 *******************************************************************************/
j2k_tile_part::j2k_tile_part(uint16_t num_components) {
  tile_index      = 0;
  tile_part_index = 0;
  body            = nullptr;
  length          = 0;
  header          = std::make_unique<j2k_tilepart_header>(num_components);
}

void j2k_tile_part::set_SOT(SOT_marker &tmpSOT) {
  this->tile_index      = tmpSOT.get_tile_index();
  this->tile_part_index = tmpSOT.get_tile_part_index();
  // this->header->SOT     = SOT_marker;
  this->header->SOT = tmpSOT;
}

int j2k_tile_part::read(j2c_src_memory &in) {
  uint32_t length_of_tilepart_markers = this->header->read(in);
  this->length += this->header->SOT.get_tile_part_length() - length_of_tilepart_markers;
  this->body = in.get_buf_pos();

  if (in.forward_Nbytes(this->length) == EXIT_FAILURE) {
    printf("ERROR: forward_Nbytes exceeds tehe size of buffer.\n");
    exit(EXIT_FAILURE);
  }
  return EXIT_SUCCESS;
}

uint16_t j2k_tile_part::get_tile_index() const { return tile_index; }

uint8_t j2k_tile_part::get_tile_part_index() const { return tile_part_index; }

uint32_t j2k_tile_part::get_length() const { return this->length; }

uint8_t *j2k_tile_part::get_buf() { return body; }

void j2k_tile_part::set_tile_index(uint16_t t) { tile_index = t; }
void j2k_tile_part::set_tile_part_index(uint8_t tp) { tile_part_index = tp; }

/********************************************************************************
 * j2k_tile_component
 *******************************************************************************/
j2k_tile_component::j2k_tile_component() {
  index              = 0;
  bitdepth           = 0;
  NL                 = 0;
  Cmodes             = 0;
  codeblock_size.x   = 0;
  codeblock_size.y   = 0;
  precinct_size      = {};
  transformation     = 0;
  quantization_style = 0;
  exponents          = {};
  mantissas          = {};
  num_guard_bits     = 0;
  ROIshift           = 0;
  samples            = nullptr;
  fsamples           = nullptr;
  resolution         = nullptr;
}

j2k_tile_component::~j2k_tile_component() {
  aligned_mem_free(samples);
  aligned_mem_free(fsamples);
}

void j2k_tile_component::init(j2k_main_header *hdr, j2k_tilepart_header *tphdr, j2k_tile_base *tile,
                              uint16_t c, std::vector<int32_t *> img) {
  index = c;
  // copy both coding and quantization styles from COD or tile-part COD
  NL                 = tile->NL;
  codeblock_size     = tile->codeblock_size;
  Cmodes             = tile->Cmodes;
  transformation     = tile->transformation;
  precinct_size      = tile->precinct_size;
  quantization_style = tile->quantization_style;
  exponents          = tile->exponents;
  mantissas          = tile->mantissas;
  num_guard_bits     = tile->num_guard_bits;

  // set bitdepth from main header
  bitdepth = hdr->SIZ->get_bitdepth(c);
  element_siz subsampling;
  hdr->SIZ->get_subsampling_factor(subsampling, c);

  pos0.x = ceil_int(tile->pos0.x, subsampling.x);
  pos0.y = ceil_int(tile->pos0.y, subsampling.y);
  pos1.x = ceil_int(tile->pos1.x, subsampling.x);
  pos1.y = ceil_int(tile->pos1.y, subsampling.y);

  // apply COC, if any
  if (!tphdr->COC.empty()) {
    for (auto &i : tphdr->COC) {
      if (i->get_component_index() == c) {
        setCOCparams(i.get());
      }
    }
  } else {
    for (auto &i : hdr->COC) {
      if (i->get_component_index() == c) {
        setCOCparams(i.get());
      }
    }
  }

  // apply QCC, if any
  if (!tphdr->QCC.empty()) {
    for (auto &i : tphdr->QCC) {
      if (i->get_component_index() == c) {
        setQCCparams(i.get());
      }
    }
  } else {
    for (auto &i : hdr->QCC) {
      if (i->get_component_index() == c) {
        setQCCparams(i.get());
      }
    }
  }

  // apply RGN, if any
  if (!tphdr->RGN.empty()) {
    for (auto &i : tphdr->RGN) {
      if (i->get_component_index() == c) {
        setRGNparams(i.get());
      }
    }
  } else {
    for (auto &i : hdr->RGN) {
      if (i->get_component_index() == c) {
        setRGNparams(i.get());
      }
    }
  }

  // We consider "-reduce" parameter value to determine necessary buffer size.
  const uint32_t num_bufsamples =
      (ceil_int(pos1.x, 1 << tile->reduce_NL) - ceil_int(pos0.x, 1 << tile->reduce_NL))
      * (ceil_int(pos1.y, 1 << tile->reduce_NL) - ceil_int(pos0.y, 1 << tile->reduce_NL));
  // samples         = new int32_t[(pos1.x - pos0.x) * (pos1.y - pos0.y)]();
  samples = static_cast<int32_t *>(aligned_mem_alloc(sizeof(int32_t) * num_bufsamples, 32));
  // fsamples = new float[(tc1.x - tc0.x) * (tc1.y - tc0.y)]();
  fsamples = static_cast<float *>(aligned_mem_alloc(sizeof(float) * num_bufsamples, 32));

  // create tile samples, only for encoding;
  if (!img.empty()) {
    int32_t *const src_origin = img[this->index];
    const int32_t height      = pos1.y - pos0.y;
    const int32_t width       = pos1.x - pos0.x;
    element_siz imgsize;
    hdr->SIZ->get_image_size(imgsize);
    const uint32_t stride = imgsize.x;
#pragma omp parallel for  // default(none) shared(height, width, src, stride)
    for (int i = 0; i < height; ++i) {
      int32_t *src = src_origin + (pos0.y + i) * stride + pos0.x;
      int32_t *dst = samples + i * width;
      memcpy(dst, src, sizeof(int32_t) * width);
      //      for (int j = 0; j < width; ++j) {
      //        *dst++ = *src++;
      //      }
    }
  }
}

void j2k_tile_component::setCOCparams(COC_marker *COC) {
  // coding style related properties
  NL = COC->get_dwt_levels();
  COC->get_codeblock_size(codeblock_size);
  Cmodes         = COC->get_Cmodes();
  transformation = COC->get_transformation();
  precinct_size.clear();
  precinct_size.reserve(NL + 1);
  element_siz tmp;
  for (uint8_t r = 0; r <= NL; r++) {
    COC->get_precinct_size(tmp, r);
    precinct_size.emplace_back(tmp);
  }
}

void j2k_tile_component::setQCCparams(QCC_marker *QCC) {
  quantization_style = QCC->get_quantization_style();
  exponents.clear();
  mantissas.clear();
  if (quantization_style != 1) {
    // lossless or lossy expounded
    for (uint8_t nb = 0; nb < 3 * NL + 1; nb++) {
      exponents.push_back(QCC->get_exponents(nb));
      if (quantization_style == 2) {
        // lossy expounded
        mantissas.push_back(QCC->get_mantissas(nb));
      }
    }
  } else {
    // lossy derived
    exponents.push_back(QCC->get_exponents(0));
    mantissas.push_back(QCC->get_mantissas(0));
  }
  num_guard_bits = QCC->get_number_of_guardbits();
}

void j2k_tile_component::setRGNparams(RGN_marker *RGN) { this->ROIshift = RGN->get_ROIshift(); }

int32_t *j2k_tile_component::get_sample_address(uint32_t x, uint32_t y) {
  return this->samples + x + y * (this->pos1.x - this->pos0.x);
}

float *j2k_tile_component::get_fsample_address(uint32_t x, uint32_t y) {
  return this->fsamples + x + y * (this->pos1.x - this->pos0.x);
}

uint8_t j2k_tile_component::get_dwt_levels() { return this->NL; }

uint8_t j2k_tile_component::get_transformation() { return this->transformation; }

uint8_t j2k_tile_component::get_Cmodes() const { return this->Cmodes; }

uint8_t j2k_tile_component::get_bitdepth() const { return this->bitdepth; }

element_siz j2k_tile_component::get_precinct_size(uint8_t r) { return this->precinct_size[r]; }

element_siz j2k_tile_component::get_codeblock_size() { return this->codeblock_size; }

uint8_t j2k_tile_component::get_ROIshift() const { return this->ROIshift; }

j2k_resolution *j2k_tile_component::access_resolution(uint8_t r) { return this->resolution[r].get(); }

void j2k_tile_component::create_resolutions(uint16_t numlayers) {
  resolution =
      std::make_unique<std::unique_ptr<j2k_resolution>[]>(NL + 1);  // new j2k_resolution *[NL + 1];

  // float tmp_ranges[4] = {1.0, 1.0, 1.0, 1.0};
  // float child_ranges[32][4];
  // float normalization       = 1.0;
  // uint8_t normalizing_shift = 0;
  // uint8_t nb;
  // uint8_t nshift[32];

  // for (uint8_t r = 1; r <= NL; r++) {
  //   uint64_t d = 1 << (NL - r);
  //   const element_siz respos0(static_cast<uint32_t>(ceil_int(pos0.x, d)),
  //                             static_cast<uint32_t>(ceil_int(pos0.y, d)));
  //   const element_siz respos1(static_cast<uint32_t>(ceil_int(pos1.x, d)),
  //                             static_cast<uint32_t>(ceil_int(pos1.y, d)));
  //   nb = NL - r + 1;
  //   find_child_ranges(tmp_ranges, normalizing_shift, normalization, nb, respos0.x, respos1.x,
  //                     respos0.y, respos1.y);
  //   for (uint8_t b = 0; b < 4; ++b) {
  //     child_ranges[nb][b] = tmp_ranges[b];
  //   }
  // }
  // for (uint8_t b = 0; b < 4; ++b) {
  //   child_ranges[0][b] = tmp_ranges[b];
  // }

  for (uint8_t r = 0; r <= NL; r++) {
    uint64_t d = 1 << (NL - r);
    const element_siz respos0(static_cast<uint32_t>(ceil_int(pos0.x, d)),
                              static_cast<uint32_t>(ceil_int(pos0.y, d)));
    const element_siz respos1(static_cast<uint32_t>(ceil_int(pos1.x, d)),
                              static_cast<uint32_t>(ceil_int(pos1.y, d)));
    const element_siz log2PP = get_precinct_size(r);
    const element_siz PP(1 << log2PP.x, 1 << log2PP.y);
    const uint32_t npw = (respos1.x > respos0.x) ? ceil_int(respos1.x, PP.x) - respos0.x / PP.x : 0;
    const uint32_t nph = (respos1.y > respos0.y) ? ceil_int(respos1.y, PP.y) - respos0.y / PP.y : 0;

    resolution[r] = std::make_unique<j2k_resolution>(r, respos0, respos1, npw, nph);
    // resolution[r]->set_nominal_ranges(child_ranges[r]);
    resolution[r]->normalizing_upshift = normalizing_upshift[NL - r];
    resolution[r]->create_subbands(this->pos0, this->pos1, this->NL, this->transformation, this->exponents,
                                   this->mantissas, this->num_guard_bits, this->quantization_style,
                                   this->bitdepth);
    resolution[r]->create_precincts(precinct_size[r], numlayers, codeblock_size, Cmodes);
  }

#ifdef BIBO
  j2k_subband *cb;
  element_siz p0, p1;
  float child_ranges[4];
  uint8_t normalizing_upshift;
  float normalization = 1.0;
  // uint8_t r, lev;
  for (uint8_t r = NL, lev = 0; r > 0; --r, ++lev) {
    p0 = resolution[r]->get_pos0();
    p1 = resolution[r]->get_pos1();
    // if (p0.x == p1.x || p0.y == p1.y) {
    //   continue;
    // }
    // printf("u0 = %d, u1 = %d, v0 = %d, v1 = %d\n", p0.x, p1.x, p0.y, p1.y);
    find_child_ranges(child_ranges, normalizing_upshift, normalization, lev, this->NL, p0.x, p1.x, p0.y,
                      p1.y);
    // printf("r = %d, lut = %dm calc = %d\n", r, resolution[r]->normalizing_upshift,
    //        normalizing_upshift);
    // printf("r = %d\n", resolution[r]->get_index());
    for (uint8_t i = 0; i < resolution[r]->num_bands; ++i) {
      cb        = resolution[r]->access_subband(i);
      cb->range = child_ranges[cb->orientation];
      // printf("[%d] = %f\n", i, cb->range);
    }
    // printf("\n");
  }

  for (uint8_t i = 0; i < resolution[0]->num_bands; ++i) {
    cb        = resolution[0]->access_subband(i);
    cb->range = child_ranges[cb->orientation];

    // resolution[0]->normalizing_upshift = 0;  // normalizing_upshift;
    // printf("r = %d, lut = %dm calc = %d\n", 0, resolution[0]->normalizing_upshift,
    //        normalizing_upshift);
    // printf("[%d] = %f\n", i, cb->range);
  }
  // printf("\n");

  for (uint8_t r = 0; r <= NL; ++r) {
    resolution[r]->create_precincts(precinct_size[r], numlayers, codeblock_size, Cmodes);
  }
#endif
}

void j2k_tile_component::perform_dc_offset(const uint8_t transformation, const bool is_signed) {
  const uint8_t shiftup   = (transformation) ? 0 : FRACBITS - this->bitdepth;
  const int32_t DC_OFFSET = (is_signed) ? 0 : 1 << (this->bitdepth - 1 + shiftup);
  const uint32_t length   = (this->pos1.x - this->pos0.x) * (this->pos1.y - this->pos0.y);
#if defined(__AVX2__)
  __m256i doff = _mm256_set1_epi32(DC_OFFSET);
  for (uint32_t i = 0; i < round_down(length, SIMD_LEN_I32); i += SIMD_LEN_I32) {
    __m256i sp                  = *((__m256i *)(samples + i));
    *((__m256i *)(samples + i)) = _mm256_sub_epi32(_mm256_slli_epi32(sp, shiftup), doff);
  }
  for (uint32_t i = round_down(length, SIMD_LEN_I32); i < length; ++i) {
    samples[i] <<= shiftup;
    samples[i] -= DC_OFFSET;
  }
#else
  for (uint32_t i = 0; i < length; ++i) {
    samples[i] <<= shiftup;
    samples[i] -= DC_OFFSET;
  }
#endif
}

/********************************************************************************
 * j2k_tile
 *******************************************************************************/
j2k_tile::j2k_tile()
    : tile_part(),
      index(0),
      num_components(0),
      use_SOP(false),
      use_EPH(false),
      progression_order(0),
      numlayers(0),
      MCT(0),
      length(0),
      tile_buf(nullptr),
      packet_header(nullptr),
      num_tile_part(0),
      current_tile_part_pos(-1),
      tcomp(nullptr),
      ppt_header(nullptr),
      num_packets(0),
      packet(nullptr),
      // reduce_NL(0),
      Ccap15(0) {}

void j2k_tile::setCODparams(COD_marker *COD) {
  // coding style related properties
  use_SOP           = COD->is_use_SOP();
  use_EPH           = COD->is_use_EPH();
  progression_order = COD->get_progression_order();
  numlayers         = COD->get_number_of_layers();
  MCT               = COD->use_color_trafo();
  NL                = COD->get_dwt_levels();
  COD->get_codeblock_size(codeblock_size);
  Cmodes         = COD->get_Cmodes();
  transformation = COD->get_transformation();
  precinct_size.clear();
  precinct_size.reserve(NL + 1);
  element_siz tmp;
  for (uint8_t r = 0; r <= NL; r++) {
    COD->get_precinct_size(tmp, r);
    precinct_size.emplace_back(tmp);
  }
}

void j2k_tile::setQCDparams(QCD_marker *QCD) {
  quantization_style = QCD->get_quantization_style();
  exponents.clear();
  mantissas.clear();
  if (quantization_style != 1) {
    // lossless or lossy expounded
    for (uint8_t nb = 0; nb < 3 * NL + 1; nb++) {
      exponents.push_back(QCD->get_exponents(nb));
      if (quantization_style == 2) {
        // lossy expounded
        mantissas.push_back(QCD->get_mantissas(nb));
      }
    }
  } else {
    // lossy derived
    exponents.push_back(QCD->get_exponents(0));
    mantissas.push_back(QCD->get_mantissas(0));
  }
  num_guard_bits = QCD->get_number_of_guardbits();
}

void j2k_tile::dec_init(uint16_t idx, j2k_main_header &main_header, uint8_t reduce_levels) {
  index          = idx;
  num_components = main_header.SIZ->get_num_components();
  // set coding style related properties from main header
  setCODparams(main_header.COD.get());
  // set quantization style related properties from main header
  setQCDparams(main_header.QCD.get());
  // set Ccap15(HTJ2K only or mixed)
  Ccap15 = (main_header.CAP != nullptr) ? main_header.CAP->get_Ccap(15) : 0;
  // set resolution reduction, if any
  reduce_NL = reduce_levels;
}

void j2k_tile::add_tile_part(SOT_marker &tmpSOT, j2c_src_memory &in, j2k_main_header &main_header) {
  this->length += tmpSOT.get_tile_part_length();
  // this->tile_part.push_back(move(make_unique<j2k_tile_part>(num_components)));
  this->tile_part.push_back(std::make_unique<j2k_tile_part>(num_components));
  this->num_tile_part++;
  this->current_tile_part_pos++;
  this->tile_part[current_tile_part_pos]->set_SOT(tmpSOT);
  this->tile_part[current_tile_part_pos]->read(in);
  j2k_tilepart_header *tphdr = this->tile_part[current_tile_part_pos]->header.get();

  uint8_t tile_part_index = tmpSOT.get_tile_part_index();
  if (tile_part_index == 0) {
    // this->set_index(tmpSOT->get_tile_index(), main_header);
    element_siz numTiles;
    element_siz Siz, Osiz, Tsiz, TOsiz;
    main_header.get_number_of_tiles(numTiles.x, numTiles.y);
    uint16_t p = this->index % numTiles.x;
    uint16_t q = this->index / numTiles.x;
    main_header.SIZ->get_image_size(Siz);
    main_header.SIZ->get_image_origin(Osiz);
    main_header.SIZ->get_tile_size(Tsiz);
    main_header.SIZ->get_tile_origin(TOsiz);

    this->pos0.x = std::max(TOsiz.x + p * Tsiz.x, Osiz.x);
    this->pos0.y = std::max(TOsiz.y + q * Tsiz.y, Osiz.y);
    this->pos1.x = std::min(TOsiz.x + (p + 1) * Tsiz.x, Siz.x);
    this->pos1.y = std::min(TOsiz.y + (q + 1) * Tsiz.y, Siz.y);

    // set coding style related properties from tile-part header
    if (tphdr->COD != nullptr) {
      setCODparams(tphdr->COD.get());
    }
    // set quantization style related properties from tile-part header
    if (tphdr->QCD != nullptr) {
      setQCDparams(tphdr->QCD.get());
    }

    // create tile components
    this->tcomp = std::make_unique<j2k_tile_component[]>(num_components);
    for (uint16_t c = 0; c < num_components; c++) {
      this->tcomp[c].init(&main_header, tphdr, this, c);
    }

    // apply POC, if any
    if (tphdr->POC != nullptr) {
      for (unsigned long i = 0; i < tphdr->POC->nPOC; ++i) {
        porder_info.add(tphdr->POC->RSpoc[i], tphdr->POC->CSpoc[i], tphdr->POC->LYEpoc[i],
                        tphdr->POC->REpoc[i], tphdr->POC->CEpoc[i], tphdr->POC->Ppoc[i]);
      }
    } else if (main_header.POC != nullptr) {
      for (unsigned long i = 0; i < main_header.POC->nPOC; ++i) {
        porder_info.add(main_header.POC->RSpoc[i], main_header.POC->CSpoc[i], main_header.POC->LYEpoc[i],
                        main_header.POC->REpoc[i], main_header.POC->CEpoc[i], main_header.POC->Ppoc[i]);
      }
    }
  }
}

void j2k_tile::create_tile_buf(j2k_main_header &main_header) {
  uint8_t t = 0;
  // concatenate tile-parts into a tile
  this->tile_buf = std::make_unique<buf_chain>(num_tile_part);
  for (unsigned long i = 0; i < num_tile_part; i++) {
    // If a length of a tile-part is 0, buf number 't' should not be
    // incremented!!
    if (this->tile_part[i]->get_length() != 0) {
      this->tile_buf->set_buf_node(t, this->tile_part[i]->get_buf(), this->tile_part[i]->get_length());
      t++;
    }
  }
  this->tile_buf->activate();
  // If PPT exits, create PPT buf chain
  if (!this->tile_part[0]->header->PPT.empty()) {
    ppt_header = std::make_unique<buf_chain>();
    for (unsigned long i = 0; i < num_tile_part; i++) {
      for (auto &ppt : this->tile_part[i]->header->PPT) {
        ppt_header->add_buf_node(ppt->pptbuf, ppt->pptlen);
      }
    }
    ppt_header->activate();
  }

  // determine the location of the packet header
  this->packet_header = nullptr;
  if (main_header.get_ppm_header() != nullptr) {
    assert(ppt_header == nullptr);
    sbst_packet_header = *(main_header.get_ppm_header());
    // TODO: this implementation may not be enough because this does not
    // consider "tile-part". MARK: find begining of the packet header for a
    // tile!
    sbst_packet_header.activate(this->index);
    packet_header = &sbst_packet_header;  // main_header.get_ppm_header();
  } else if (ppt_header != nullptr) {
    assert(main_header.get_ppm_header() == nullptr);
    packet_header = ppt_header.get();
  } else {
    packet_header = this->tile_buf.get();
  }

  // create resolution levels, subbands, precincts, precinct subbands and codeblocks
  uint32_t max_res_precincts = 0;
  uint8_t c_NL;
  uint8_t max_c_NL = 0;
  for (uint16_t c = 0; c < num_components; c++) {
    this->tcomp[c].create_resolutions(numlayers);
    c_NL = this->tcomp[c].NL;
    if (c_NL < this->reduce_NL) {
      printf(
          "ERROR: Resolution level reduction exceeds the DWT level of "
          "component %d.\n",
          c);
      exit(EXIT_FAILURE);
    }
    max_c_NL = std::max(c_NL, max_c_NL);
    j2k_resolution *cr;
    for (uint8_t r = 0; r <= c_NL; r++) {
      cr = this->tcomp[c].access_resolution(r);
      num_packets += cr->npw * cr->nph;
      max_res_precincts = std::max(cr->npw * cr->nph, max_res_precincts);
    }
  }
  num_packets *= numlayers;
  // TODO: create packets with progression order
  this->packet = std::make_unique<j2c_packet[]>(num_packets);
  // need to construct a POC marker from progression order value in COD marker
  porder_info.add(0, 0, this->numlayers, max_c_NL + 1, this->num_components, this->progression_order);
  uint8_t PO, RS, RE, r, local_RE;
  uint16_t LYE, CS, CE, c, l;
  uint32_t p;
  bool x_cond, y_cond;
  std::vector<std::vector<std::vector<std::vector<bool>>>> is_packet_read(
      numlayers, std::vector<std::vector<std::vector<bool>>>(
                     max_c_NL + 1, std::vector<std::vector<bool>>(
                                       num_components, std::vector<bool>(max_res_precincts, false))));
  j2k_resolution *cr   = nullptr;
  j2k_precinct *cp     = nullptr;
  int32_t packet_count = 0;
  for (unsigned long i = 0; i < porder_info.nPOC; ++i) {
    RS  = porder_info.RSpoc[i];
    CS  = porder_info.CSpoc[i];
    LYE = std::min(porder_info.LYEpoc[i], numlayers);
    RE  = porder_info.REpoc[i];
    CE  = std::min(porder_info.CEpoc[i], num_components);
    PO  = porder_info.Ppoc[i];
    std::vector<std::vector<uint32_t>> p_x(static_cast<uint32_t>(num_components),
                                           std::vector<uint32_t>(static_cast<uint32_t>(max_c_NL + 1), 0));
    std::vector<std::vector<uint32_t>> p_y(static_cast<uint32_t>(num_components),
                                           std::vector<uint32_t>(static_cast<uint32_t>(max_c_NL + 1), 0));

    element_siz PP, cPP, csub;
    std::vector<uint32_t> x_examin;
    std::vector<uint32_t> y_examin;

    switch (PO) {
      case 0:  // LRCP
        for (l = 0; l < LYE; l++) {
          for (r = RS; r < RE; r++) {
            for (c = CS; c < CE; c++) {
              c_NL = this->tcomp[c].NL;
              if (r <= c_NL) {
                cr = this->tcomp[c].access_resolution(r);
                if (!cr->is_empty) {
                  for (p = 0; p < cr->npw * cr->nph; p++) {
                    cp = cr->access_precinct(p);  //&cr->precincts[p];
                    if (!is_packet_read[l][r][c][p]) {
                      this->packet[packet_count++] = j2c_packet(l, r, c, p, packet_header, tile_buf.get());
                      this->read_packet(cp, l, cr->num_bands);
                      is_packet_read[l][r][c][p] = true;
                    }
                  }
                }
              }
            }
          }
        }
        break;
      case 1:  // RLCP
        for (r = RS; r < RE; r++) {
          for (l = 0; l < LYE; l++) {
            for (c = CS; c < CE; c++) {
              c_NL = this->tcomp[c].NL;
              if (r <= c_NL) {
                cr = this->tcomp[c].access_resolution(r);
                if (!cr->is_empty) {
                  for (p = 0; p < cr->npw * cr->nph; p++) {
                    cp = cr->access_precinct(p);
                    if (!is_packet_read[l][r][c][p]) {
                      this->packet[packet_count++] = j2c_packet(l, r, c, p, packet_header, tile_buf.get());
                      this->read_packet(cp, l, cr->num_bands);
                      is_packet_read[l][r][c][p] = true;
                    }
                  }
                }
              }
            }
          }
        }
        break;
      case 2:  // RPCL
        this->find_gcd_of_precinct_size(PP);
        x_examin.push_back(pos0.x);
        for (uint32_t x = 0; x < this->pos1.x; x += (1 << PP.x)) {
          if (x > pos0.x) {
            x_examin.push_back(x);
          }
        }
        y_examin.push_back(pos0.y);
        for (uint32_t y = 0; y < this->pos1.y; y += (1 << PP.y)) {
          if (y > pos0.y) {
            y_examin.push_back(y);
          }
        }
        for (r = RS; r < RE; r++) {
          for (uint32_t y : y_examin) {
            for (uint32_t x : x_examin) {
              for (c = CS; c < CE; c++) {
                c_NL = this->tcomp[c].NL;
                if (r <= c_NL) {
                  cPP = this->tcomp[c].get_precinct_size(r);
                  cr  = this->tcomp[c].access_resolution(r);
                  if (!cr->is_empty) {
                    element_siz tr0 = cr->get_pos0();
                    x_cond          = false;
                    y_cond          = false;
                    main_header.SIZ->get_subsampling_factor(csub, c);
                    x_cond = (x % (csub.x * (1 << (cPP.x + c_NL - r))) == 0)
                             || ((x == pos0.x)
                                 && ((tr0.x * (1 << (c_NL - r))) % (1 << (cPP.x + c_NL - r)) != 0));
                    y_cond = (y % (csub.y * (1 << (cPP.y + c_NL - r))) == 0)
                             || ((y == pos0.y)
                                 && ((tr0.y * (1 << (c_NL - r))) % (1 << (cPP.y + c_NL - r)) != 0));
                    if (x_cond && y_cond) {
                      p  = p_x[c][r] + p_y[c][r] * cr->npw;
                      cp = cr->access_precinct(p);
                      for (l = 0; l < LYE; l++) {
                        if (!is_packet_read[l][r][c][p]) {
                          this->packet[packet_count++] =
                              j2c_packet(l, r, c, p, packet_header, tile_buf.get());
                          this->read_packet(cp, l, cr->num_bands);
                          is_packet_read[l][r][c][p] = true;
                        }
                      }
                      p_x[c][r] += 1;
                      if (p_x[c][r] == cr->npw) {
                        p_x[c][r] = 0;
                        p_y[c][r] += 1;
                      }
                    }
                  }
                }
              }
            }
          }
        }
        break;
      case 3:  // PCRL
        this->find_gcd_of_precinct_size(PP);
        x_examin.push_back(pos0.x);
        for (uint32_t x = 0; x < this->pos1.x; x += (1 << PP.x)) {
          if (x > pos0.x) {
            x_examin.push_back(x);
          }
        }
        y_examin.push_back(pos0.y);
        for (uint32_t y = 0; y < this->pos1.y; y += (1 << PP.y)) {
          if (y > pos0.y) {
            y_examin.push_back(y);
          }
        }
        for (uint32_t y : y_examin) {
          for (uint32_t x : x_examin) {
            for (c = CS; c < CE; c++) {
              c_NL     = this->tcomp[c].NL;
              local_RE = ((c_NL + 1) < RE) ? (c_NL + 1) : RE;
              for (r = RS; r < local_RE; r++) {
                cPP = this->tcomp[c].get_precinct_size(r);
                cr  = this->tcomp[c].access_resolution(r);
                if (!cr->is_empty) {
                  element_siz tr0 = cr->get_pos0();
                  x_cond          = false;
                  y_cond          = false;
                  main_header.SIZ->get_subsampling_factor(csub, c);
                  x_cond =
                      (x % (csub.x * (1 << (cPP.x + c_NL - r))) == 0)
                      || ((x == pos0.x) && ((tr0.x * (1 << (c_NL - r))) % (1 << (cPP.x + c_NL - r)) != 0));
                  y_cond =
                      (y % (csub.y * (1 << (cPP.y + c_NL - r))) == 0)
                      || ((y == pos0.y) && ((tr0.y * (1 << (c_NL - r))) % (1 << (cPP.y + c_NL - r)) != 0));
                  if (x_cond && y_cond) {
                    p  = p_x[c][r] + p_y[c][r] * cr->npw;
                    cp = cr->access_precinct(p);
                    for (l = 0; l < LYE; l++) {
                      if (!is_packet_read[l][r][c][p]) {
                        this->packet[packet_count++] =
                            j2c_packet(l, r, c, p, packet_header, tile_buf.get());
                        is_packet_read[l][r][c][p] = true;
                        this->read_packet(cp, l, cr->num_bands);
                      }
                    }
                    p_x[c][r] += 1;
                    if (p_x[c][r] == cr->npw) {
                      p_x[c][r] = 0;
                      p_y[c][r] += 1;
                    }
                  }
                }
              }
            }
          }
        }
        break;
      case 4:  // CPRL
        this->find_gcd_of_precinct_size(PP);
        x_examin.push_back(pos0.x);
        for (uint32_t x = 0; x < this->pos1.x; x += (1 << PP.x)) {
          if (x > pos0.x) {
            x_examin.push_back(x);
          }
        }
        y_examin.push_back(pos0.y);
        for (uint32_t y = 0; y < this->pos1.y; y += (1 << PP.y)) {
          if (y > pos0.y) {
            y_examin.push_back(y);
          }
        }
        for (c = CS; c < CE; c++) {
          c_NL     = this->tcomp[c].NL;
          local_RE = ((c_NL + 1) < RE) ? (c_NL + 1) : RE;
          for (uint32_t y : y_examin) {
            for (uint32_t x : x_examin) {
              for (r = RS; r < local_RE; r++) {
                cPP = this->tcomp[c].get_precinct_size(r);
                cr  = this->tcomp[c].access_resolution(r);
                if (!cr->is_empty) {
                  element_siz tr0 = cr->get_pos0();
                  x_cond          = false;
                  y_cond          = false;
                  main_header.SIZ->get_subsampling_factor(csub, c);
                  x_cond =
                      (x % (csub.x * (1 << (cPP.x + c_NL - r))) == 0)
                      || ((x == pos0.x) && ((tr0.x * (1 << (c_NL - r))) % (1 << (cPP.x + c_NL - r)) != 0));
                  y_cond =
                      (y % (csub.y * (1 << (cPP.y + c_NL - r))) == 0)
                      || ((y == pos0.y) && ((tr0.y * (1 << (c_NL - r))) % (1 << (cPP.y + c_NL - r)) != 0));
                  if (x_cond && y_cond) {
                    p  = p_x[c][r] + p_y[c][r] * cr->npw;
                    cp = cr->access_precinct(p);
                    for (l = 0; l < LYE; l++) {
                      if (!is_packet_read[l][r][c][p]) {
                        this->packet[packet_count++] =
                            j2c_packet(l, r, c, p, packet_header, tile_buf.get());
                        is_packet_read[l][r][c][p] = true;
                        this->read_packet(cp, l, cr->num_bands);
                      }
                    }
                    p_x[c][r] += 1;
                    if (p_x[c][r] == cr->npw) {
                      p_x[c][r] = 0;
                      p_y[c][r] += 1;
                    }
                  }
                }
              }
            }
          }
        }
        break;

      default:
        printf(
            "ERROR: Progression order number shall be in the range from 0 "
            "to 4\n");
        exit(EXIT_FAILURE);
        // break;
    }
  }
}

void j2k_tile::construct_packets(j2k_main_header &main_header) {
  // derive number of packets needed to be created
  num_packets                = 0;
  uint32_t max_res_precincts = 0;
  uint8_t c_NL;
  uint8_t max_c_NL = 0;

  for (uint16_t c = 0; c < num_components; c++) {
    c_NL     = this->tcomp[c].NL;
    max_c_NL = std::max(c_NL, max_c_NL);
    j2k_resolution *cr;
    for (uint8_t r = 0; r <= c_NL; r++) {
      cr = this->tcomp[c].access_resolution(r);
      num_packets += cr->npw * cr->nph;
      max_res_precincts = std::max(cr->npw * cr->nph, max_res_precincts);
    }
  }
  num_packets *= this->numlayers;
  this->packet = std::make_unique<j2c_packet[]>(num_packets);

  // need to construct a POC marker from progression order value in COD marker
  porder_info.add(0, 0, this->numlayers, max_c_NL + 1, this->num_components, this->progression_order);
  uint8_t PO, RS, RE, r, local_RE;
  uint16_t LYE, CS, CE, c, l;
  uint32_t p;
  bool x_cond, y_cond;
  std::vector<std::vector<std::vector<std::vector<bool>>>> is_packet_created(
      numlayers, std::vector<std::vector<std::vector<bool>>>(
                     max_c_NL + 1, std::vector<std::vector<bool>>(
                                       num_components, std::vector<bool>(max_res_precincts, false))));

  j2k_resolution *cr   = nullptr;
  j2k_precinct *cp     = nullptr;
  int32_t packet_count = 0;
  for (unsigned long i = 0; i < porder_info.nPOC; ++i) {
    RS  = porder_info.RSpoc[i];
    CS  = porder_info.CSpoc[i];
    LYE = std::min(porder_info.LYEpoc[i], numlayers);
    RE  = porder_info.REpoc[i];
    CE  = std::min(porder_info.CEpoc[i], num_components);
    PO  = porder_info.Ppoc[i];
    std::vector<std::vector<uint32_t>> p_x(static_cast<uint32_t>(num_components),
                                           std::vector<uint32_t>(static_cast<uint32_t>(max_c_NL + 1), 0));
    std::vector<std::vector<uint32_t>> p_y(static_cast<uint32_t>(num_components),
                                           std::vector<uint32_t>(static_cast<uint32_t>(max_c_NL + 1), 0));

    element_siz PP, cPP, csub;
    std::vector<uint32_t> x_examin;
    std::vector<uint32_t> y_examin;

    switch (PO) {
      case 0:  // LRCP
        for (l = 0; l < LYE; l++) {
          for (r = RS; r < RE; r++) {
            for (c = CS; c < CE; c++) {
              c_NL = this->tcomp[c].NL;
              if (r <= c_NL) {
                cr = this->tcomp[c].access_resolution(r);
                if (!cr->is_empty) {
                  for (p = 0; p < cr->npw * cr->nph; p++) {
                    cp = cr->access_precinct(p);
                    if (!is_packet_created[l][r][c][p]) {
                      this->packet[packet_count++]  = j2c_packet(l, r, c, p, cp, cr->num_bands);
                      is_packet_created[l][r][c][p] = true;
                    }
                  }
                }
              }
            }
          }
        }
        break;
      case 1:  // RLCP
        for (r = RS; r < RE; r++) {
          for (l = 0; l < LYE; l++) {
            for (c = CS; c < CE; c++) {
              c_NL = this->tcomp[c].NL;
              if (r <= c_NL) {
                cr = this->tcomp[c].access_resolution(r);
                if (!cr->is_empty) {
                  for (p = 0; p < cr->npw * cr->nph; p++) {
                    cp = cr->access_precinct(p);
                    if (!is_packet_created[l][r][c][p]) {
                      this->packet[packet_count++]  = j2c_packet(l, r, c, p, cp, cr->num_bands);
                      is_packet_created[l][r][c][p] = true;
                    }
                  }
                }
              }
            }
          }
        }
        break;
      case 2:  // RPCL
        this->find_gcd_of_precinct_size(PP);
        x_examin.push_back(pos0.x);
        for (uint32_t x = 0; x < this->pos1.x; x += (1 << PP.x)) {
          if (x > pos0.x) {
            x_examin.push_back(x);
          }
        }
        y_examin.push_back(pos0.y);
        for (uint32_t y = 0; y < this->pos1.y; y += (1 << PP.y)) {
          if (y > pos0.y) {
            y_examin.push_back(y);
          }
        }
        for (r = RS; r < RE; r++) {
          for (uint32_t y : y_examin) {
            for (uint32_t x : x_examin) {
              for (c = CS; c < CE; c++) {
                c_NL = this->tcomp[c].NL;
                if (r <= c_NL) {
                  cPP = this->tcomp[c].get_precinct_size(r);
                  cr  = this->tcomp[c].access_resolution(r);
                  if (!cr->is_empty) {
                    element_siz tr0 = cr->get_pos0();
                    x_cond          = false;
                    y_cond          = false;
                    main_header.SIZ->get_subsampling_factor(csub, c);
                    x_cond = (x % (csub.x * (1 << (cPP.x + c_NL - r))) == 0)
                             || ((x == pos0.x)
                                 && ((tr0.x * (1 << (c_NL - r))) % (1 << (cPP.x + c_NL - r)) != 0));
                    y_cond = (y % (csub.y * (1 << (cPP.y + c_NL - r))) == 0)
                             || ((y == pos0.y)
                                 && ((tr0.y * (1 << (c_NL - r))) % (1 << (cPP.y + c_NL - r)) != 0));
                    if (x_cond && y_cond) {
                      p  = p_x[c][r] + p_y[c][r] * cr->npw;
                      cp = cr->access_precinct(p);
                      for (l = 0; l < LYE; l++) {
                        if (!is_packet_created[l][r][c][p]) {
                          this->packet[packet_count++]  = j2c_packet(l, r, c, p, cp, cr->num_bands);
                          is_packet_created[l][r][c][p] = true;
                        }
                      }
                      p_x[c][r] += 1;
                      if (p_x[c][r] == cr->npw) {
                        p_x[c][r] = 0;
                        p_y[c][r] += 1;
                      }
                    }
                  }
                }
              }
            }
          }
        }
        break;
      case 3:  // PCRL
        this->find_gcd_of_precinct_size(PP);
        x_examin.push_back(pos0.x);
        for (uint32_t x = 0; x < this->pos1.x; x += (1 << PP.x)) {
          if (x > pos0.x) {
            x_examin.push_back(x);
          }
        }
        y_examin.push_back(pos0.y);
        for (uint32_t y = 0; y < this->pos1.y; y += (1 << PP.y)) {
          if (y > pos0.y) {
            y_examin.push_back(y);
          }
        }
        for (uint32_t y : y_examin) {
          for (uint32_t x : x_examin) {
            for (c = CS; c < CE; c++) {
              c_NL     = this->tcomp[c].NL;
              local_RE = ((c_NL + 1) < RE) ? (c_NL + 1) : RE;
              for (r = RS; r < local_RE; r++) {
                cPP = this->tcomp[c].get_precinct_size(r);
                cr  = this->tcomp[c].access_resolution(r);
                if (!cr->is_empty) {
                  element_siz tr0 = cr->get_pos0();
                  x_cond          = false;
                  y_cond          = false;
                  main_header.SIZ->get_subsampling_factor(csub, c);
                  x_cond =
                      (x % (csub.x * (1 << (cPP.x + c_NL - r))) == 0)
                      || ((x == pos0.x) && ((tr0.x * (1 << (c_NL - r))) % (1 << (cPP.x + c_NL - r)) != 0));
                  y_cond =
                      (y % (csub.y * (1 << (cPP.y + c_NL - r))) == 0)
                      || ((y == pos0.y) && ((tr0.y * (1 << (c_NL - r))) % (1 << (cPP.y + c_NL - r)) != 0));
                  if (x_cond && y_cond) {
                    p  = p_x[c][r] + p_y[c][r] * cr->npw;
                    cp = cr->access_precinct(p);
                    for (l = 0; l < LYE; l++) {
                      if (!is_packet_created[l][r][c][p]) {
                        this->packet[packet_count++]  = j2c_packet(l, r, c, p, cp, cr->num_bands);
                        is_packet_created[l][r][c][p] = true;
                      }
                    }
                    p_x[c][r] += 1;
                    if (p_x[c][r] == cr->npw) {
                      p_x[c][r] = 0;
                      p_y[c][r] += 1;
                    }
                  }
                }
              }
            }
          }
        }
        break;
      case 4:  // CPRL
        this->find_gcd_of_precinct_size(PP);
        x_examin.push_back(pos0.x);
        for (uint32_t x = 0; x < this->pos1.x; x += (1 << PP.x)) {
          if (x > pos0.x) {
            x_examin.push_back(x);
          }
        }
        y_examin.push_back(pos0.y);
        for (uint32_t y = 0; y < this->pos1.y; y += (1 << PP.y)) {
          if (y > pos0.y) {
            y_examin.push_back(y);
          }
        }
        for (c = CS; c < CE; c++) {
          c_NL     = this->tcomp[c].NL;
          local_RE = ((c_NL + 1) < RE) ? (c_NL + 1) : RE;
          for (uint32_t y : y_examin) {
            for (uint32_t x : x_examin) {
              for (r = RS; r < local_RE; r++) {
                cPP = this->tcomp[c].get_precinct_size(r);
                cr  = this->tcomp[c].access_resolution(r);
                if (!cr->is_empty) {
                  element_siz tr0 = cr->get_pos0();
                  x_cond          = false;
                  y_cond          = false;
                  main_header.SIZ->get_subsampling_factor(csub, c);
                  x_cond =
                      (x % (csub.x * (1 << (cPP.x + c_NL - r))) == 0)
                      || ((x == pos0.x) && ((tr0.x * (1 << (c_NL - r))) % (1 << (cPP.x + c_NL - r)) != 0));
                  y_cond =
                      (y % (csub.y * (1 << (cPP.y + c_NL - r))) == 0)
                      || ((y == pos0.y) && ((tr0.y * (1 << (c_NL - r))) % (1 << (cPP.y + c_NL - r)) != 0));
                  if (x_cond && y_cond) {
                    p  = p_x[c][r] + p_y[c][r] * cr->npw;
                    cp = cr->access_precinct(p);
                    for (l = 0; l < LYE; l++) {
                      if (!is_packet_created[l][r][c][p]) {
                        this->packet[packet_count++]  = j2c_packet(l, r, c, p, cp, cr->num_bands);
                        is_packet_created[l][r][c][p] = true;
                      }
                    }
                    p_x[c][r] += 1;
                    if (p_x[c][r] == cr->npw) {
                      p_x[c][r] = 0;
                      p_y[c][r] += 1;
                    }
                  }
                }
              }
            }
          }
        }
        break;
      default:
        printf(
            "ERROR: Progression order number shall be in the range from 0 "
            "to 4\n");
        exit(EXIT_FAILURE);
    }
  }
}

void j2k_tile::write_packets(j2c_destination_base &outbuf) {
  for (int i = 0; i < this->num_tile_part; ++i) {
    j2k_tile_part *tp = this->tile_part[i].get();
    // set tile-part length
    this->tile_part[0]->header->SOT.set_tile_part_length(this->length
                                                         + 6 * this->num_packets * this->is_use_SOP());
    tp->header->SOT.write(outbuf);
    // write packets
    for (int n = 0; n < this->num_packets; ++n) {
      if (this->is_use_SOP()) {
        outbuf.put_word(_SOP);
        outbuf.put_word(0x0004);
        outbuf.put_word(n % 65536);
      }
      outbuf.put_N_bytes(this->packet[n].buf.get(), this->packet[n].length);
    }
  }
}

void j2k_tile::decode(j2k_main_header &main_header) {
  typedef void (*decode_func)(j2k_codeblock *, const uint8_t);
  static decode_func block_decode_funcs[2];
  block_decode_funcs[0] = j2k_decode;
  block_decode_funcs[1] = htj2k_decode;
#pragma omp parallel for  // default(none) shared(block_decode_funcs)
  for (uint16_t c = 0; c < num_components; c++) {
    const uint8_t ROIshift       = this->tcomp[c].get_ROIshift();
    const uint8_t NL             = this->tcomp[c].get_dwt_levels();
    const uint8_t transformation = this->tcomp[c].get_transformation();
    for (int8_t lev = NL; lev >= this->reduce_NL; --lev) {
      j2k_resolution *cr           = this->tcomp[c].access_resolution(NL - lev);
      const uint32_t num_precincts = cr->npw * cr->nph;
      for (uint32_t p = 0; p < num_precincts; p++) {
        j2k_precinct *cp = cr->access_precinct(p);
        for (uint8_t b = 0; b < cr->num_bands; b++) {
          j2k_precinct_subband *cpb = cp->access_pband(b);
          const uint32_t num_cblks  = cpb->num_codeblock_x * cpb->num_codeblock_y;
          for (uint32_t block_index = 0; block_index < num_cblks; ++block_index) {
            j2k_codeblock *block = cpb->access_codeblock(block_index);
            // only decode a codeblock having non-zero coding passes
            if (block->num_passes) {
              block_decode_funcs[(block->Cmodes & HT) >> 6](block, ROIshift);
            }
          }  // end of codeblock loop
        }    // end of subbnad loop
      }      // end of precinct loop

      // lowest resolution level (= LL0) does not have HL, LH, HH bands.
      if (lev != NL) {
        j2k_resolution *pcr            = this->tcomp[c].access_resolution(NL - lev - 1);
        const element_siz top_left     = cr->get_pos0();
        const element_siz bottom_right = cr->get_pos1();
        const uint32_t u0              = top_left.x;
        const uint32_t u1              = bottom_right.x;
        const uint32_t v0              = top_left.y;
        const uint32_t v1              = bottom_right.y;

        j2k_subband *HL = cr->access_subband(0);
        j2k_subband *LH = cr->access_subband(1);
        j2k_subband *HH = cr->access_subband(2);

        if (u1 != u0 && v1 != v0) {
          idwt_2d_sr_fixed(cr->i_samples, pcr->i_samples, HL->i_samples, LH->i_samples, HH->i_samples, u0,
                           u1, v0, v1, transformation, pcr->normalizing_upshift);
        }
      }
    }  // end of resolution loop
    j2k_resolution *cr = this->tcomp[c].access_resolution(NL - reduce_NL);
    int16_t *sp        = cr->i_samples;
    int32_t *dp        = this->tcomp[c].get_sample_address(0, 0);

    // modify coordinates of tile component considering a value defined via "-reduce" parameter
    this->tcomp[c].set_pos0(cr->get_pos0());
    this->tcomp[c].set_pos1(cr->get_pos1());
    element_siz tc0 = this->tcomp[c].get_pos0();
    element_siz tc1 = this->tcomp[c].get_pos1();

    // copy samples in resolution buffer to that in tile component buffer
    unsigned long num_samples = (tc1.x - tc0.x) * (tc1.y - tc0.y);
#pragma omp parallel for  // default(none) shared(num_samples, sp, dp)
#if defined(__AVX2__)
    for (uint32_t n = 0; n < round_down(num_samples, SIMD_LEN_I32); n += SIMD_LEN_I32) {
      __m128i src = _mm_loadu_si128((__m128i *)(sp + n));
      __m256i dst = _mm256_cvtepi16_epi32(src);
      _mm256_store_si256((__m256i *)(dp + n), dst);
    }
    for (unsigned long n = round_down(num_samples, SIMD_LEN_I32); n < num_samples; ++n) {
      dp[n] = sp[n];
    }
#else
    for (unsigned long n = 0; n < num_samples; ++n) {
      dp[n] = sp[n];
    }
#endif

  }  // end of component loop
}
void j2k_tile::read_packet(j2k_precinct *current_precint, uint16_t layer, uint8_t num_band) {
  uint16_t Nsop;
  uint16_t Lsop;
  if (use_SOP) {
    uint16_t word = this->tile_buf->get_word();
    if (word != _SOP) {
      printf("ERROR: Expected SOP marker but %04X is found\n", word);
      exit(EXIT_FAILURE);
    }
    Lsop = this->tile_buf->get_word();
    if (Lsop != 4) {
      printf("ERROR: illegal Lsop value %d is found\n", Lsop);
      exit(EXIT_FAILURE);
    }
    Nsop = this->tile_buf->get_word();
  } else {
    Nsop = 0;
  }
  uint8_t bit = this->packet_header->get_bit();
  if (bit == 0) {                       // empty packet
    this->packet_header->flush_bits();  // emit_dword packet header
    if (use_EPH) {
      uint16_t word = this->packet_header->get_word();
      if (word != _EPH) {
        printf("ERROR: Expected EPH marker but %04X is found\n", word);
        exit(EXIT_FAILURE);
      }
    }
    return;
  }
  j2k_precinct_subband *cpb;
  // uint32_t num_bytes;
  for (uint8_t b = 0; b < num_band; b++) {
    cpb = current_precint->access_pband(b);  //&current_precint->pband[b];
    cpb->parse_packet_header(this->packet_header, layer, this->Ccap15);
  }
  // if the last byte of a packet header is 0xFF, one bit shall be read.
  this->packet_header->check_last_FF();
  this->packet_header->flush_bits();
  // check EPH
  if (use_EPH) {
    uint16_t word = this->packet_header->get_word();
    if (word != _EPH) {
      printf("ERROR: Expected EPH marker but %04X is found\n", word);
      exit(EXIT_FAILURE);
    }
  }

  j2k_codeblock *block;
  uint16_t buf_limit = 8192;
  for (uint8_t b = 0; b < num_band; b++) {
    cpb                      = current_precint->access_pband(b);  //&current_precint->pband[b];
    const uint32_t num_cblks = cpb->num_codeblock_x * cpb->num_codeblock_y;
    if (num_cblks != 0) {
      for (uint32_t block_index = 0; block_index < num_cblks; block_index++) {
        block = cpb->access_codeblock(block_index);
        block->create_compressed_buffer(this->tile_buf.get(), buf_limit, layer);
      }
    }
  }
}

void j2k_tile::find_gcd_of_precinct_size(element_siz &out) {
  element_siz PP;
  uint8_t PPx = 16, PPy = 16;
  for (uint16_t c = 0; c < num_components; c++) {
    for (uint8_t r = 0; r <= this->tcomp[c].get_dwt_levels(); r++) {
      PP  = this->tcomp[c].get_precinct_size(r);
      PPx = (PPx > PP.x) ? PP.x : PPx;
      PPy = (PPy > PP.y) ? PP.y : PPx;
    }
  }
  out.x = PPx;
  out.y = PPy;
}

void j2k_tile::ycbcr_to_rgb(j2k_main_header &main_header) {
  if (num_components != 3) {
    return;
  }
  uint8_t transformation;
  transformation = this->tcomp[0].get_transformation();
  assert(transformation == this->tcomp[1].get_transformation());
  assert(transformation == this->tcomp[2].get_transformation());

  element_siz tc0         = this->tcomp[0].get_pos0();
  element_siz tc1         = this->tcomp[0].get_pos1();
  uint32_t num_tc_samples = (tc1.x - tc0.x) * (tc1.y - tc0.y);

  int32_t *sp0 = this->tcomp[0].get_sample_address(0, 0);  // samples;
  int32_t *sp1 = this->tcomp[1].get_sample_address(0, 0);  // samples;
  int32_t *sp2 = this->tcomp[2].get_sample_address(0, 0);  // samples;
  if (MCT) {
    cvt_ycbcr_to_rgb[transformation](sp0, sp1, sp2, num_tc_samples);
  }
}

void j2k_tile::finalize(j2k_main_header &hdr) {
  //#pragma omp parallel for
  for (uint16_t c = 0; c < this->num_components; ++c) {
    int32_t *sp             = tcomp[c].get_sample_address(0, 0);
    const int32_t DC_OFFSET = (hdr.SIZ->is_signed(c)) ? 0 : 1 << (tcomp[c].bitdepth - 1);
    const int32_t MAXVAL =
        (hdr.SIZ->is_signed(c)) ? (1 << (tcomp[c].bitdepth - 1)) - 1 : (1 << tcomp[c].bitdepth) - 1;
    const int32_t MINVAL          = (hdr.SIZ->is_signed(c)) ? -(1 << (tcomp[c].bitdepth - 1)) : 0;
    const element_siz tc0         = this->tcomp[c].get_pos0();
    const element_siz tc1         = this->tcomp[c].get_pos1();
    const uint32_t num_tc_samples = (tc1.x - tc0.x) * (tc1.y - tc0.y);

    // downshift value for lossy path
    int16_t downshift = (tcomp[c].transformation) ? 0 : 13 - tcomp[c].bitdepth;
    if (downshift < 0) {
      printf("WARNING: sample precision over 13 bit/pixel is not supported.\n");
    }
    int16_t offset = (1 << downshift) >> 1;
    for (uint32_t n = 0; n < num_tc_samples; ++n) {
      sp[n] = (sp[n] + offset) >> downshift;
      sp[n] += DC_OFFSET;
      sp[n] = (sp[n] > MAXVAL) ? MAXVAL : sp[n];
      sp[n] = (sp[n] < MINVAL) ? MINVAL : sp[n];
    }
  }
}

void j2k_tile::enc_init(uint16_t idx, j2k_main_header &main_header, std::vector<int32_t *> img) {
  if (img.empty()) {
    printf("ERROR: input image is empty.\n");
    exit(EXIT_FAILURE);
  }
  index          = idx;
  num_components = main_header.SIZ->get_num_components();
  // set coding style related properties from main header
  setCODparams(main_header.COD.get());
  // set quantization style related properties from main header
  setQCDparams(main_header.QCD.get());
  // set Ccap15(HTJ2K only or mixed)
  Ccap15 = (main_header.CAP != nullptr) ? main_header.CAP->get_Ccap(15) : 0;
  // create tile-part(s)
  this->tile_part.push_back(std::make_unique<j2k_tile_part>(num_components));
  this->num_tile_part++;
  this->current_tile_part_pos++;
  SOT_marker tmpSOT;
  tmpSOT.set_SOT_marker(index, 0, 1);  // only one tile-part is supported
  this->tile_part[current_tile_part_pos]->set_SOT(tmpSOT);
  j2k_tilepart_header *tphdr = this->tile_part[current_tile_part_pos]->header.get();

  element_siz numTiles;
  element_siz Siz, Osiz, Tsiz, TOsiz;
  main_header.get_number_of_tiles(numTiles.x, numTiles.y);
  uint16_t p = this->index % numTiles.x;
  uint16_t q = this->index / numTiles.x;
  main_header.SIZ->get_image_size(Siz);
  main_header.SIZ->get_image_origin(Osiz);
  main_header.SIZ->get_tile_size(Tsiz);
  main_header.SIZ->get_tile_origin(TOsiz);

  this->pos0.x = std::max(TOsiz.x + p * Tsiz.x, Osiz.x);
  this->pos0.y = std::max(TOsiz.y + q * Tsiz.y, Osiz.y);
  this->pos1.x = std::min(TOsiz.x + (p + 1) * Tsiz.x, Siz.x);
  this->pos1.y = std::min(TOsiz.y + (q + 1) * Tsiz.y, Siz.y);

  // set coding style related properties from tile-part header
  if (tphdr->COD != nullptr) {
    setCODparams(tphdr->COD.get());
  }
  // set quantization style related properties from tile-part header
  if (tphdr->QCD != nullptr) {
    setQCDparams(tphdr->QCD.get());
  }

  // create tile components
  this->tcomp = std::make_unique<j2k_tile_component[]>(num_components);
#pragma omp parallel for  // default(none) shared(img, tphdr, main_header)
  for (uint16_t c = 0; c < num_components; c++) {
    this->tcomp[c].init(&main_header, tphdr, this, c, img);
    this->tcomp[c].create_resolutions(1);  // number of layers = 1
  }

  // apply POC, if any
  if (tphdr->POC != nullptr) {
    for (unsigned long i = 0; i < tphdr->POC->nPOC; ++i) {
      porder_info.add(tphdr->POC->RSpoc[i], tphdr->POC->CSpoc[i], tphdr->POC->LYEpoc[i],
                      tphdr->POC->REpoc[i], tphdr->POC->CEpoc[i], tphdr->POC->Ppoc[i]);
    }
  } else if (main_header.POC != nullptr) {
    for (unsigned long i = 0; i < main_header.POC->nPOC; ++i) {
      porder_info.add(main_header.POC->RSpoc[i], main_header.POC->CSpoc[i], main_header.POC->LYEpoc[i],
                      main_header.POC->REpoc[i], main_header.POC->CEpoc[i], main_header.POC->Ppoc[i]);
    }
  }
}

int j2k_tile::perform_dc_offset(j2k_main_header &hdr) {
  int done = 0;
  for (int c = 0; c < this->num_components; ++c) {
    this->tcomp[c].perform_dc_offset(this->transformation, hdr.SIZ->is_signed(c));
    done += 1;
  }
  return done;
}

void j2k_tile::rgb_to_ycbcr(j2k_main_header &main_header) {
  if (num_components != 3) {
    return;
  }
  const uint8_t transformation = this->tcomp[0].get_transformation();
  assert(transformation == this->tcomp[1].get_transformation());
  assert(transformation == this->tcomp[2].get_transformation());

  const element_siz tc0         = this->tcomp[0].get_pos0();
  const element_siz tc1         = this->tcomp[0].get_pos1();
  const uint32_t num_tc_samples = (tc1.x - tc0.x) * (tc1.y - tc0.y);

  int32_t *const sp0 = this->tcomp[0].get_sample_address(0, 0);  // samples;
  int32_t *const sp1 = this->tcomp[1].get_sample_address(0, 0);  // samples;
  int32_t *const sp2 = this->tcomp[2].get_sample_address(0, 0);  // samples;
  if (MCT) {
    cvt_rgb_to_ycbcr[transformation](sp0, sp1, sp2, num_tc_samples);
  }
}

uint8_t *j2k_tile::encode(j2k_main_header &main_header) {
#pragma omp parallel for  // default(none)
  for (uint16_t c = 0; c < num_components; c++) {
    const uint8_t ROIshift       = this->tcomp[c].get_ROIshift();
    const uint8_t NL             = this->tcomp[c].get_dwt_levels();
    const uint8_t transformation = this->tcomp[c].get_transformation();
    element_siz top_left         = this->tcomp[c].get_pos0();
    element_siz bottom_right     = this->tcomp[c].get_pos1();
    j2k_resolution *cr           = this->tcomp[c].access_resolution(NL);

    int32_t *const sp0            = this->tcomp[c].get_sample_address(0, 0);
    const uint32_t num_tc_samples = (bottom_right.x - top_left.x) * (bottom_right.y - top_left.y);
    // TODO: enc_init vectorize code
#if defined(__AVX2__)
    __m256i offsets = _mm256_set_epi32(7, 6, 3, 2, 5, 4, 1, 0);
    for (uint32_t n = 0; n < round_down(num_tc_samples, 16); n += 16) {
      __m256i s0a = _mm256_loadu_si256((__m256i *)(sp0 + n));
      __m256i s0b = _mm256_loadu_si256((__m256i *)(sp0 + n + 8));
      s0a         = _mm256_permutevar8x32_epi32(_mm256_packs_epi32(s0a, s0b), offsets);
      _mm256_storeu_si256((__m256i *)(cr->i_samples + n), s0a);
    }
    for (uint32_t n = round_down(num_tc_samples, 16); n < num_tc_samples; ++n) {
      cr->i_samples[n] = static_cast<int16_t>(sp0[n]);
    }
#else
    for (uint32_t n = 0; n < num_tc_samples; ++n) {
      cr->i_samples[n] = static_cast<int16_t>(sp0[n]);
    }
#endif
    //    // experimental floating point code
    //    if (transformation == 0) {
    //      for (uint32_t n = 0; n < (bottom_right.x - top_left.x) * (bottom_right.y - top_left.y); ++n) {
    //        cr->f_samples[n] = static_cast<float>(cr->i_samples[n]);
    //      }
    //    }
    auto t1_encode = [](uint16_t numlayers_local, bool use_EPH_local, j2k_resolution *cr,
                        uint8_t ROIshift) {
      int32_t length = 0;
      for (uint32_t p = 0; p < cr->npw * cr->nph; ++p) {
        int32_t packet_length = 0;
        j2k_precinct *cp      = cr->access_precinct(p);
        packet_header_writer pckt_hdr;
        for (uint8_t b = 0; b < cr->num_bands; ++b) {
          j2k_precinct_subband *cpb = cp->access_pband(b);
          const uint32_t num_cblks  = cpb->num_codeblock_x * cpb->num_codeblock_y;
          //#pragma omp parallel for reduction(+ : packet_length)
          for (uint32_t block_index = 0; block_index < num_cblks; ++block_index) {
            j2k_codeblock *block = cpb->access_codeblock(block_index);
            packet_length += htj2k_encode(block, ROIshift);
          }
          // construct packet header
          cpb->generate_packet_header(pckt_hdr, numlayers_local - 1);
        }
        // emit_dword packet header
        pckt_hdr.flush(use_EPH_local);
        cp->packet_header_length = pckt_hdr.get_length();
        cp->packet_header        = std::make_unique<uint8_t[]>(cp->packet_header_length);

        pckt_hdr.copy_buf(cp->packet_header.get());
        packet_length += pckt_hdr.get_length();
        cp->set_length(packet_length);
        length += packet_length;
      }
      return length;
    };
    for (int8_t r = NL; r > 0; --r) {
      j2k_resolution *ncr = this->tcomp[c].access_resolution(r - 1);
      const uint32_t u0   = top_left.x;
      const uint32_t u1   = bottom_right.x;
      const uint32_t v0   = top_left.y;
      const uint32_t v1   = bottom_right.y;
      j2k_subband *HL     = cr->access_subband(0);
      j2k_subband *LH     = cr->access_subband(1);
      j2k_subband *HH     = cr->access_subband(2);

      if (u1 != u0 && v1 != v0) {
        // if (transformation) {
        fdwt_2d_sr_fixed(cr->i_samples, ncr->i_samples, HL->i_samples, LH->i_samples, HH->i_samples, u0, u1,
                         v0, v1, transformation);
        //        fdwt_2d_sr_float(cr->f_samples, ncr, HL, LH, HH, u0, u1, v0, v1, transformation);
        ncr->scale();
        HL->quantize();
        LH->quantize();
        HH->quantize();
        //        HL->quantize_float();
        //        LH->quantize_float();
        //        HH->quantize_float();
      }
      // encode codeblocks in HL or LH or HH
      this->length += t1_encode(this->numlayers, this->use_EPH, cr, ROIshift);

      cr           = this->tcomp[c].access_resolution(r - 1);
      top_left     = cr->get_pos0();
      bottom_right = cr->get_pos1();
    }

    // encode codeblocks in LL
    j2k_subband *LL = cr->access_subband(0);
    LL->quantize();
    //    LL->quantize_float();
    this->length += t1_encode(this->numlayers, this->use_EPH, cr, ROIshift);
  }  // end of component loop
  this->tile_part[0]->set_tile_index(this->index);
  this->tile_part[0]->set_tile_part_index(0);  // currently ony a single tile-part is supported
  // length of tile-part will be written in j2k_tile::write_packets()
  // this->tile_part[0]->header->SOT.set_tile_part_length(this->length);
  return nullptr;  // fake
}

j2k_tile_component *j2k_tile::get_tile_component(uint16_t c) { return &this->tcomp[c]; }

uint8_t j2k_tile::get_byte_from_tile_buf() { return this->tile_buf->get_byte(); }

uint8_t j2k_tile::get_bit_from_tile_buf() { return this->tile_buf->get_bit(); }

uint32_t j2k_tile::get_length() const { return length; }

uint32_t j2k_tile::get_buf_length() { return tile_buf->get_total_length(); }
