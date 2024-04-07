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

#pragma once

namespace grk
{
/**
 * Quantization stepsize
 */
struct grk_stepsize
{
   grk_stepsize() : expn(0), mant(0) {}
   /** exponent - 5 bits */
   uint8_t expn;
   /** mantissa  -11 bits */
   uint16_t mant;
};

struct Quantizer
{
 public:
   Quantizer(bool reversible, uint8_t guard_bits);
   virtual ~Quantizer() = default;
   // for compress
   void pull(grk_stepsize* stepptr);
   // for decompress
   void push(grk_stepsize* stepptr);
   virtual void generate(uint32_t decomps, uint32_t max_bit_depth, bool color_transform,
						 bool is_signed);
   virtual bool write(BufferedStream* stream);

 protected:
   uint32_t get_num_guard_bits() const;
   uint8_t Sqcd;
   union
   {
	  uint8_t u8_SPqcd[97];
	  uint16_t u16_SPqcd[97];
   };
   uint32_t num_decomps;
   bool isReversible;
};

} // namespace grk
