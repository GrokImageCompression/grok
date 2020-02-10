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
// File: ojph_block_decoder.cpp
// Author: Aous Naman
// Date: 28 August 2019
//***************************************************************************/


#include <cassert>
#include <cstring>
#include "ojph_block_decoder.h"
#include "ojph_arch.h"
#include "ojph_message.h"

namespace ojph {
  namespace local {

    /////////////////////////////////////////////////////////////////////////
    // tables
    /////////////////////////////////////////////////////////////////////////

    //VLC
    // index: 7 bits for codeword + 3 bits for context
    // table 0 is for the initial line of quads
    static ui16 vlc_tbl0[1024] = { 0 };
    static ui16 vlc_tbl1[1024] = { 0 };

    /////////////////////////////////////////////////////////////////////////
    //
    /////////////////////////////////////////////////////////////////////////
    struct mel_struct {
      //storage
      ui8* data; //pointer to where to read data
      ui64 tmp;  //temporary buffer of read data
      int bits;  //number of bits stored in tmp
      int size;
      bool unstuff;  //true if the next bit needs to be unstuffed
                     //state if mel decoder
      int k;     //state

      //queue of decoded runs
      int num_runs;
      ui64 runs;
    };

    /////////////////////////////////////////////////////////////////////////
    static inline
    void mel_read(mel_struct *melp)
    {
      if (melp->bits > 32)
        return;
      ui32 val;
      val = *(ui32*)melp->data;

      int bits = 32 - melp->unstuff;

      ui32 t = (melp->size > 0) ? (val & 0xFF) : 0xFF;
      if (melp->size == 1) t |= 0xF;
      melp->data += melp->size-- > 0;
      bool unstuff = ((val & 0xFF) == 0xFF);

      bits -= unstuff;
      t = t << (8 - unstuff);

      t |= (melp->size > 0) ? ((val>>8) & 0xFF) : 0xFF;
      if (melp->size == 1) t |= 0xF;
      melp->data += melp->size-- > 0;
      unstuff = (((val >> 8) & 0xFF) == 0xFF);

      bits -= unstuff;
      t = t << (8 - unstuff);

      t |= (melp->size > 0) ? ((val>>16) & 0xFF) : 0xFF;
      if (melp->size == 1) t |= 0xF;
      melp->data += melp->size-- > 0;
      unstuff = (((val >> 16) & 0xFF) == 0xFF);

      bits -= unstuff;
      t = t << (8 - unstuff);

      t |= (melp->size > 0) ? ((val>>24) & 0xFF) : 0xFF;
      if (melp->size == 1) t |= 0xF;
      melp->data += melp->size-- > 0;
      melp->unstuff = (((val >> 24) & 0xFF) == 0xFF);

      melp->tmp |= ((ui64)t) << (64 - bits - melp->bits);
      melp->bits += bits;
    }

    /////////////////////////////////////////////////////////////////////////
    static inline
    void mel_decode(mel_struct *melp)
    {
      static const int mel_exp[13] = { //MEL exponent
        0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 4, 5
      };

      if (melp->bits < 6)
        mel_read(melp);

      while (melp->bits >= 6 && melp->num_runs < 8)
      {
        int eval = mel_exp[melp->k];
        int run = 0;
        if (melp->tmp & (1ull<<63)) //MSB is set
        { //one is found
          run = 1 << eval;
          run--;
          melp->k = melp->k + 1 < 12 ? melp->k + 1 : 12;
          melp->tmp <<= 1;
          melp->bits -= 1;
          run = run << 1; //not terminating in one
        }
        else
        { //0 is found
          run = (melp->tmp >> (63 - eval)) & ((1 << eval) - 1);
          //run = bit_reverse[run] >> (5 - eval);
          melp->k = melp->k - 1 > 0 ? melp->k - 1 : 0;
          melp->tmp <<= eval + 1;
          melp->bits -= eval + 1;
          run = (run << 1) + 1; //terminating with one
        }
        eval = melp->num_runs * 7;
        melp->runs &= ~((ui64)0x3F << eval);
        melp->runs |= ((ui64)run) << eval;
        melp->num_runs++; //increment count
      }
    }

    /////////////////////////////////////////////////////////////////////////
    static inline
    void mel_init(mel_struct *melp, ui8*bbuf, int lcup, int scup)
    {
      melp->data = bbuf + lcup - scup;
      melp->bits = 0;
      melp->tmp = 0;
      melp->unstuff = false;
      melp->size = scup - 1;
      melp->k = 0;
      melp->num_runs = 0;
      melp->runs = 0;

      //These few lines take care of the case where data is not at a multiple
      // of 4 boundary.  It reads at 1,2,3 up to 4 bytes from the mel stream
      int num = 4 - (intptr_t(melp->data) & 0x3);
      for (int i = 0; i < num; ++i) {
        assert(melp->unstuff == false || melp->data[0] <= 0x8F);
        ui64 d = (melp->size > 0) ? *melp->data : 0xFF;
        if (melp->size == 1) d |= 0xF;
        melp->data += melp->size-- > 0;
        int d_bits = 8 - melp->unstuff;
        melp->tmp = (melp->tmp << d_bits) | d;
        melp->bits += d_bits;
        melp->unstuff = ((d & 0xFF) == 0xFF);
      }
      melp->tmp <<= (64 - melp->bits); //push up
    }

    /////////////////////////////////////////////////////////////////////////
    static inline
    int mel_get_run(mel_struct *melp)
    {
      if (melp->num_runs == 0)
        mel_decode(melp);

      int t = melp->runs & 0x7F;
      melp->runs >>= 7;
      melp->num_runs--;
      return t;
    }

    /////////////////////////////////////////////////////////////////////////
    //
    /////////////////////////////////////////////////////////////////////////
    struct rev_struct {
      //storage
      ui8* data;     //pointer to where to read data
      ui64 tmp;		   //temporary buffer of read data
      int bits;      //number of bits stored in tmp
      int size;
      bool unstuff;  //true if a bit needs to be unstuffed
    };

    /////////////////////////////////////////////////////////////////////////
    inline void rev_read(rev_struct *vlcp)
    {
      //process 4 bytes at a time
      if (vlcp->bits > 32)
        return;
      ui32 val;
      val = *(ui32*)vlcp->data;
      vlcp->data -= 4;

      //accumulate in int and then push into the registers
      ui32 tmp = val >> 24;
      int bits;
      bits = 8 - ((vlcp->unstuff && (((val >> 24) & 0x7F) == 0x7F)) ? 1 : 0);
      bool unstuff = (val >> 24) > 0x8F;

      tmp |= ((val >> 16) & 0xFF) << bits;
      bits += 8 - ((unstuff && (((val >> 16) & 0x7F) == 0x7F)) ? 1 : 0);
      unstuff = ((val >> 16) & 0xFF) > 0x8F;

      tmp |= ((val >> 8) & 0xFF) << bits;
      bits += 8 - ((unstuff && (((val >> 8) & 0x7F) == 0x7F)) ? 1 : 0);
      unstuff = ((val >> 8) & 0xFF) > 0x8F;

      tmp |= (val & 0xFF) << bits;
      bits += 8 - ((unstuff && ((val & 0x7F) == 0x7F)) ? 1 : 0);
      unstuff = (val & 0xFF) > 0x8F;

      vlcp->tmp |= (ui64)tmp << vlcp->bits;
      vlcp->bits += bits;
      vlcp->unstuff = unstuff;

      vlcp->size -= 4;
      //because we read ahead of time, we might in fact exceed vlc size,
      // but data should not be used if the codeblock is properly generated
      //The mel code can in fact occupy zero length, if it has a small number
      // of bits and these bits overlap with the VLC code
      if (vlcp->size < -8) //8 is based on the fact that we may read 64 bits
        OJPH_ERROR(0x00010001, "Error in reading VLC data");
    }

    /////////////////////////////////////////////////////////////////////////
    inline void rev_init(rev_struct *vlcp, ui8* data, int lcup, int scup)
    {
      //first byte has only the upper 4 bits
      vlcp->data = data + lcup - 2;

      //size can not be larger than this, in fact it should be smaller
      vlcp->size = scup - 2;

      int d = *vlcp->data--;
      vlcp->tmp = d >> 4; //both initialize and set
      vlcp->bits = 4 - ((vlcp->tmp & 7) == 7);
      vlcp->unstuff = (d | 0xF) > 0x8F;

      //These few lines take care of the case where data is not at a multiple
      // of 4 boundary.  It reads at 1,2,3 up to 4 bytes from the vlc stream
      int num = 1 + (intptr_t(vlcp->data) & 0x3);
      int tnum = num < vlcp->size ? num : vlcp->size;
      for (int i = 0; i < tnum; ++i) {
        ui64 d;
        d = *vlcp->data--;
        int d_bits = 8 - ((vlcp->unstuff && ((d & 0x7F) == 0x7F)) ? 1 : 0);
        vlcp->tmp |= d << vlcp->bits;
        vlcp->bits += d_bits;
        vlcp->unstuff = d > 0x8F;
      }
      vlcp->data -= 3; //make ready to read a 32 bits
      rev_read(vlcp);
    }

    /////////////////////////////////////////////////////////////////////////
    inline ui32 rev_fetch(rev_struct *vlcp)
    {
      if (vlcp->bits < 32)
      {
        rev_read(vlcp);
        if (vlcp->bits < 32)
          rev_read(vlcp);
      }
      return (ui32)vlcp->tmp;
    }

    /////////////////////////////////////////////////////////////////////////
    inline ui32 rev_advance(rev_struct *vlcp, int num_bits)
    {
      assert(num_bits <= vlcp->bits);
      vlcp->tmp >>= num_bits;
      vlcp->bits -= num_bits;
      return (ui32)vlcp->tmp;
    }

    /////////////////////////////////////////////////////////////////////////
    inline void rev_read_mrp(rev_struct *mrp)
    {
      //process 4 bytes at a time
      if (mrp->bits > 32)
        return;
      ui32 val;
      val = *(ui32*)mrp->data;
      mrp->data -= mrp->size > 0 ? 4 : 0;

      //accumulate in int and then push into the registers
      ui32 tmp = (mrp->size-- > 0) ? (val >> 24) : 0;
      int bits;
      bits = 8 - ((mrp->unstuff && (((val >> 24) & 0x7F) == 0x7F)) ? 1 : 0);
      bool unstuff = (val >> 24) > 0x8F;

      tmp |= (mrp->size-- > 0) ? (((val >> 16) & 0xFF) << bits) : 0;
      bits += 8 - ((unstuff && (((val >> 16) & 0x7F) == 0x7F)) ? 1 : 0);
      unstuff = ((val >> 16) & 0xFF) > 0x8F;

      tmp |= (mrp->size-- > 0) ? (((val >> 8) & 0xFF) << bits) : 0;
      bits += 8 - ((unstuff && (((val >> 8) & 0x7F) == 0x7F)) ? 1 : 0);
      unstuff = ((val >> 8) & 0xFF) > 0x8F;

      tmp |= (mrp->size-- > 0) ? ((val & 0xFF) << bits) : 0;
      bits += 8 - ((unstuff && ((val & 0x7F) == 0x7F)) ? 1 : 0);
      unstuff = (val & 0xFF) > 0x8F;

      mrp->tmp |= (ui64)tmp << mrp->bits;
      mrp->bits += bits;
      mrp->unstuff = unstuff;
    }

    /////////////////////////////////////////////////////////////////////////
    inline void rev_init_mrp(rev_struct *vlcp, ui8* data, int lcup, int scup)
    {
      //first byte has only the upper 4 bits
      vlcp->data = data + lcup + scup - 1;
      vlcp->size = scup;
      vlcp->unstuff = true;
      vlcp->bits = 0;
      vlcp->tmp = 0;

      //These few lines take care of the case where data is not at a multiple
      // of 4 boundary.  It reads at 1,2,3 up to 4 bytes from the mrp stream
      int num = 1 + (intptr_t(vlcp->data) & 0x3);
      for (int i = 0; i < num; ++i) {
        ui64 d;
        d = (vlcp->size-- > 0) ? *vlcp->data-- : 0;
        int d_bits = 8 - ((vlcp->unstuff && ((d & 0x7F) == 0x7F)) ? 1 : 0);
        vlcp->tmp |= d << vlcp->bits;
        vlcp->bits += d_bits;
        vlcp->unstuff = d > 0x8F;
      }
      vlcp->data -= 3; //make ready to read a 32 bits
      rev_read_mrp(vlcp);
    }

    /////////////////////////////////////////////////////////////////////////
    inline ui32 rev_fetch_mrp(rev_struct *vlcp)
    {
      if (vlcp->bits < 32)
      {
        rev_read_mrp(vlcp);
        if (vlcp->bits < 32)
          rev_read_mrp(vlcp);
      }
      return (ui32)vlcp->tmp;
    }

    /////////////////////////////////////////////////////////////////////////
    inline ui32 rev_advance_mrp(rev_struct *vlcp, int num_bits)
    {
      assert(num_bits <= vlcp->bits);
      vlcp->tmp >>= num_bits;
      vlcp->bits -= num_bits;
      return (ui32)vlcp->tmp;
    }

    /////////////////////////////////////////////////////////////////////////
    static bool vlc_init_tables()
    {
      const bool debug = false;

      struct vlc_src_table { int c_q, rho, u_off, e_k, e_1, cwd, cwd_len; };
      vlc_src_table tbl0[] = {
    #include "table0.h"
      };
      size_t tbl0_size = sizeof(tbl0) / sizeof(vlc_src_table);

      vlc_src_table tbl1[] = {
    #include "table1.h"
      };
      size_t tbl1_size = sizeof(tbl1) / sizeof(vlc_src_table);

      if (debug) memset(vlc_tbl0, 0, sizeof(vlc_tbl0)); //unnecessary
      for (int i = 0; i < 1024; ++i)
      {
        int cwd = i & 0x7F;
        int c_q = i >> 7;
        for (size_t j = 0; j < tbl0_size; ++j)
          if (tbl0[j].c_q == c_q) // this is an and operation
            if (tbl0[j].cwd == (cwd & ((1 << tbl0[j].cwd_len) - 1)))
            {
              if (debug) assert(vlc_tbl0[i] == 0);
              vlc_tbl0[i] = (tbl0[j].rho << 4) | (tbl0[j].u_off << 3)
                | (tbl0[j].e_k << 12) | (tbl0[j].e_1 << 8) | tbl0[j].cwd_len;
            }
      }
      if (debug) memset(vlc_tbl1, 0, sizeof(vlc_tbl1)); //unnecessary
      for (int i = 0; i < 1024; ++i)
      {
        int cwd = i & 0x7F; //7 bits
        int c_q = i >> 7;
        for (size_t j = 0; j < tbl1_size; ++j)
          if (tbl1[j].c_q == c_q) // this is an and operation
            if (tbl1[j].cwd == (cwd & ((1 << tbl1[j].cwd_len) - 1)))
            {
              if (debug) assert(vlc_tbl1[i] == 0);
              vlc_tbl1[i] = (tbl1[j].rho << 4) | (tbl1[j].u_off << 3)
                | (tbl1[j].e_k << 12) | (tbl1[j].e_1 << 8) | tbl1[j].cwd_len;
            }
      }

      return true;
    }

    /////////////////////////////////////////////////////////////////////////
    inline int decode_init_uvlc(ui32 vlc, ui32 mode, int *u)
    {
      //table stores possible decoding three bits from vlc
      // there are 8 entries for xx1, x10, 100, 000
      // 2 bits for prefix length
      // 3 bits for suffix length
      // 3 bits for prefix value
      static const ui8 dec[8] = {
        3 | (5 << 2) | (5 << 5), //000 == 000
        1 | (0 << 2) | (1 << 5), //001 == xx1
        2 | (0 << 2) | (2 << 5), //010 == x10
        1 | (0 << 2) | (1 << 5), //011 == xx1
        3 | (1 << 2) | (3 << 5), //100 == 100
        1 | (0 << 2) | (1 << 5), //101 == xx1
        2 | (0 << 2) | (2 << 5), //110 == x10
        1 | (0 << 2) | (1 << 5)  //111 == xx1
      };

      int consumed_bits = 0;
      if (mode == 0)
      {
        u[0] = u[1] = 1; //Kappa is 1 for initial line
      }
      else if (mode <= 2)
      {
        int d = dec[vlc & 0x7];
        vlc >>= d & 0x3;
        consumed_bits += d & 0x3;

        int suffix_len = ((d >> 2) & 0x7);
        consumed_bits += suffix_len;

        d = (d >> 5) + (vlc & ((1 << suffix_len) - 1));
        u[0] = (mode == 1) ? d + 1 : 1; //Kappa is 1 for initial line
        u[1] = (mode == 1) ? 1 : d + 1; //Kappa is 1 for initial line
      }
      else if (mode == 3)
      {
        int d1 = dec[vlc & 0x7];
        vlc >>= d1 & 0x3;
        consumed_bits += d1 & 0x3;

        if ((d1 & 0x3) > 2)
        {
          //u_{q_2} prefix
          u[1] = (vlc & 1) + 1 + 1; //Kappa is 1 for initial line
          ++consumed_bits;
          vlc >>= 1;

          int suffix_len = ((d1 >> 2) & 0x7);
          consumed_bits += suffix_len;
          d1 = (d1 >> 5) + (vlc & ((1 << suffix_len) - 1));
          u[0] = d1 + 1; //Kappa is 1 for initial line
        }
        else
        {
          int d2 = dec[vlc & 0x7];
          vlc >>= d2 & 0x3;
          consumed_bits += d2 & 0x3;

          int suffix_len = ((d1 >> 2) & 0x7);
          consumed_bits += suffix_len;

          d1 = (d1 >> 5) + (vlc & ((1 << suffix_len) - 1));
          u[0] = d1 + 1; //Kappa is 1 for initial line
          vlc >>= suffix_len;

          suffix_len = ((d2 >> 2) & 0x7);
          consumed_bits += suffix_len;

          d2 = (d2 >> 5) + (vlc & ((1 << suffix_len) - 1));
          u[1] = d2 + 1; //Kappa is 1 for initial line
        }
      }
      else if (mode == 4)
      {
        int d1 = dec[vlc & 0x7];
        vlc >>= d1 & 0x3;
        consumed_bits += d1 & 0x3;

        int d2 = dec[vlc & 0x7];
        vlc >>= d2 & 0x3;
        consumed_bits += d2 & 0x3;

        int suffix_len = ((d1 >> 2) & 0x7);
        consumed_bits += suffix_len;

        d1 = (d1 >> 5) + (vlc & ((1 << suffix_len) - 1));
        u[0] = d1 + 3; //Kappa is 1 for initial line
        vlc >>= suffix_len;

        suffix_len = ((d2 >> 2) & 0x7);
        consumed_bits += suffix_len;

        d2 = (d2 >> 5) + (vlc & ((1 << suffix_len) - 1));
        u[1] = d2 + 3; //Kappa is 1 for initial line
      }
      return consumed_bits;
    }

    /////////////////////////////////////////////////////////////////////////
    inline int decode_noninit_uvlc(ui32 vlc, ui32 mode, int *u)
    {
      //table stores possible decoding three bits from vlc
      // there are 8 entries for xx1, x10, 100, 000
      // 2 bits for prefix length
      // 3 bits for suffix length
      // 3 bits for prefix value
      static const ui8 dec[8] = {
        3 | (5 << 2) | (5 << 5), //000 == 000
        1 | (0 << 2) | (1 << 5), //001 == xx1
        2 | (0 << 2) | (2 << 5), //010 == x10
        1 | (0 << 2) | (1 << 5), //011 == xx1
        3 | (1 << 2) | (3 << 5), //100 == 100
        1 | (0 << 2) | (1 << 5), //101 == xx1
        2 | (0 << 2) | (2 << 5), //110 == x10
        1 | (0 << 2) | (1 << 5)  //111 == xx1
      };

      int consumed_bits = 0;
      if (mode == 0)
      {
        u[0] = u[1] = 1; //for kappa
      }
      else if (mode <= 2)
      {
        int d = dec[vlc & 0x7];
        vlc >>= d & 0x3;
        consumed_bits += d & 0x3;

        int suffix_len = ((d >> 2) & 0x7);
        consumed_bits += suffix_len;

        d = (d >> 5) + (vlc & ((1 << suffix_len) - 1));
        u[0] = (mode == 1) ? d + 1 : 1; //for kappa
        u[1] = (mode == 1) ? 1 : d + 1; //for kappa
      }
      else if (mode == 3)
      {
        int d1 = dec[vlc & 0x7];
        vlc >>= d1 & 0x3;
        consumed_bits += d1 & 0x3;

        int d2 = dec[vlc & 0x7];
        vlc >>= d2 & 0x3;
        consumed_bits += d2 & 0x3;

        int suffix_len = ((d1 >> 2) & 0x7);
        consumed_bits += suffix_len;

        d1 = (d1 >> 5) + (vlc & ((1 << suffix_len) - 1));
        u[0] = d1 + 1;  //for kappa
        vlc >>= suffix_len;

        suffix_len = ((d2 >> 2) & 0x7);
        consumed_bits += suffix_len;

        d2 = (d2 >> 5) + (vlc & ((1 << suffix_len) - 1));
        u[1] = d2 + 1;  //for kappa
      }
      return consumed_bits;
    }


    /////////////////////////////////////////////////////////////////////////
    static bool vlc_tables_initialized = vlc_init_tables();

    /////////////////////////////////////////////////////////////////////////
    //
    /////////////////////////////////////////////////////////////////////////
    struct frwd_struct {
      const ui8* data;        //pointer to where to read data
      ui64 tmp;         //temporary buffer of read data
      int bits;         //number of bits stored in tmp
      bool unstuff;     //true if a bit needs to be unstuffed
      int size;         //size of data
    };

    /////////////////////////////////////////////////////////////////////////
    template<int X>
    void frwd_read(frwd_struct *msp)
    {
      assert(msp->bits <= 32);

      ui32 val;
      val = *(ui32*)msp->data;
      msp->data += msp->size > 0 ? 4 : 0;

      int bits = 8 - msp->unstuff;
      ui32 t = msp->size-- > 0 ? (val & 0xFF) : X;
      bool unstuff = ((val & 0xFF) == 0xFF);

      t |= (msp->size-- > 0 ? ((val >> 8) & 0xFF) : X) << bits;
      bits += 8 - unstuff;
      unstuff = (((val >> 8) & 0xFF) == 0xFF);

      t |= (msp->size-- > 0 ? ((val >> 16) & 0xFF) : X) << bits;
      bits += 8 - unstuff;
      unstuff = (((val >> 16) & 0xFF) == 0xFF);

      t |= (msp->size-- > 0 ? ((val >> 24) & 0xFF) : X) << bits;
      bits += 8 - unstuff;
      msp->unstuff = (((val >> 24) & 0xFF) == 0xFF);

      msp->tmp |= ((ui64)t) << msp->bits;
      msp->bits += bits;
    }

    /////////////////////////////////////////////////////////////////////////
    template<int X>
    void frwd_init(frwd_struct *msp, const ui8* data, int size)
    {
      msp->data = data;
      msp->tmp = 0;
      msp->bits = 0;
      msp->unstuff = false;
      msp->size = size;

      //These few lines take care of the case where data is not at a multiple
      // of 4 boundary.  It reads at 1,2,3 up to 4 bytes from the mel stream
      int num = 4 - (intptr_t(msp->data) & 0x3);
      for (int i = 0; i < num; ++i)
      {
        ui64 d;
        d = msp->size-- > 0 ? *msp->data++ : X;
        msp->tmp |= (d << msp->bits);
        msp->bits += 8 - msp->unstuff;
        msp->unstuff = ((d & 0xFF) == 0xFF);
      }
      frwd_read<X>(msp);
    }

    /////////////////////////////////////////////////////////////////////////
    inline void frwd_advance(frwd_struct *msp, int num_bits)
    {
      assert(num_bits <= msp->bits);
      msp->tmp >>= num_bits;
      msp->bits -= num_bits;
    }

    /////////////////////////////////////////////////////////////////////////
    template<int X>
    ui32 frwd_fetch(frwd_struct *msp)
    {
      if (msp->bits < 32)
        frwd_read<X>(msp);
      return (ui32)msp->tmp;
    }


    /////////////////////////////////////////////////////////////////////////
    //
    /////////////////////////////////////////////////////////////////////////
    void ojph_decode_codeblock(ui8* coded_data, si32* decoded_data,
                               int missing_msbs, int num_passes,
                               int lengths1, int lengths2,
                               int width, int height, int stride)
    {
      //sigma: each ui32 contains flags for 32 locations, stripe high;
      // that is, 4 rows by 8 columns.  For 1024 columns, we need 32 integers.
      // Here, we need these arrays to be used interchangeably
      //One extra for simple implementation
      ui32 sigma1[33] = { 0 }, sigma2[33] = { 0 };
      //mbr: arranges similar to sigma.
      ui32 mbr1[33] = { 0 }, mbr2[33] = { 0 };
      //a pointer to sigma
      ui32* sip = sigma1;
      //pointers to arrays to be used interchangeably
      int sip_shift = 0; //decides where data go

      int p = 30 - missing_msbs; // Bit-plane index for cleanup pass

      // read scup and fix the bytes there
      int lcup, scup;
      lcup = lengths1;
      scup = (((int)coded_data[lcup-1]) << 4) + (coded_data[lcup-2] & 0xF);
      if (scup > lcup) //something is wrong
        return;

      //init mel
      mel_struct mel;
      mel_init(&mel, coded_data, lcup, scup);
      rev_struct vlc;
      rev_init(&vlc, coded_data, lcup, scup);
      frwd_struct magsgn;
      frwd_init<0xFF>(&magsgn, coded_data, lcup - scup);
      frwd_struct sigprop;
      frwd_init<0>(&sigprop, coded_data + lengths1, lengths2);
      rev_struct magref;
      if (num_passes > 2)
        rev_init_mrp(&magref, coded_data, lengths1, lengths2);

      //storage
      //one byte per quad to represent previous line
      //upper 6 bits represent max exponent for the bottom two pixels of quad
      //the lower 2bits are the significance of these samples, left sample
      // is 1st pixel and right sample is 2nd pixel
      ui8 *lsp, line_state[514]; //enough for 1024, max block width, + 2 extra

      //initial 2 lines
      /////////////////
      lsp = line_state;
      lsp[0] = 0;
      int run = mel_get_run(&mel);
      ui32 vlc_val, qinf[2] = { 0 };
      ui16 c_p = 0;
      si32* sp = decoded_data;
      for (int x = 0; x < width; x += 4)
      {
        // decode vlc
        /////////////

        //first quad
        vlc_val = rev_fetch(&vlc);
        qinf[0] = vlc_tbl0[ (c_p << 7) | (vlc_val & 0x7F) ];
        if (c_p == 0) //zero context
        {
          run -= 2;
          qinf[0] = (run == -1) ? qinf[0] : 0;
          if (run < 0) //either -1 or -2, get
            run = mel_get_run(&mel);
        }
        //prepare context for the next quad
        c_p = ((qinf[0] & 0x10) >> 4) | ((qinf[0] & 0xE0) >> 5);
        //remove data from vlc stream
        vlc_val = rev_advance(&vlc, qinf[0] & 0x7);

        //update sigma
        *sip |= (((qinf[0] & 0x30)>>4) | ((qinf[0] & 0xC0)>>2)) << sip_shift;

        //second quad
        qinf[1] = 0;
        if (x + 2 < width)
        {
          qinf[1] = vlc_tbl0[(c_p << 7) | (vlc_val & 0x7F)];
          if (c_p == 0) //zero context
          {
            run -= 2;
            qinf[1] = (run == -1) ? qinf[1] : 0;
            if (run < 0) //either -1 or -2, get
              run = mel_get_run(&mel);
          }
          //prepare context for the next quad
          c_p = ((qinf[1] & 0x10) >> 4) | ((qinf[1] & 0xE0) >> 5);
          //remove data from vlc stream
          vlc_val = rev_advance(&vlc, qinf[1] & 0x7);
        }

        //update sigma
        *sip |= (((qinf[1] & 0x30) | ((qinf[1] & 0xC0)<<2))) << (4+sip_shift);

        sip += x & 0x7 ? 1 : 0;
        sip_shift ^= 0x10;

        //retrieve u
        ////////////
        int U_p[2];
        int uvlc_mode = ((qinf[0] & 0x8) >> 3) | ((qinf[1] & 0x8) >> 2);
        if (uvlc_mode == 3)
        {
          run -= 2;
          uvlc_mode += (run == -1) ? 1 : 0;
          if (run < 0) //either -1 or -2, get
            run = mel_get_run(&mel);
        }
        int consumed_bits = decode_init_uvlc(vlc_val, uvlc_mode, U_p);
        vlc_val = rev_advance(&vlc, consumed_bits);

        //decode magsgn and update line_state
        /////////////////////////////////////
        int m_n, v_n;
        ui32 ms_val;

        //locations where samples need update
        int locs = 4 - (width - x);
        locs = 0xFF >> (locs > 0 ? (locs<<1) : 0);
        locs = height > 1 ? locs : (locs & 0x55);

        if (qinf[0] & 0x10) //sigma_n
        {
          ms_val = frwd_fetch<0xFF>(&magsgn);
          m_n = U_p[0] - ((qinf[0] >> 12) & 1); //m_n
          frwd_advance(&magsgn, m_n);
          si32 val = ms_val << 31;
          v_n = ms_val & ((1 << m_n) - 1);
          v_n |= ((qinf[0] & 0x100) >> 8) << m_n;
          v_n |= 1; //center of bin
          sp[0] = val | ((v_n + 2) << (p - 1));
        }
        else if (locs & 0x1)
          sp[0] = 0;

        if (qinf[0] & 0x20) //sigma_n
        {
          ms_val = frwd_fetch<0xFF>(&magsgn);
          m_n = U_p[0] - ((qinf[0] >> 13) & 1); //m_n
          frwd_advance(&magsgn, m_n);
          si32 val = ms_val << 31;
          v_n = ms_val & ((1 << m_n) - 1);
          v_n |= ((qinf[0] & 0x200) >> 9) << m_n;
          v_n |= 1; //center of bin
          sp[stride] = val | ((v_n + 2) << (p - 1));

          //update line_state: bit 7 (\sigma^N), and E^N
          int s = (lsp[0] & 0x80) | 0x80; //\sigma^NW | \sigma^N
          int t = lsp[0] & 0x7F; //E^NW
          v_n = 32 - count_leading_zeros(v_n); //because E-=2;
          lsp[0] = s | (t > v_n ? t : v_n);
        }
        else if (locs & 0x2)
          sp[stride] = 0; //no need to update line_state

        ++lsp;
        ++sp;

        if (qinf[0] & 0x40) //sigma_n
        {
          ms_val = frwd_fetch<0xFF>(&magsgn);
          m_n = U_p[0] - ((qinf[0] >> 14) & 1); //m_n
          frwd_advance(&magsgn, m_n);
          si32 val = ms_val << 31;
          v_n = ms_val & ((1 << m_n) - 1);
          v_n |= (((qinf[0] & 0x400) >> 10) << m_n);
          v_n |= 1; //center of bin
          sp[0] = val | ((v_n + 2) << (p - 1));
        }
        else if (locs & 0x4)
          sp[0] = 0;

        lsp[0] = 0;
        if (qinf[0] & 0x80) //sigma_n
        {
          ms_val = frwd_fetch<0xFF>(&magsgn);
          m_n = U_p[0] - ((qinf[0] >> 15) & 1); //m_n
          frwd_advance(&magsgn, m_n);
          si32 val = ms_val << 31;
          v_n = ms_val & ((1 << m_n) - 1);
          v_n |= ((qinf[0] & 0x800) >> 11) << m_n;
          v_n |= 1; //center of bin
          sp[stride] = val | ((v_n + 2) << (p - 1));

          //update line_state: bit 7 (\sigma^NW), and E^NW for next quad
          lsp[0] = 0x80 | 32 - count_leading_zeros(v_n); //because E-=2;
        }
        else if (locs & 0x8)
          sp[stride] = 0;

        ++sp;

        if (qinf[1] & 0x10) //sigma_n
        {
          ms_val = frwd_fetch<0xFF>(&magsgn);
          m_n = U_p[1] - ((qinf[1] >> 12) & 1); //m_n
          frwd_advance(&magsgn, m_n);
          si32 val = ms_val << 31;
          v_n = ms_val & ((1 << m_n) - 1);
          v_n |= (((qinf[1] & 0x100) >> 8) << m_n);
          v_n |= 1; //center of bin
          sp[0] = val | ((v_n + 2) << (p - 1));
        }
        else if (locs & 0x10)
          sp[0] = 0;

        if (qinf[1] & 0x20) //sigma_n
        {
          ms_val = frwd_fetch<0xFF>(&magsgn);
          m_n = U_p[1] - ((qinf[1] >> 13) & 1); //m_n
          frwd_advance(&magsgn, m_n);
          si32 val = ms_val << 31;
          v_n = ms_val & ((1 << m_n) - 1);
          v_n |= (((qinf[1] & 0x200) >> 9) << m_n);
          v_n |= 1; //center of bin
          sp[stride] = val | ((v_n + 2) << (p - 1));

          //update line_state: bit 7 (\sigma^N), and E^N
          int s = (lsp[0] & 0x80) | 0x80; //\sigma^NW | \sigma^N
          int t = lsp[0] & 0x7F; //E^NW
          v_n = 32 - count_leading_zeros(v_n); //because E-=2;
          lsp[0] = s | (t > v_n ? t : v_n);
        }
        else if (locs & 0x20)
          sp[stride] = 0; //no need to update line_state

        ++lsp;
        ++sp;

        if (qinf[1] & 0x40) //sigma_n
        {
          ms_val = frwd_fetch<0xFF>(&magsgn);
          m_n = U_p[1] - ((qinf[1] >> 14) & 1); //m_n
          frwd_advance(&magsgn, m_n);
          si32 val = ms_val << 31;
          v_n = ms_val & ((1 << m_n) - 1);
          v_n |= (((qinf[1] & 0x400) >> 10) << m_n);
          v_n |= 1; //center of bin
          sp[0] = val | ((v_n + 2) << (p - 1));
        }
        else if (locs & 0x40)
          sp[0] = 0;

        lsp[0] = 0;
        if (qinf[1] & 0x80) //sigma_n
        {
          ms_val = frwd_fetch<0xFF>(&magsgn);
          m_n = U_p[1] - ((qinf[1] >> 15) & 1); //m_n
          frwd_advance(&magsgn, m_n);
          si32 val = ms_val << 31;
          v_n = ms_val & ((1 << m_n) - 1);
          v_n |= (((qinf[1] & 0x800) >> 11) << m_n);
          v_n |= 1; //center of bin
          sp[stride] = val | ((v_n + 2) << (p - 1));

          //update line_state: bit 7 (\sigma^NW), and E^NW for next quad
          lsp[0] = 0x80 | 32 - count_leading_zeros(v_n); //because E-=2;
        }
        else if (locs & 0x80)
          sp[stride] = 0;

        ++sp;
      }

      //non-initial lines
      //////////////////////////
      for (int y = 2; y < height; /*done at the end of loop*/)
      {
        sip_shift ^= 0x2;
        sip_shift &= 0xFFFFFFEF;
        ui32 *sip = y & 0x4 ? sigma2 : sigma1;

        lsp = line_state;
        ui8 ls0 = lsp[0];
        lsp[0] = 0;
        sp = decoded_data + y * stride;
        c_p = 0;
        for (int x = 0; x < width; x += 4)
        {
          // decode vlc
          /////////////

          //first quad
          c_p |= (ls0 >> 7);
          c_p |= (lsp[1] >> 5) & 0x4;
          vlc_val = rev_fetch(&vlc);
          qinf[0] = vlc_tbl1[(c_p << 7) | (vlc_val & 0x7F)];
          if (c_p == 0) //zero context
          {
            run -= 2;
            qinf[0] = (run == -1) ? qinf[0] : 0;
            if (run < 0) //either -1 or -2, get
              run = mel_get_run(&mel);
          }
          //prepare context for the next quad
          c_p = ((qinf[0] & 0x40) >> 5) | ((qinf[0] & 0x80) >> 6);
          //remove data from vlc stream
          vlc_val = rev_advance(&vlc, qinf[0] & 0x7);

          //update sigma
          *sip |= (((qinf[0]&0x30) >> 4) | ((qinf[0]&0xC0) >> 2)) << sip_shift;

          //second quad
          qinf[1] = 0;
          if (x + 2 < width)
          {
            c_p |= (lsp[1] >> 7);
            c_p |= (lsp[2] >> 5) & 0x4;
            qinf[1] = vlc_tbl1[(c_p << 7) | (vlc_val & 0x7F)];
            if (c_p == 0) //zero context
            {
              run -= 2;
              qinf[1] = (run == -1) ? qinf[1] : 0;
              if (run < 0) //either -1 or -2, get
                run = mel_get_run(&mel);
            }
            //prepare context for the next quad
            c_p = ((qinf[1] & 0x40) >> 5) | ((qinf[1] & 0x80) >> 6);
            //remove data from vlc stream
            vlc_val = rev_advance(&vlc, qinf[1] & 0x7);
          }

          //update sigma
          *sip |= (((qinf[1]&0x30) | ((qinf[1]&0xC0) << 2))) << (4+sip_shift);

          sip += x & 0x7 ? 1 : 0;
          sip_shift ^= 0x10;

          //retrieve u
          ////////////
          int U_p[2];
          int uvlc_mode = ((qinf[0] & 0x8) >> 3) | ((qinf[1] & 0x8) >> 2);
          int consumed_bits = decode_noninit_uvlc(vlc_val, uvlc_mode, U_p);
          vlc_val = rev_advance(&vlc, consumed_bits);

          //calculate kappa and add it to U_p
          if ((qinf[0] & 0xF0) & ((qinf[0] & 0xF0) - 1))
          {
            int E = (ls0 & 0x7F);
            E = E > (lsp[1] & 0x7F) ? E : (lsp[1] & 0x7F);
            E -= 2;
            U_p[0] += E > 0 ? E : 0;
          }

          if ((qinf[1] & 0xF0) & ((qinf[1] & 0xF0) - 1))
          {
            int E = (lsp[1] & 0x7F);
            E = E > (lsp[2] & 0x7F) ? E : (lsp[2] & 0x7F);
            E -= 2;
            U_p[1] += E > 0 ? E : 0;
          }

          ls0 = lsp[2]; //for next double quad
          lsp[1] = lsp[2] = 0;

          //decode magsgn and update line_state
          /////////////////////////////////////
          int m_n, v_n;
          ui32 ms_val;

          //locations where samples need update
          int locs = 4 - (width - x);
          locs = 0xFF >> (locs > 0 ? (locs << 1) : 0);
          locs = y < height - 1 ? locs : (locs & 0x55);

          if (qinf[0] & 0x10) //sigma_n
          {
            ms_val = frwd_fetch<0xFF>(&magsgn);
            m_n = U_p[0] - ((qinf[0] >> 12) & 1); //m_n
            frwd_advance(&magsgn, m_n);
            si32 val = ms_val << 31;
            v_n = ms_val & ((1 << m_n) - 1);
            v_n |= ((qinf[0] & 0x100) >> 8) << m_n;
            v_n |= 1; //center of bin
            sp[0] = val | ((v_n + 2) << (p - 1));
          }
          else if (locs & 0x1)
            sp[0] = 0;

          if (qinf[0] & 0x20) //sigma_n
          {
            ms_val = frwd_fetch<0xFF>(&magsgn);
            m_n = U_p[0] - ((qinf[0] >> 13) & 1); //m_n
            frwd_advance(&magsgn, m_n);
            si32 val = ms_val << 31;
            v_n = ms_val & ((1 << m_n) - 1);
            v_n |= ((qinf[0] & 0x200) >> 9) << m_n;
            v_n |= 1; //center of bin
            sp[stride] = val | ((v_n + 2) << (p - 1));

            //update line_state: bit 7 (\sigma^N), and E^N
            int s = (lsp[0] & 0x80) | 0x80; //\sigma^NW | \sigma^N
            int t = lsp[0] & 0x7F; //E^NW
            v_n = 32 - count_leading_zeros(v_n); //because E-=2;
            lsp[0] = s | (t > v_n ? t : v_n);
          }
          else if (locs & 0x2)
            sp[stride] = 0; //no need to update line_state

          ++lsp;
          ++sp;

          if (qinf[0] & 0x40) //sigma_n
          {
            ms_val = frwd_fetch<0xFF>(&magsgn);
            m_n = U_p[0] - ((qinf[0] >> 14) & 1); //m_n
            frwd_advance(&magsgn, m_n);
            si32 val = ms_val << 31;
            v_n = ms_val & ((1 << m_n) - 1);
            v_n |= (((qinf[0] & 0x400) >> 10) << m_n);
            v_n |= 1; //center of bin
            sp[0] = val | ((v_n + 2) << (p - 1));
          }
          else if (locs & 0x4)
            sp[0] = 0;

          //      lsp[0] = 0;
          if (qinf[0] & 0x80) //sigma_n
          {
            ms_val = frwd_fetch<0xFF>(&magsgn);
            m_n = U_p[0] - ((qinf[0] >> 15) & 1); //m_n
            frwd_advance(&magsgn, m_n);
            si32 val = ms_val << 31;
            v_n = ms_val & ((1 << m_n) - 1);
            v_n |= ((qinf[0] & 0x800) >> 11) << m_n;
            v_n |= 1; //center of bin
            sp[stride] = val | ((v_n + 2) << (p - 1));

            //update line_state: bit 7 (\sigma^NW), and E^NW for next quad
            lsp[0] = 0x80 | 32 - count_leading_zeros(v_n); //because E-=2
          }
          else if (locs & 0x8)
            sp[stride] = 0;

          ++sp;

          if (qinf[1] & 0x10) //sigma_n
          {
            ms_val = frwd_fetch<0xFF>(&magsgn);
            m_n = U_p[1] - ((qinf[1] >> 12) & 1); //m_n
            frwd_advance(&magsgn, m_n);
            si32 val = ms_val << 31;
            v_n = ms_val & ((1 << m_n) - 1);
            v_n |= (((qinf[1] & 0x100) >> 8) << m_n);
            v_n |= 1; //center of bin
            sp[0] = val | ((v_n + 2) << (p - 1));
          }
          else if (locs & 0x10)
            sp[0] = 0;

          if (qinf[1] & 0x20) //sigma_n
          {
            ms_val = frwd_fetch<0xFF>(&magsgn);
            m_n = U_p[1] - ((qinf[1] >> 13) & 1); //m_n
            frwd_advance(&magsgn, m_n);
            si32 val = ms_val << 31;
            v_n = ms_val & ((1 << m_n) - 1);
            v_n |= (((qinf[1] & 0x200) >> 9) << m_n);
            v_n |= 1; //center of bin
            sp[stride] = val | ((v_n + 2) << (p - 1));

            //update line_state: bit 7 (\sigma^N), and E^N
            int s = (lsp[0] & 0x80) | 0x80; //\sigma^NW | \sigma^N
            int t = lsp[0] & 0x7F; //E^NW
            v_n = 32 - count_leading_zeros(v_n); //because E-=2;
            lsp[0] = s | (t > v_n ? t : v_n);
          }
          else if (locs & 0x20)
            sp[stride] = 0; //no need to update line_state

          ++lsp;
          ++sp;

          if (qinf[1] & 0x40) //sigma_n
          {
            ms_val = frwd_fetch<0xFF>(&magsgn);
            m_n = U_p[1] - ((qinf[1] >> 14) & 1); //m_n
            frwd_advance(&magsgn, m_n);
            si32 val = ms_val << 31;
            v_n = ms_val & ((1 << m_n) - 1);
            v_n |= (((qinf[1] & 0x400) >> 10) << m_n);
            v_n |= 1; //center of bin
            sp[0] = val | ((v_n + 2) << (p - 1));
          }
          else if (locs & 0x40)
            sp[0] = 0;

          //      lsp[0] = 0;
          if (qinf[1] & 0x80) //sigma_n
          {
            ms_val = frwd_fetch<0xFF>(&magsgn);
            m_n = U_p[1] - ((qinf[1] >> 15) & 1); //m_n
            frwd_advance(&magsgn, m_n);
            si32 val = ms_val << 31;
            v_n = ms_val & ((1 << m_n) - 1);
            v_n |= (((qinf[1] & 0x800) >> 11) << m_n);
            v_n |= 1; //center of bin
            sp[stride] = val | ((v_n + 2) << (p - 1));

            //update line_state: bit 7 (\sigma^NW), and E^NW for next quad
            lsp[0] = 0x80 | 32 - count_leading_zeros(v_n); //because E-=2
          }
          else if (locs & 0x80)
            sp[stride] = 0;

          ++sp;

        }

        y += 2;
        if (num_passes > 1 && (y & 3) == 0) {

          if (num_passes > 2) //do magref
          {
            ui32 *cur_sig = y & 0x4 ? sigma1 : sigma2;
            si32 *dpp = decoded_data + (y - 4) * stride;
            ui32 half = 1 << (p - 2);
            for (int i = 0; i < width; i += 8)
            {
              ui32 cwd = rev_fetch_mrp(&magref);
              ui32 sig = *cur_sig++;
              ui32 col_mask = 0xF;
              si32 *dp = dpp + i;
              if (sig)
              {
                for (int j = 0; j < 8; ++j, dp++)
                {
                  if (sig & col_mask)
                  {
                    ui32 sample_mask = 0x11111111 & col_mask;

                    if (sig & sample_mask)
                    {
                      assert(dp[0] != 0);
                      ui32 sym = cwd & 1;
                      dp[0] ^= (1 - sym) << (p - 1);
                      dp[0] |= half;
                      cwd >>= 1;
                    }
                    sample_mask += sample_mask;

                    if (sig & sample_mask)
                    {
                      assert(dp[stride] != 0);
                      ui32 sym = cwd & 1;
                      dp[stride] ^= (1 - sym) << (p - 1);
                      dp[stride] |= half;
                      cwd >>= 1;
                    }
                    sample_mask += sample_mask;

                    if (sig & sample_mask)
                    {
                      assert(dp[2 * stride] != 0);
                      ui32 sym = cwd & 1;
                      dp[2 * stride] ^= (1 - sym) << (p - 1);
                      dp[2 * stride] |= half;
                      cwd >>= 1;
                    }
                    sample_mask += sample_mask;

                    if (sig & sample_mask)
                    {
                      assert(dp[3 * stride] != 0);
                      ui32 sym = cwd & 1;
                      dp[3 * stride] ^= (1 - sym) << (p - 1);
                      dp[3 * stride] |= half;
                      cwd >>= 1;
                    }
                    sample_mask += sample_mask;
                  }
                  col_mask <<= 4;
                }
              }
              rev_advance_mrp(&magref, population_count(sig));
            }
          }

          if (y >= 4)
          {
            //generate mbr of first stripe
            ui32 *sig = y & 0x4 ? sigma1 : sigma2;
            ui32 *mbr = y & 0x4 ? mbr1 : mbr2;
            //integrate horizontally
            ui32 prev = 0;
            for (int i = 0; i < width; i += 8, mbr++, sig++)
            {
              mbr[0] = sig[0];
              mbr[0] |= prev >> 28;    //for first column, left neighbors
              mbr[0] |= sig[0] << 4;   //left neighbors
              mbr[0] |= sig[0] >> 4;   //left neighbors
              mbr[0] |= sig[1] << 28;  //for last column, right neighbors
              prev = sig[0];

              //integrate vertically
              int t = mbr[0], z = mbr[0];
              z |= (t & 0x77777777) << 1; //above neighbors
              z |= (t & 0xEEEEEEEE) >> 1; //below neighbors
              mbr[0] = z & ~sig[0]; //remove already significance samples
            }
          }

          if (y >= 8) //wait until 8 rows has been processed
          {
            ui32 *cur_sig, *cur_mbr, *nxt_sig, *nxt_mbr;

            //add membership from the next stripe, obtained above
            cur_sig = y & 0x4 ? sigma2 : sigma1;
            cur_mbr = y & 0x4 ? mbr2 : mbr1;
            nxt_sig = y & 0x4 ? sigma1 : sigma2;
            ui32 prev = 0;
            for (int i = 0; i < width; i += 8, cur_mbr++, cur_sig++, nxt_sig++)
            {
              ui32 t = nxt_sig[0];
              t |= prev >> 28;        //for first column, left neighbors
              t |= nxt_sig[0] << 4;   //left neighbors
              t |= nxt_sig[0] >> 4;   //left neighbors
              t |= nxt_sig[1] << 28;  //for last column, right neighbors
              prev = nxt_sig[0];

              cur_mbr[0] |= (t & 0x11111111) << 3;
              cur_mbr[0] &= ~cur_sig[0]; //remove already significance samples
            }

            //find new locations and get signs
            cur_sig = y & 0x4 ? sigma2 : sigma1;
            cur_mbr = y & 0x4 ? mbr2 : mbr1;
            nxt_sig = y & 0x4 ? sigma1 : sigma2;
            nxt_mbr = y & 0x4 ? mbr1 : mbr2;
            ui32 val = 3 << (p - 2);
            for (int i = 0; i < width;
                 i += 8, cur_sig++, cur_mbr++, nxt_sig++, nxt_mbr++)
            {
              int mbr = *cur_mbr;
              ui32 new_sig = 0;
              if (mbr)
              {
                for (int n = 0; n < 8; n += 4)
                {
                  ui32 cwd = frwd_fetch<0>(&sigprop);
                  int cnt = 0;

                  si32 *dp = decoded_data + (y - 8) * stride;
                  dp += i + n;

                  ui32 col_mask = 0xF << (4 * n);

                  ui32 inv_sig = ~cur_sig[0];

                  int end = n + 4 < width - i ? n + 4 : width - i;
                  for (int j = n; j < end; ++j, ++dp, col_mask <<= 4)
                  {
                    if ((col_mask & mbr) == 0)
                      continue;

                    //scan 4 mbr
                    int sample_mask = 0x11111111 & col_mask;
                    if (mbr & sample_mask)
                    {
                      assert(dp[0] == 0);
                      if (cwd & 1)
                      {
                        new_sig |= sample_mask;
                        ui32 t = 0x32 << (j * 4);
                        mbr |= t & inv_sig;
                      }
                      cwd >>= 1; ++cnt;
                    }

                    sample_mask += sample_mask;
                    if (mbr & sample_mask)
                    {
                      assert(dp[stride] == 0);
                      if (cwd & 1)
                      {
                        new_sig |= sample_mask;
                        ui32 t = 0x74 << (j * 4);
                        mbr |= t & inv_sig;
                      }
                      cwd >>= 1; ++cnt;
                    }

                    sample_mask += sample_mask;
                    if (mbr & sample_mask)
                    {
                      assert(dp[2 * stride] == 0);
                      if (cwd & 1)
                      {
                        new_sig |= sample_mask;
                        ui32 t = 0xE8 << (j * 4);
                        mbr |= t & inv_sig;
                      }
                      cwd >>= 1; ++cnt;
                    }

                    sample_mask += sample_mask;
                    if (mbr & sample_mask)
                    {
                      assert(dp[3 * stride] == 0);
                      if (cwd & 1)
                      {
                        new_sig |= sample_mask;
                        ui32 t = 0xC0 << (j * 4);
                        mbr |= t & inv_sig;
                      }
                      cwd >>= 1; ++cnt;
                    }
                  }

                  //signs here
                  if (new_sig & (0xFFFF << (4 * n)))
                  {
                    si32 *dp = decoded_data + (y - 8) * stride;
                    dp += i + n;
                    ui32 col_mask = 0xF << (4 * n);

                    for (int j = n; j < end; ++j, ++dp, col_mask <<= 4)
                    {
                      if ((col_mask & new_sig) == 0)
                        continue;

                      //scan 4 signs
                      int sample_mask = 0x11111111 & col_mask;
                      if (new_sig & sample_mask)
                      {
                        assert(dp[0] == 0);
                        dp[0] |= ((cwd & 1) << 31) | val;
                        cwd >>= 1; ++cnt;
                      }

                      sample_mask += sample_mask;
                      if (new_sig & sample_mask)
                      {
                        assert(dp[stride] == 0);
                        dp[stride] |= ((cwd & 1) << 31) | val;
                        cwd >>= 1; ++cnt;
                      }

                      sample_mask += sample_mask;
                      if (new_sig & sample_mask)
                      {
                        assert(dp[2 * stride] == 0);
                        dp[2 * stride] |= ((cwd & 1) << 31) | val;
                        cwd >>= 1; ++cnt;
                      }

                      sample_mask += sample_mask;
                      if (new_sig & sample_mask)
                      {
                        assert(dp[3 * stride] == 0);
                        dp[3 * stride] |= ((cwd & 1) << 31) | val;
                        cwd >>= 1; ++cnt;
                      }
                    }

                  }
                  frwd_advance(&sigprop, cnt);
                  cnt = 0;

                  //update next stripe
                  if (n == 4)
                  {
                    //horizontally
                    ui32 t = new_sig >> 28;
                    t |= ((t & 0xE) >> 1) | ((t & 7) << 1);
                    cur_mbr[1] |= t & ~cur_sig[1];
                  }
                }
              }
              //vertically
              new_sig |= cur_sig[0];
              ui32 u = (new_sig & 0x88888888) >> 3;
              ui32 t = u | (u << 4) | (u >> 4);
              if (i > 0)
                nxt_mbr[-1] |= (u << 28) & ~nxt_sig[-1];
              nxt_mbr[0] |= t & ~nxt_sig[0];
              nxt_mbr[1] |= (u >> 28) & ~nxt_sig[1];
            }

            //clear current sigma
            //mbr need not be cleared because it is overwritten
            cur_sig = y & 0x4 ? sigma2 : sigma1;
            memset(cur_sig, 0, ((width + 7) >> 3) << 2);
          }
        }
      }

      //terminating
      if (num_passes > 1) {

        if (num_passes > 2 && ((height & 3) == 1 || (height & 3) == 2))
        {//do magref
          ui32 *cur_sig = height & 0x4 ? sigma2 : sigma1; //reversed
          si32 *dpp = decoded_data + (height & 0xFFFFFFFC) * stride;
          ui32 half = 1 << (p - 2);
          for (int i = 0; i < width; i += 8)
          {
            ui32 cwd = rev_fetch_mrp(&magref);
            ui32 sig = *cur_sig++;
            ui32 col_mask = 0xF;
            si32 *dp = dpp + i;
            if (sig)
            {
              for (int j = 0; j < 8; ++j, dp++)
              {
                if (sig & col_mask)
                {
                  ui32 sample_mask = 0x11111111 & col_mask;

                  if (sig & sample_mask)
                  {
                    assert(dp[0] != 0);
                    ui32 sym = cwd & 1;
                    dp[0] ^= (1 - sym) << (p - 1);
                    dp[0] |= half;
                    cwd >>= 1;
                  }
                  sample_mask += sample_mask;

                  if (sig & sample_mask)
                  {
                    assert(dp[stride] != 0);
                    ui32 sym = cwd & 1;
                    dp[stride] ^= (1 - sym) << (p - 1);
                    dp[stride] |= half;
                    cwd >>= 1;
                  }
                  sample_mask += sample_mask;

                  if (sig & sample_mask)
                  {
                    assert(dp[2 * stride] != 0);
                    ui32 sym = cwd & 1;
                    dp[2 * stride] ^= (1 - sym) << (p - 1);
                    dp[2 * stride] |= half;
                    cwd >>= 1;
                  }
                  sample_mask += sample_mask;

                  if (sig & sample_mask)
                  {
                    assert(dp[3 * stride] != 0);
                    ui32 sym = cwd & 1;
                    dp[3 * stride] ^= (1 - sym) << (p - 1);
                    dp[3 * stride] |= half;
                    cwd >>= 1;
                  }
                  sample_mask += sample_mask;
                }
                col_mask <<= 4;
              }
            }
            rev_advance_mrp(&magref, population_count(sig));
          }
        }

        //do the last incomplete stripe
        // for cases of (height & 3) == 0 and 3
        // the should have been processed previously
        if ((height & 3) == 1 || (height & 3) == 2)
        {
          //generate mbr of first stripe
          ui32 *sig = height & 0x4 ? sigma2 : sigma1;
          ui32 *mbr = height & 0x4 ? mbr2 : mbr1;
          //integrate horizontally
          ui32 prev = 0;
          for (int i = 0; i < width; i += 8, mbr++, sig++)
          {
            mbr[0] = sig[0];
            mbr[0] |= prev >> 28;    //for first column, left neighbors
            mbr[0] |= sig[0] << 4;   //left neighbors
            mbr[0] |= sig[0] >> 4;   //left neighbors
            mbr[0] |= sig[1] << 28;  //for last column, right neighbors
            prev = sig[0];

            //integrate vertically
            int t = mbr[0], z = mbr[0];
            z |= (t & 0x77777777) << 1; //above neighbors
            z |= (t & 0xEEEEEEEE) >> 1; //below neighbors
            mbr[0] = z & ~sig[0]; //remove already significance samples
          }
        }

        int st = height;
        st -= height > 6 ? (((height + 1) & 3) + 3) : height;
        for (int y = st; y < height; y += 4)
        {
          ui32 *cur_sig, *cur_mbr, *nxt_sig, *nxt_mbr;

          int pattern = 0xFFFFFFFF;
          if (height - y == 3)
            pattern = 0x77777777;
          else if (height - y == 2)
            pattern = 0x33333333;
          else if (height - y == 1)
            pattern = 0x11111111;

          //add membership from the next stripe, obtained above
          if (height - y > 4)
          {
            cur_sig = y & 0x4 ? sigma2 : sigma1;
            cur_mbr = y & 0x4 ? mbr2 : mbr1;
            nxt_sig = y & 0x4 ? sigma1 : sigma2;
            ui32 prev = 0;
            for (int i = 0; i < width; i += 8, cur_mbr++, cur_sig++, nxt_sig++)
            {
              ui32 t = nxt_sig[0];
              t |= prev >> 28;     //for first column, left neighbors
              t |= nxt_sig[0] << 4;   //left neighbors
              t |= nxt_sig[0] >> 4;   //left neighbors
              t |= nxt_sig[1] << 28;  //for last column, right neighbors
              prev = nxt_sig[0];

              cur_mbr[0] |= (t & 0x11111111) << 3;
              //remove already significance samples
              cur_mbr[0] &= ~cur_sig[0];
            }
          }

          //find new locations and get signs
          cur_sig = y & 0x4 ? sigma2 : sigma1;
          cur_mbr = y & 0x4 ? mbr2 : mbr1;
          nxt_sig = y & 0x4 ? sigma1 : sigma2;
          nxt_mbr = y & 0x4 ? mbr1 : mbr2;
          ui32 val = 3 << (p - 2);
          for (int i = 0; i < width; i += 8,
               cur_sig++, cur_mbr++, nxt_sig++, nxt_mbr++)
          {
            int mbr = *cur_mbr & pattern;
            ui32 new_sig = 0;
            if (mbr)
            {
              for (int n = 0; n < 8; n += 4)
              {
                ui32 cwd = frwd_fetch<0>(&sigprop);
                int cnt = 0;

                si32 *dp = decoded_data + y * stride;
                dp += i + n;

                ui32 col_mask = 0xF << (4 * n);

                ui32 inv_sig = ~cur_sig[0] & pattern;

                int end = n + 4 < width - i ? n + 4 : width - i;
                for (int j = n; j < end; ++j, ++dp, col_mask <<= 4)
                {
                  if ((col_mask & mbr) == 0)
                    continue;

                  //scan 4 mbr
                  int sample_mask = 0x11111111 & col_mask;
                  if (mbr & sample_mask)
                  {
                    assert(dp[0] == 0);
                    if (cwd & 1)
                    {
                      new_sig |= sample_mask;
                      ui32 t = 0x32 << (j * 4);
                      mbr |= t & inv_sig;
                    }
                    cwd >>= 1; ++cnt;
                  }

                  sample_mask += sample_mask;
                  if (mbr & sample_mask)
                  {
                    assert(dp[stride] == 0);
                    if (cwd & 1)
                    {
                      new_sig |= sample_mask;
                      ui32 t = 0x74 << (j * 4);
                      mbr |= t & inv_sig;
                    }
                    cwd >>= 1; ++cnt;
                  }

                  sample_mask += sample_mask;
                  if (mbr & sample_mask)
                  {
                    assert(dp[2 * stride] == 0);
                    if (cwd & 1)
                    {
                      new_sig |= sample_mask;
                      ui32 t = 0xE8 << (j * 4);
                      mbr |= t & inv_sig;
                    }
                    cwd >>= 1; ++cnt;
                  }

                  sample_mask += sample_mask;
                  if (mbr & sample_mask)
                  {
                    assert(dp[3 * stride] == 0);
                    if (cwd & 1)
                    {
                      new_sig |= sample_mask;
                      ui32 t = 0xC0 << (j * 4);
                      mbr |= t & inv_sig;
                    }
                    cwd >>= 1; ++cnt;
                  }
                }

                //signs here
                if (new_sig & (0xFFFF << (4 * n)))
                {
                  si32 *dp = decoded_data + y * stride;
                  dp += i + n;
                  ui32 col_mask = 0xF << (4 * n);

                  for (int j = n; j < end; ++j, ++dp, col_mask <<= 4)
                  {
                    if ((col_mask & new_sig) == 0)
                      continue;

                    //scan 4 signs
                    int sample_mask = 0x11111111 & col_mask;
                    if (new_sig & sample_mask)
                    {
                      assert(dp[0] == 0);
                      dp[0] |= ((cwd & 1) << 31) | val;
                      cwd >>= 1; ++cnt;
                    }

                    sample_mask += sample_mask;
                    if (new_sig & sample_mask)
                    {
                      assert(dp[stride] == 0);
                      dp[stride] |= ((cwd & 1) << 31) | val;
                      cwd >>= 1; ++cnt;
                    }

                    sample_mask += sample_mask;
                    if (new_sig & sample_mask)
                    {
                      assert(dp[2 * stride] == 0);
                      dp[2 * stride] |= ((cwd & 1) << 31) | val;
                      cwd >>= 1; ++cnt;
                    }

                    sample_mask += sample_mask;
                    if (new_sig & sample_mask)
                    {
                      assert(dp[3 * stride] == 0);
                      dp[3 * stride] |= ((cwd & 1) << 31) | val;
                      cwd >>= 1; ++cnt;
                    }
                  }

                }
                frwd_advance(&sigprop, cnt);
                cnt = 0;

                //update next stripe
                if (n == 4)
                {
                  //horizontally
                  ui32 t = new_sig >> 28;
                  t |= ((t & 0xE) >> 1) | ((t & 7) << 1);
                  cur_mbr[1] |= t & ~cur_sig[1];
                }
              }
            }
            //vertically
            new_sig |= cur_sig[0];
            ui32 u = (new_sig & 0x88888888) >> 3;
            ui32 t = u | (u << 4) | (u >> 4);
            if (i > 0)
              nxt_mbr[-1] |= (u << 28) & ~nxt_sig[-1];
            nxt_mbr[0] |= t & ~nxt_sig[0];
            nxt_mbr[1] |= (u >> 28) & ~nxt_sig[1];
          }
        }
      }
    }
  }
}
