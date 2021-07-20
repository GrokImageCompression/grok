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

#include "mq_decoder.hpp"

#include <cstdio>
#include <cstdlib>

mq_decoder::mq_decoder(const uint8_t *const buf)
    : A(0),
      t(0),
      C(0),
      T(0),
      L(0),
      L_start(0),
      Lmax(0),
      byte_buffer(buf),
      dynamic_table{{}},
      static_table{
          {1,  2,  3,  4,  5,  38, 7,  8,  9,  10, 11, 12, 13, 29, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
           25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 45, 46},
          {1,  6,  9,  12, 29, 33, 6,  14, 14, 14, 17, 18, 20, 21, 14, 14, 15, 16, 17, 18, 19, 19, 20, 21,
           22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 46},
          {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
          {0x5601, 0x3401, 0x1801, 0x0AC1, 0x0521, 0x0221, 0x5601, 0x5401, 0x4801, 0x3801, 0x3001, 0x2401,
           0x1C01, 0x1601, 0x5601, 0x5401, 0x5101, 0x4801, 0x3801, 0x3401, 0x3001, 0x2801, 0x2401, 0x2201,
           0x1C01, 0x1801, 0x1601, 0x1401, 0x1201, 0x1101, 0x0AC1, 0x09C1, 0x08A1, 0x0521, 0x0441, 0x02A1,
           0x0221, 0x0141, 0x0111, 0x0085, 0x0049, 0x0025, 0x0015, 0x0009, 0x0005, 0x0001, 0x5601}} {}

void mq_decoder::init(uint32_t buf_pos, uint32_t segment_length, bool is_bypass) {
  L_start = buf_pos;
  Lmax    = buf_pos + segment_length;
  L       = buf_pos;  // this means L points the beginning of a codeword segment (L=0)
  T       = 0;
  if (is_bypass) {
    t = 0;
  } else {
    A = 0x8000;
    C = 0;
    fill_LSBs();
    C <<= t;
    fill_LSBs();
    C <<= 7;
    t -= 7;
  }
}

void mq_decoder::init_states_for_all_contexts() {
  for (uint8_t i = 0; i < 19; i++) {
    dynamic_table[0][i] = 0;
    dynamic_table[1][i] = 0;
  }
  dynamic_table[0][0]  = 4;
  dynamic_table[0][17] = 3;
  dynamic_table[0][18] = 46;
}

void mq_decoder::renormalize_once() {
  if (t == 0) {
    fill_LSBs();
  }
  A <<= 1;
  C <<= 1;
  t--;
}

void mq_decoder::fill_LSBs() {
  t = 8;
  if (L == Lmax || (T == 0xFF && byte_buffer[L] > 0x8F)) {
    // Codeword exhausted; fill C with 1's from now on
    C += 0xFF;
  } else {
    if (T == 0xFF) {
      t = 7;
    }
    T = byte_buffer[L];
    L++;
    C += (T << (8 - t));
  }
}

uint8_t mq_decoder::decode(uint8_t label) {
  uint8_t symbol;
  uint16_t Temp;
  uint8_t min_C_active     = 9 - 1;
  uint32_t C_active_mask   = 0xFFFF00;
  uint16_t expected_symbol = dynamic_table[1][label];
  uint16_t probability     = static_table[3][dynamic_table[0][label]];

  if (expected_symbol > 1) {
    printf("ERROR: mq_dec error in function decode()\n");
    exit(EXIT_FAILURE);
  }

  A -= probability;
  if (A < probability) {
    // Conditional exchange of MPS and LPS
    expected_symbol = 1 - expected_symbol;
  }

  // Compare active region of C
  if (((C & C_active_mask) >> 8) < probability) {
    symbol = 1 - expected_symbol;
    A      = probability;
  } else {
    symbol = expected_symbol;
    Temp   = ((C & C_active_mask) >> min_C_active) - static_cast<uint32_t>(probability);
    C &= ~C_active_mask;
    C += static_cast<uint32_t>((Temp << min_C_active)) & C_active_mask;
  }
  if (A < 0x8000) {
    if (symbol == dynamic_table[1][label]) {
      // The symbol was a real MPS
      dynamic_table[0][label] = static_table[0][dynamic_table[0][label]];
    } else {
      // The symbol was a real LPS
      dynamic_table[1][label] = dynamic_table[1][label] ^ static_table[2][dynamic_table[0][label]];
      dynamic_table[0][label] = static_table[1][dynamic_table[0][label]];
    }
  }

  while (A < 0x8000) {
    renormalize_once();
  }
  return symbol;
}

uint8_t mq_decoder::get_raw_symbol() {
  if (t == 0) {
    t = 8;
    if (L == Lmax) {
      T = 0xFF;
    } else {
      if (T == 0xFF) {
        t = 7;
      }
      T = byte_buffer[L];
      L++;
    }
  }
  t--;
  return ((T >> t) & 1);
}

void mq_decoder::finish() {
  // TODO: ERTERM
}
