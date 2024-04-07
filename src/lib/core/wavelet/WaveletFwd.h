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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#pragma once

namespace grk
{
class dwt53
{
 public:
   void encode_and_deinterleave_v(int32_t* arrayIn, int32_t* tmpIn, uint32_t height, bool even,
								  uint32_t stride_width, uint32_t cols);

   void encode_and_deinterleave_h_one_row(int32_t* rowIn, int32_t* tmpIn, uint32_t width,
										  bool even);
};

class dwt97
{
 public:
   void encode_and_deinterleave_v(float* arrayIn, float* tmpIn, uint32_t height, bool even,
								  uint32_t stride_width, uint32_t cols);

   void encode_and_deinterleave_h_one_row(float* rowIn, float* tmpIn, uint32_t width, bool even);

 private:
   void grk_v8dwt_encode_step1(float* fw, uint32_t end, const float cst);
   void grk_v8dwt_encode_step2(float* fl, float* fw, uint32_t end, uint32_t m, float cst);
   void encode_step2(float* fl, float* fw, uint32_t end, uint32_t m, float c);

   void encode_step1_combined(float* fw, uint32_t iters_c1, uint32_t iters_c2, const float c1,
							  const float c2);
   void encode_1_real(float* w, int32_t dn, int32_t sn, int32_t parity);
};

class WaveletFwdImpl
{
 public:
   virtual ~WaveletFwdImpl() = default;
   bool compress(TileComponent* tile_comp, uint8_t qmfbid);

 private:
   template<typename T, typename DWT>
   bool encode_procedure(TileComponent* tilec);
};

} // namespace grk
