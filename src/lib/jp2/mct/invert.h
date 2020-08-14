/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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

namespace grk {

/**
 @file invert.h
 @brief Implementation of the matrix inversion

 The function in INVERT.H compute a matrix inversion with a LUP method
 */

/** @defgroup INVERT INVERT - Implementation of a matrix inversion */
/*@{*/
/** @name Exported functions */
/*@{*/
/* ----------------------------------------------------------------------- */

/**
 * Calculate a n x n double matrix inversion with a LUP method.
 * Data is aligned, rows after rows (or columns after columns).
 * The function does not take ownership of any memory block,
 * data must be freed by the user.
 *
 * @param pSrcMatrix	the matrix to invert.
 * @param pDestMatrix	data to store the inverted matrix.
 * @param n size of the matrix
 * @return true if the inversion is successful, false if the matrix is singular.
 */
bool matrix_inversion_f(float *pSrcMatrix, float *pDestMatrix,
		uint32_t n);
/* ----------------------------------------------------------------------- */
/*@}*/

/*@}*/

}
