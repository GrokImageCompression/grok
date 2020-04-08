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
// File: ojph_expand.cpp
// Author: Aous Naman
// Date: 28 August 2019
//***************************************************************************/


#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
using namespace std;

namespace grk {

class sqrt_energy_gains
{
public:
  static float get_gain_l(int num_decomp, bool reversible);
  static float get_gain_h(int num_decomp, bool reversible);
private:
  static const float gain_9x7_l[34];
  static const float gain_9x7_h[34];
  static const float gain_5x3_l[34];
  static const float gain_5x3_h[34];
};

struct param_qcd
{
public:
  param_qcd() :	  Sqcd(0),
			  num_decomps(0),
			  base_delta(-1.0f)
  {
	memset(u8_SPqcd, 0, 97);
	memset(u16_SPqcd, 0, 97*sizeof(short));
  }

  void set_delta(float delta) { base_delta = delta; }
  void set_rev_quant(int bit_depth, bool is_employing_color_transform);
  void set_irrev_quant();

  void generate(uint8_t guard_bits,
		  uint32_t decomps,bool is_reversible,
		  uint32_t max_bit_depth, bool color_transform, bool is_signed );

  int get_num_guard_bits() const;
  int get_MAGBp() const;
  int get_Kmax(int resolution, int subband) const;
  int rev_get_num_bits(int resolution, int subband) const;
  float irrev_get_delta(int resolution, int subband) const;

  void pull(grk_stepsize* stepptr, bool reversible);
  void push(grk_stepsize* stepptr, bool reversible);

private:
  uint8_t Sqcd;
  union
  {
	uint8_t u8_SPqcd[97];
	uint16_t u16_SPqcd[97];
  };
  uint32_t num_decomps;
  float base_delta;
};

}



