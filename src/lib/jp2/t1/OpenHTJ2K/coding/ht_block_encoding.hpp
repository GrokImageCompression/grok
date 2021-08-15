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

#if defined(_MSC_VER) || defined(__MINGW64__)
  #include <intrin.h>
#elif defined(__x86_64__)
  #include <x86intrin.h>
#endif

#define MAX_Lcup 16834
#define MAX_Scup 4079

/********************************************************************************
 * state_MS_enc: state class for MagSgn encoding
 *******************************************************************************/
class state_MS_enc {
 private:
  int32_t pos;         // current position in the buffer
  uint8_t *const buf;  // buffer for MagSgn
#ifdef MSNAIVE
  uint8_t bits;
  uint8_t max;
  uint8_t tmp;
#else
  uint64_t Creg;      // temporal buffer to store up to 4 codewords
  uint32_t ctreg;     // number of used bits in Creg
  uint8_t last;       // last byte in the buffer
  void emit_dword();  // internal function to emit 4 code words
#endif

 public:
  explicit state_MS_enc(uint8_t *p)
      : pos(0),
        buf(p),
#ifdef MSNAIVE
        bits(0),
        max(8),
        tmp(0)
#else
        Creg(0),
        ctreg(0),
        last(0)
#endif
  {
  }
#ifdef MSNAIVE
  void emitMagSgnBits(uint32_t cwd, uint8_t m_n);
#else
  void emitMagSgnBits(uint32_t cwd, uint8_t m_n, uint8_t emb_1);
#endif
  int32_t termMS();
};

class state_MEL_enc;  // forward declaration for friend function "termMELandVLC()"
/********************************************************************************
 * state_VLC_enc: state classe for VLC encoding
 *******************************************************************************/
class state_VLC_enc {
 private:
  int32_t pos;
  uint8_t bits;
  uint8_t tmp;
  uint8_t last;
  uint8_t *const buf;

  friend int32_t termMELandVLC(state_VLC_enc &, state_MEL_enc &);

 public:
  explicit state_VLC_enc(uint8_t *p) : pos(MAX_Scup - 2), bits(4), tmp(0xF), last(0xFF), buf(p) {
    buf[pos + 1] = 0xFF;
  }
  void emitVLCBits(uint16_t cwd, uint8_t len);
};

/********************************************************************************
 * state_MEL_enc: state class for MEL encoding
 *******************************************************************************/
class state_MEL_enc {
 private:
  int8_t MEL_k;
  uint8_t MEL_run;
  const uint8_t MEL_E[13];
  uint8_t MEL_t;
  int32_t pos;
  uint8_t rem;
  uint8_t tmp;
  uint8_t *const buf;
  void emitMELbit(uint8_t bit);

  friend int32_t termMELandVLC(state_VLC_enc &, state_MEL_enc &);

 public:
  explicit state_MEL_enc(uint8_t *p)
      : MEL_k(0),
        MEL_run(0),
        MEL_E{0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 4, 5},
        MEL_t(1 << MEL_E[MEL_k]),
        pos(0),
        rem(8),
        tmp(0),
        buf(p) {}
  void encodeMEL(uint8_t smel);
  void termMEL();
};
