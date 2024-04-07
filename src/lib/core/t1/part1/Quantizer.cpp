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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "grk_includes.h"

namespace grk
{
Quantizer::Quantizer(bool reversible, uint8_t guard_bits)
	: Sqcd((uint8_t)(guard_bits << 5)), num_decomps(0), isReversible(reversible)
{
   memset(u8_SPqcd, 0, GRK_J2K_MAXBANDS);
   memset(u16_SPqcd, 0, GRK_J2K_MAXBANDS * sizeof(short));
}

uint32_t Quantizer::get_num_guard_bits() const
{
   return uint32_t(Sqcd >> 5U);
}

/**
 * Note:
 *
 * Lossless
 * u8_SPqcd[] stores expn in upper 5 bits (mantissa is zero)
 *
 * Lossy
 * u16_SPqcd[bn] stores expn in upper 5 bits and mantissa
 * in lower 11 bits
 *
 *
 */
void Quantizer::pull(grk_stepsize* stepptr)
{
   uint32_t numbands = 3 * num_decomps + 1;
   for(uint32_t bn = 0; bn < numbands; bn++)
   {
	  auto step = stepptr + bn;
	  if(isReversible)
	  {
		 step->expn = (uint8_t)(u8_SPqcd[bn] >> 3);
		 step->mant = 0;
	  }
	  else
	  {
		 step->expn = (uint8_t)(u16_SPqcd[bn] >> 11);
		 step->mant = (uint16_t)(u16_SPqcd[bn] & 0x7FF);
	  }
   }
}
void Quantizer::push(grk_stepsize* stepptr)
{
   uint32_t numbands = 3 * num_decomps + 1;
   for(uint32_t bn = 0; bn < numbands; bn++)
   {
	  auto step = stepptr + bn;
	  if(isReversible)
		 u8_SPqcd[bn] = (uint8_t)(step->expn << 3);
	  else
		 u16_SPqcd[bn] = (uint16_t)((step->expn << 11) + step->mant);
   }
}
void Quantizer::generate(uint32_t decomps, uint32_t max_bit_depth,
						 [[maybe_unused]] bool color_transform, [[maybe_unused]] bool is_signed)
{
   num_decomps = decomps;

   uint32_t numresolutions = decomps + 1;
   uint32_t numbands = 3 * numresolutions - 2;
   for(uint32_t bandno = 0; bandno < numbands; bandno++)
   {
	  uint32_t resno = (bandno == 0) ? 0 : ((bandno - 1) / 3 + 1);
	  uint8_t orient = (uint8_t)((bandno == 0) ? 0 : ((bandno - 1) % 3 + 1));
	  uint32_t level = numresolutions - 1 - resno;
	  uint32_t gain =
		  (!isReversible) ? 0 : ((orient == 0) ? 0 : (((orient == 1) || (orient == 2)) ? 1 : 2));

	  double stepsize = 1.0;
	  if(!isReversible)
		 stepsize = (1 << (gain)) / T1::getnorm(level, orient, false);
	  uint32_t step = (uint32_t)floor(stepsize * 8192.0);
	  int32_t p, n;
	  p = floorlog2(step) - 13;
	  n = 11 - floorlog2(step);
	  uint32_t mant = (n < 0 ? step >> -n : step << n) & 0x7ff;
	  int32_t expn = (int32_t)(max_bit_depth + gain) - p;
	  assert(expn >= 0);
	  if(isReversible)
		 u8_SPqcd[bandno] = (uint8_t)(expn << 3);
	  else
		 u16_SPqcd[bandno] = (uint16_t)((uint32_t)(expn << 11) + mant);
   }
}

// no-op
bool Quantizer::write([[maybe_unused]] BufferedStream* stream)
{
   return true;
}

} // namespace grk
