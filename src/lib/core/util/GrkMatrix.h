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

namespace grk
{
class GrkMatrix
{
 public:
   bool matrix_inversion_f(float* pSrcMatrix, float* pDestMatrix, uint32_t nb_compo);

 private:
   void lupInvert(float* pSrcMatrix, float* pDestMatrix, uint32_t nb_compo, uint32_t* pPermutations,
				  float* p_src_temp, float* p_dest_temp, float* p_swap_area);

   bool lupDecompose(float* matrix, uint32_t* permutations, float* p_swap_area, uint32_t nb_compo);

   void lupSolve(float* pResult, float* pMatrix, float* pVector, uint32_t* pPermutations,
				 uint32_t nb_compo, float* p_intermediate_data);
};

} // namespace grk
