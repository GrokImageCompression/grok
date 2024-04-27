/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This source code is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 *    This source code incorporates work covered by the following license:
 *    Please see the LICENSE file in the root directory for details.
 */
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

#define _USE_MATH_DEFINES
#include "grk_includes.h"
#include "QuantizerOJPH.h"

namespace ojph
{
class sqrt_energy_gains
{
 public:
   static float get_gain_l(uint32_t num_decomp, bool reversible);
   static float get_gain_h(uint32_t num_decomp, bool reversible);

 private:
   static const float gain_9x7_l[34];
   static const float gain_9x7_h[34];
   static const float gain_5x3_l[34];
   static const float gain_5x3_h[34];
};

float sqrt_energy_gains::get_gain_l(uint32_t num_decomp, bool reversible)
{
   return reversible ? gain_5x3_l[num_decomp] : gain_9x7_l[num_decomp];
}
float sqrt_energy_gains::get_gain_h(uint32_t num_decomp, bool reversible)
{
   return reversible ? gain_5x3_h[num_decomp] : gain_9x7_h[num_decomp];
}

//////////////////////////////////////////////////////////////////////////
const float sqrt_energy_gains::gain_9x7_l[34] = {
	1.0000e+00f, 1.4021e+00f, 2.0304e+00f, 2.9012e+00f, 4.1153e+00f, 5.8245e+00f, 8.2388e+00f,
	1.1652e+01f, 1.6479e+01f, 2.3304e+01f, 3.2957e+01f, 4.6609e+01f, 6.5915e+01f, 9.3217e+01f,
	1.3183e+02f, 1.8643e+02f, 2.6366e+02f, 3.7287e+02f, 5.2732e+02f, 7.4574e+02f, 1.0546e+03f,
	1.4915e+03f, 2.1093e+03f, 2.9830e+03f, 4.2185e+03f, 5.9659e+03f, 8.4371e+03f, 1.1932e+04f,
	1.6874e+04f, 2.3864e+04f, 3.3748e+04f, 4.7727e+04f, 6.7496e+04f, 9.5454e+04f};
const float sqrt_energy_gains::gain_9x7_h[34] = {
	1.4425e+00f, 1.9669e+00f, 2.8839e+00f, 4.1475e+00f, 5.8946e+00f, 8.3472e+00f, 1.1809e+01f,
	1.6701e+01f, 2.3620e+01f, 3.3403e+01f, 4.7240e+01f, 6.6807e+01f, 9.4479e+01f, 1.3361e+02f,
	1.8896e+02f, 2.6723e+02f, 3.7792e+02f, 5.3446e+02f, 7.5583e+02f, 1.0689e+03f, 1.5117e+03f,
	2.1378e+03f, 3.0233e+03f, 4.2756e+03f, 6.0467e+03f, 8.5513e+03f, 1.2093e+04f, 1.7103e+04f,
	2.4187e+04f, 3.4205e+04f, 4.8373e+04f, 6.8410e+04f, 9.6747e+04f, 1.3682e+05f};
const float sqrt_energy_gains::gain_5x3_l[34] = {
	1.0000e+00f, 1.2247e+00f, 1.3229e+00f, 1.5411e+00f, 1.7139e+00f, 1.9605e+00f, 2.2044e+00f,
	2.5047e+00f, 2.8277e+00f, 3.2049e+00f, 3.6238e+00f, 4.1033e+00f, 4.6423e+00f, 5.2548e+00f,
	5.9462e+00f, 6.7299e+00f, 7.6159e+00f, 8.6193e+00f, 9.7544e+00f, 1.1039e+01f, 1.2493e+01f,
	1.4139e+01f, 1.6001e+01f, 1.8108e+01f, 2.0493e+01f, 2.3192e+01f, 2.6246e+01f, 2.9702e+01f,
	3.3614e+01f, 3.8041e+01f, 4.3051e+01f, 4.8721e+01f, 5.5138e+01f, 6.2399e+01f};
const float sqrt_energy_gains::gain_5x3_h[34] = {
	1.0458e+00f, 1.3975e+00f, 1.4389e+00f, 1.7287e+00f, 1.8880e+00f, 2.1841e+00f, 2.4392e+00f,
	2.7830e+00f, 3.1341e+00f, 3.5576e+00f, 4.0188e+00f, 4.5532e+00f, 5.1494e+00f, 5.8301e+00f,
	6.5963e+00f, 7.4663e+00f, 8.4489e+00f, 9.5623e+00f, 1.0821e+01f, 1.2247e+01f, 1.3860e+01f,
	1.5685e+01f, 1.7751e+01f, 2.0089e+01f, 2.2735e+01f, 2.5729e+01f, 2.9117e+01f, 3.2952e+01f,
	3.7292e+01f, 4.2203e+01f, 4.7761e+01f, 5.4051e+01f, 6.1170e+01f, 6.9226e+01f};

//////////////////////////////////////////////////////////////////////////
// static
class bibo_gains
{
 public:
   static float get_bibo_gain_l(uint32_t num_decomp, bool reversible)
   {
	  return reversible ? gain_5x3_l[num_decomp] : gain_9x7_l[num_decomp];
   }
   static float get_bibo_gain_h(uint32_t num_decomp, bool reversible)
   {
	  return reversible ? gain_5x3_h[num_decomp] : gain_9x7_h[num_decomp];
   }

 private:
   static const float gain_9x7_l[34];
   static const float gain_9x7_h[34];
   static const float gain_5x3_l[34];
   static const float gain_5x3_h[34];
};

//////////////////////////////////////////////////////////////////////////
const float bibo_gains::gain_9x7_l[34] = {
	1.0000e+00f, 1.3803e+00f, 1.3328e+00f, 1.3067e+00f, 1.3028e+00f, 1.3001e+00f, 1.2993e+00f,
	1.2992e+00f, 1.2992e+00f, 1.2992e+00f, 1.2992e+00f, 1.2992e+00f, 1.2992e+00f, 1.2992e+00f,
	1.2992e+00f, 1.2992e+00f, 1.2992e+00f, 1.2992e+00f, 1.2992e+00f, 1.2992e+00f, 1.2992e+00f,
	1.2992e+00f, 1.2992e+00f, 1.2992e+00f, 1.2992e+00f, 1.2992e+00f, 1.2992e+00f, 1.2992e+00f,
	1.2992e+00f, 1.2992e+00f, 1.2992e+00f, 1.2992e+00f, 1.2992e+00f, 1.2992e+00f};
const float bibo_gains::gain_9x7_h[34] = {
	1.2976e+00f, 1.3126e+00f, 1.2757e+00f, 1.2352e+00f, 1.2312e+00f, 1.2285e+00f, 1.2280e+00f,
	1.2278e+00f, 1.2278e+00f, 1.2278e+00f, 1.2278e+00f, 1.2278e+00f, 1.2278e+00f, 1.2278e+00f,
	1.2278e+00f, 1.2278e+00f, 1.2278e+00f, 1.2278e+00f, 1.2278e+00f, 1.2278e+00f, 1.2278e+00f,
	1.2278e+00f, 1.2278e+00f, 1.2278e+00f, 1.2278e+00f, 1.2278e+00f, 1.2278e+00f, 1.2278e+00f,
	1.2278e+00f, 1.2278e+00f, 1.2278e+00f, 1.2278e+00f, 1.2278e+00f, 1.2278e+00f};
const float bibo_gains::gain_5x3_l[34] = {
	1.0000e+00f, 1.5000e+00f, 1.6250e+00f, 1.6875e+00f, 1.6963e+00f, 1.7067e+00f, 1.7116e+00f,
	1.7129e+00f, 1.7141e+00f, 1.7145e+00f, 1.7151e+00f, 1.7152e+00f, 1.7155e+00f, 1.7155e+00f,
	1.7156e+00f, 1.7156e+00f, 1.7156e+00f, 1.7156e+00f, 1.7156e+00f, 1.7156e+00f, 1.7156e+00f,
	1.7156e+00f, 1.7156e+00f, 1.7156e+00f, 1.7156e+00f, 1.7156e+00f, 1.7156e+00f, 1.7156e+00f,
	1.7156e+00f, 1.7156e+00f, 1.7156e+00f, 1.7156e+00f, 1.7156e+00f, 1.7156e+00f};
const float bibo_gains::gain_5x3_h[34] = {
	2.0000e+00f, 2.5000e+00f, 2.7500e+00f, 2.8047e+00f, 2.8198e+00f, 2.8410e+00f, 2.8558e+00f,
	2.8601e+00f, 2.8628e+00f, 2.8656e+00f, 2.8662e+00f, 2.8667e+00f, 2.8669e+00f, 2.8670e+00f,
	2.8671e+00f, 2.8671e+00f, 2.8671e+00f, 2.8671e+00f, 2.8671e+00f, 2.8671e+00f, 2.8671e+00f,
	2.8671e+00f, 2.8671e+00f, 2.8671e+00f, 2.8671e+00f, 2.8671e+00f, 2.8671e+00f, 2.8671e+00f,
	2.8671e+00f, 2.8671e+00f, 2.8671e+00f, 2.8671e+00f, 2.8671e+00f, 2.8671e+00f};

QuantizerOJPH::QuantizerOJPH(bool reversible, uint8_t guard_bits)
	: Quantizer(reversible, guard_bits), base_delta(-1.0f)
{}
void QuantizerOJPH::generate(uint32_t decomps, uint32_t max_bit_depth, bool color_transform,
							 bool is_signed)
{
   num_decomps = decomps;
   if(isReversible)
   {
	  set_rev_quant(max_bit_depth, color_transform);
   }
   else
   {
	  if(base_delta == -1.0f)
		 base_delta = 1.0f / (float)(1 << (max_bit_depth + is_signed));
	  set_irrev_quant();
   }
}
void QuantizerOJPH::set_rev_quant(uint32_t bit_depth, bool is_employing_color_transform)
{
   int B = (int)bit_depth;
   B += is_employing_color_transform ? 1 : 0; // 1 bit for RCT
   uint32_t s = 0;
   float bibo_l = bibo_gains::get_bibo_gain_l(num_decomps, true);
   // we leave some leeway for numerical error by multiplying by 1.1f
   int X = (int)ceil(log(bibo_l * bibo_l * 1.1f) / M_LN2);
   u8_SPqcd[s++] = (uint8_t)((B + X) << 3);
   for(int d = (int32_t)num_decomps - 1; d >= 0; --d)
   {
	  bibo_l = bibo_gains::get_bibo_gain_l((uint32_t)(d + 1), true);
	  float bibo_h = bibo_gains::get_bibo_gain_h((uint32_t)d, true);
	  X = (int)ceil(log(bibo_h * bibo_l * 1.1f) / M_LN2);
	  u8_SPqcd[s++] = (uint8_t)((B + X) << 3);
	  u8_SPqcd[s++] = (uint8_t)((B + X) << 3);
	  X = (int)ceil(log(bibo_h * bibo_h * 1.1f) / M_LN2);
	  u8_SPqcd[s++] = (uint8_t)((B + X) << 3);
   }
}
void QuantizerOJPH::set_irrev_quant()
{
   int s = 0;
   float gain_l = sqrt_energy_gains::get_gain_l(num_decomps, false);
   float delta_b = base_delta / (gain_l * gain_l);
   int exp = 0, mantissa;
   while(delta_b < 1.0f)
   {
	  exp++;
	  delta_b *= 2.0f;
   }
   // with rounding, there is a risk of becoming equal to 1<<12
   // but that should not happen in reality
   mantissa = (int)round(delta_b * (float)(1 << 11)) - (1 << 11);
   mantissa = mantissa < (1 << 11) ? mantissa : 0x7FF;
   u16_SPqcd[s++] = (uint16_t)((exp << 11) | mantissa);
   for(uint32_t d = num_decomps; d > 0; --d)
   {
	  float gain_l = sqrt_energy_gains::get_gain_l(d, false);
	  float gain_h = sqrt_energy_gains::get_gain_h(d - 1, false);

	  delta_b = base_delta / (gain_l * gain_h);

	  int exp = 0, mantissa;
	  while(delta_b < 1.0f)
	  {
		 exp++;
		 delta_b *= 2.0f;
	  }
	  mantissa = (int)round(delta_b * (float)(1 << 11)) - (1 << 11);
	  mantissa = mantissa < (1 << 11) ? mantissa : 0x7FF;
	  u16_SPqcd[s++] = (uint16_t)((exp << 11) | mantissa);
	  u16_SPqcd[s++] = (uint16_t)((exp << 11) | mantissa);

	  delta_b = base_delta / (gain_h * gain_h);

	  exp = 0;
	  while(delta_b < 1)
	  {
		 exp++;
		 delta_b *= 2.0f;
	  }
	  mantissa = (int)round(delta_b * (float)(1 << 11)) - (1 << 11);
	  mantissa = mantissa < (1 << 11) ? mantissa : 0x7FF;
	  u16_SPqcd[s++] = (uint16_t)((exp << 11) | mantissa);
   }
}
uint32_t QuantizerOJPH::get_MAGBp() const
{
   uint32_t B = 0;
   uint32_t irrev = Sqcd & 0x1F;
   if(irrev == 0) // reversible
	  for(uint32_t i = 0; i < 3 * num_decomps + 1; ++i)
		 B = (std::max)(B, uint32_t(u8_SPqcd[i] >> 3U) + get_num_guard_bits() - 1U);
   else if(irrev == 2) // scalar expounded
	  for(uint32_t i = 0; i < 3 * num_decomps + 1; ++i)
	  {
		 uint32_t nb = num_decomps - (i ? (i - 1) / 3 : 0); // decomposition level
		 B = (std::max)(B, uint32_t(u16_SPqcd[i] >> 11U) + get_num_guard_bits() - nb);
	  }
   else
	  assert(0);

   return B;
}
bool QuantizerOJPH::write(grk::BufferedStream* stream)
{
   // marker size excluding header
   uint16_t Lcap = 8;
   uint32_t Pcap = 0x00020000; // for jph, Pcap^15 must be set, the 15th MSB
   uint16_t Ccap[32]; // a maximum of 32
   memset(Ccap, 0, sizeof(Ccap));

   if(isReversible)
	  Ccap[0] &= 0xFFDF;
   else
	  Ccap[0] |= 0x0020;
   Ccap[0] &= 0xFFE0;

   uint32_t Bp = 0;
   uint32_t B = get_MAGBp();
   if(B <= 8)
	  Bp = 0;
   else if(B < 28)
	  Bp = B - 8;
   else if(B < 48)
	  Bp = 13 + (B >> 2);
   else
	  Bp = 31;
   Ccap[0] = (uint16_t)(Ccap[0] | Bp);

   /* CAP */
   if(!stream->writeShort(grk::J2K_CAP))
   {
	  return false;
   }

   /* L_CAP */
   if(!stream->writeShort(Lcap))
	  return false;
   /* PCAP */
   if(!stream->writeInt(Pcap))
	  return false;
   /* CCAP */
   if(!stream->writeShort(Ccap[0]))
	  return false;

   return true;
}

} // namespace ojph
