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
#include <algorithm>  // for max{a,b,c,d}

const int32_t bitmask32[32] = {
    0x00000000, 0x00000001, 0x00000003, 0x00000007, 0x0000000F, 0x0000001F, 0x0000003F, 0x0000007F,
    0x000000FF, 0x000001FF, 0x000003FF, 0x000007FF, 0x00000FFF, 0x00001FFF, 0x00003FFF, 0x00007FFF,
    0x0000FFFF, 0x0001FFFF, 0x0003FFFF, 0x0007FFFF, 0x000FFFFF, 0x001FFFFF, 0x003FFFFF, 0x007FFFFF,
    0x00FFFFFF, 0x01FFFFFF, 0x03FFFFFF, 0x07FFFFFF, 0x0FFFFFFF, 0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF};

/********************************************************************************
 * state_MS: state class for MagSgn decoding
 *******************************************************************************/
class state_MS_dec {
 private:
  uint32_t pos;
  uint8_t bits;
  uint8_t tmp;
  uint8_t last;
  const uint8_t *buf;
  const uint32_t length;
  uint64_t Creg;
  uint8_t ctreg;

 public:
  state_MS_dec(const uint8_t *Dcup, uint32_t Pcup)
      : pos(0), bits(0), tmp(0), last(0), buf(Dcup), length(Pcup), Creg(0), ctreg(0) {
    while (ctreg < 32) {
      loadByte();
    }
  }
  void loadByte();
  void close(int32_t num_bits);
  uint8_t importMagSgnBit();
  int32_t decodeMagSgnValue(int32_t m_n, int32_t i_n);
};

/********************************************************************************
 * state_MEL_unPacker and state_MEL: state classes for MEL decoding
 *******************************************************************************/
class state_MEL_unPacker {
 private:
  uint32_t pos;
  int8_t bits;
  uint8_t tmp;
  const uint8_t *buf;
  uint32_t length;

 public:
  state_MEL_unPacker(const uint8_t *Dcup, uint32_t Lcup, uint32_t Pcup)
      : pos(Pcup), bits(0), tmp(0), buf(Dcup), length(Lcup) {}
  uint8_t impoertMELbit();
};

class state_MEL_decoder {
 private:
  uint8_t MEL_k;
  uint8_t MEL_run;
  uint8_t MEL_one;
  const uint8_t MEL_E[13];
  state_MEL_unPacker *MEL_unPacker;

 public:
  explicit state_MEL_decoder(state_MEL_unPacker &unpacker)
      : MEL_k(0),
        MEL_run(0),
        MEL_one(0),
        MEL_E{0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 4, 5},
        MEL_unPacker(&unpacker) {}
  uint8_t decodeMELSym();
};

#define ADVANCED
#ifdef ADVANCED
  #define getbitfunc getVLCbit()
#else
  #define getbitfunc importVLCBit()
#endif
/********************************************************************************
 * state_VLC: state classe for VLC decoding
 *******************************************************************************/
class state_VLC_enc {
 private:
  int32_t pos;
  uint8_t last;
#ifndef ADVANCED
  uint8_t tmp;
  uint32_t rev_length;
#else
  int32_t ctreg;
  uint64_t Creg;
#endif
  uint8_t bits;
  uint8_t *buf;

 public:
  state_VLC_enc(uint8_t *Dcup, uint32_t Lcup, uint32_t Pcup)
#ifndef ADVANCED
      : pos((Lcup > 2) ? Lcup - 3 : 0),
        last(*(Dcup + Lcup - 2)),
        tmp(last >> 4),
        rev_length(Pcup),
        bits(((tmp & 0x07) < 7) ? 4 : 3),
        buf(Dcup) {
  }
  uint8_t importVLCBit();
#else
      : pos(Lcup - 2 - Pcup), ctreg(0), Creg(0), bits(0), buf(Dcup + Pcup) {
    load_bytes();
    ctreg -= 4;
    Creg >>= 4;
    while (ctreg < 32) {
      load_bytes();
    }
  }
  void load_bytes();
  uint8_t getVLCbit();
  void close32(int32_t num_bits);
#endif
  void decodeCxtVLC(const uint16_t &context, uint8_t (&u_off)[2], uint8_t (&rho)[2], uint8_t (&emb_k)[2],
                    uint8_t (&emb_1)[2], const uint8_t &first_or_second, const uint16_t *dec_CxtVLC_table);
  uint8_t decodeUPrefix();
  uint8_t decodeUSuffix(const uint8_t &u_pfx);
  uint8_t decodeUExtension(const uint8_t &u_sfx);
};
/********************************************************************************
 * SP_dec: state classe for HT SigProp decoding
 *******************************************************************************/
class SP_dec {
 private:
  const uint32_t Lref;
  uint8_t bits;
  uint8_t tmp;
  uint8_t last;
  uint32_t pos;
  const uint8_t *Dref;

 public:
  SP_dec(const uint8_t *HT_magref_segment, uint32_t magref_length)
      : Lref(magref_length),
        bits(0),
        tmp(0),
        last(0),
        pos(0),
        Dref((Lref == 0) ? nullptr : HT_magref_segment) {}
  uint8_t importSigPropBit();
};

/********************************************************************************
 * MR_dec: state classe for HT MagRef decoding
 *******************************************************************************/
class MR_dec {
 private:
  const uint32_t Lref;
  uint8_t bits;
  uint8_t last;
  uint8_t tmp;
  int32_t pos;
  const uint8_t *Dref;

 public:
  MR_dec(const uint8_t *HT_magref_segment, uint32_t magref_length)
      : Lref(magref_length),
        bits(0),
        last(0xFF),
        tmp(0),
        pos((Lref == 0) ? -1 : magref_length - 1),
        Dref((Lref == 0) ? nullptr : HT_magref_segment) {}
  uint8_t importMagRefBit();
};
