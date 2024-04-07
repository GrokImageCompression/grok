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

#include <grk_includes.h>

namespace grk
{
/**
 * Matrix inversion.
 */
bool GrkMatrix::matrix_inversion_f(float* pSrcMatrix, float* pDestMatrix, uint32_t nb_compo)
{
   uint8_t* data = nullptr;
   uint32_t permutation_size = nb_compo * (uint32_t)sizeof(uint32_t);
   uint32_t swap_size = nb_compo * (uint32_t)sizeof(float);
   uint32_t total_size = permutation_size + 3 * swap_size;
   uint32_t* lPermutations = nullptr;
   float* double_data = nullptr;

   data = (uint8_t*)grk_malloc(total_size);
   if(data == 0)
	  return false;

   lPermutations = (uint32_t*)data;
   double_data = (float*)(data + permutation_size);
   memset(lPermutations, 0, permutation_size);

   if(!lupDecompose(pSrcMatrix, lPermutations, double_data, nb_compo))
   {
	  grk_free(data);
	  return false;
   }

   lupInvert(pSrcMatrix, pDestMatrix, nb_compo, lPermutations, double_data, double_data + nb_compo,
			 double_data + 2 * nb_compo);
   grk_free(data);

   return true;
}
bool GrkMatrix::lupDecompose(float* matrix, uint32_t* permutations, float* p_swap_area,
							 uint32_t nb_compo)
{
   uint32_t* tmpPermutations = permutations;
   uint32_t* dstPermutations;
   uint32_t k2 = 0, t;
   float temp;
   uint32_t i, j, k;
   uint32_t lLastColum = nb_compo - 1;
   uint32_t lSwapSize = nb_compo * (uint32_t)sizeof(float);
   auto lTmpMatrix = matrix;
   uint32_t offset = 1;
   uint32_t lStride = nb_compo - 1;

   /*initialize permutations */
   for(i = 0; i < nb_compo; ++i)
	  *tmpPermutations++ = i;

   /* now make a pivot with column switch */
   tmpPermutations = permutations;
   for(k = 0; k < lLastColum; ++k)
   {
	  float p = 0.0;

	  /* take the middle element */
	  auto lColumnMatrix = lTmpMatrix + k;

	  /* make permutation with the biggest value in the column */
	  for(i = k; i < nb_compo; ++i)
	  {
		 temp = ((*lColumnMatrix > 0) ? *lColumnMatrix : -(*lColumnMatrix));
		 if(temp > p)
		 {
			p = temp;
			k2 = i;
		 }
		 /* next line */
		 lColumnMatrix += nb_compo;
	  }

	  /* a whole rest of 0 -> non singular */
	  if(p == 0.0)
		 return false;

	  /* should we permute ? */
	  if(k2 != k)
	  {
		 /*exchange of line */
		 /* k2 > k */
		 dstPermutations = tmpPermutations + k2 - k;
		 /* swap indices */
		 t = *tmpPermutations;
		 *tmpPermutations = *dstPermutations;
		 *dstPermutations = t;

		 /* and swap entire line. */
		 lColumnMatrix = lTmpMatrix + (k2 - k) * nb_compo;
		 memcpy(p_swap_area, lColumnMatrix, lSwapSize);
		 memcpy(lColumnMatrix, lTmpMatrix, lSwapSize);
		 memcpy(lTmpMatrix, p_swap_area, lSwapSize);
	  }

	  /* now update data in the rest of the line and line after */
	  auto lDestMatrix = lTmpMatrix + k;
	  lColumnMatrix = lDestMatrix + nb_compo;
	  /* take the middle element */
	  temp = *(lDestMatrix++);

	  /* now compute up data (i.e. coeff up of the diagonal). */
	  for(i = offset; i < nb_compo; ++i)
	  {
		 /*lColumnMatrix; */
		 /* divide the lower column elements by the diagonal value */

		 /* matrix[i][k] /= matrix[k][k]; */
		 /* p = matrix[i][k] */
		 p = *lColumnMatrix / temp;
		 *(lColumnMatrix++) = p;

		 for(j = /* k + 1 */ offset; j < nb_compo; ++j)
			/* matrix[i][j] -= matrix[i][k] * matrix[k][j]; */
			*(lColumnMatrix++) -= p * (*(lDestMatrix++));

		 /* come back to the k+1th element */
		 lDestMatrix -= lStride;
		 /* go to kth element of the next line */
		 lColumnMatrix += k;
	  }

	  /* offset is now k+2 */
	  ++offset;
	  /* 1 element less for stride */
	  --lStride;
	  /* next line */
	  lTmpMatrix += nb_compo;
	  /* next permutation element */
	  ++tmpPermutations;
   }
   return true;
}
void GrkMatrix::lupSolve(float* pResult, float* pMatrix, float* pVector, uint32_t* pPermutations,
						 uint32_t nb_compo, float* p_intermediate_data)
{
   int32_t k;
   uint32_t i, j;
   float sum;
   uint32_t lStride = nb_compo + 1;
   float* lCurrentPtr;
   float* lIntermediatePtr;
   float* lDestPtr;
   float* lTmpMatrix;
   float* lLineMatrix = pMatrix;
   float* lBeginPtr = pResult + nb_compo - 1;
   float* lGeneratedData;
   uint32_t* lCurrentPermutationPtr = pPermutations;

   lIntermediatePtr = p_intermediate_data;
   lGeneratedData = p_intermediate_data + nb_compo - 1;

   for(i = 0; i < nb_compo; ++i)
   {
	  sum = 0.0;
	  lCurrentPtr = p_intermediate_data;
	  lTmpMatrix = lLineMatrix;
	  for(j = 1; j <= i; ++j)
		 /* sum += matrix[i][j-1] * y[j-1]; */
		 sum += (*(lTmpMatrix++)) * (*(lCurrentPtr++));

	  /*y[i] = pVector[pPermutations[i]] - sum; */
	  *(lIntermediatePtr++) = pVector[*(lCurrentPermutationPtr++)] - sum;
	  lLineMatrix += nb_compo;
   }

   /* we take the last point of the matrix */
   lLineMatrix = pMatrix + nb_compo * nb_compo - 1;

   /* and we take after the last point of the destination vector */
   lDestPtr = pResult + nb_compo;

   assert(nb_compo != 0);
   for(k = (int32_t)nb_compo - 1; k != -1; --k)
   {
	  sum = 0.0;
	  lTmpMatrix = lLineMatrix;
	  float u = *(lTmpMatrix++);
	  lCurrentPtr = lDestPtr--;
	  for(j = (uint32_t)(k + 1); j < nb_compo; ++j)
		 /* sum += matrix[k][j] * x[j] */
		 sum += (*(lTmpMatrix++)) * (*(lCurrentPtr++));

	  /*x[k] = (y[k] - sum) / u; */
	  *(lBeginPtr--) = (*(lGeneratedData--) - sum) / u;
	  lLineMatrix -= lStride;
   }
}
void GrkMatrix::lupInvert(float* pSrcMatrix, float* pDestMatrix, uint32_t nb_compo,
						  uint32_t* pPermutations, float* p_src_temp, float* p_dest_temp,
						  float* p_swap_area)
{
   uint32_t j, i;
   auto lLineMatrix = pDestMatrix;
   uint32_t lSwapSize = nb_compo * (uint32_t)sizeof(float);

   for(j = 0; j < nb_compo; ++j)
   {
	  auto lCurrentPtr = lLineMatrix++;
	  memset(p_src_temp, 0, lSwapSize);
	  p_src_temp[j] = 1.0;
	  lupSolve(p_dest_temp, pSrcMatrix, p_src_temp, pPermutations, nb_compo, p_swap_area);

	  for(i = 0; i < nb_compo; ++i)
	  {
		 *(lCurrentPtr) = p_dest_temp[i];
		 lCurrentPtr += nb_compo;
	  }
   }
}

} // namespace grk
