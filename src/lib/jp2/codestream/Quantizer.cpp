/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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
bool Quantizer::setBandStepSizeAndBps(Subband* band, uint32_t resno,
									  uint8_t bandIndex, TileComponentCodingParams* tccp,
									  uint8_t image_precision, bool compress)
{
	/* Table E-1 - Sub-band gains */
	/* BUG_WEIRD_TWO_INVK (look for this identifier in dwt.c): */
	/* the test (!isEncoder && l_tccp->qmfbid == 0) is strongly */
	/* linked to the use of two_invK instead of invK */
	const uint32_t log2_gain = (!compress && tccp->qmfbid == 0) ? 0
							   : (band->orientation == 0)		? 0
							   : (band->orientation == 3)		? 2
																: 1;
	uint32_t numbps = image_precision + log2_gain;
	auto offset = (resno == 0) ? 0 : 3 * resno - 2;
	auto step_size = tccp->stepsizes + offset + bandIndex;
	band->stepsize =
		(float)(((1.0 + step_size->mant / 2048.0) * pow(2.0, (int32_t)(numbps - step_size->expn))));
	// printf("res=%d, band=%d, mant=%d,expn=%d, numbps=%d, step size=
	// %f\n",resno,band->orientation,step_size->mant,step_size->expn,numbps, band->stepsize);

	// see Taubman + Marcellin - Equation 10.22
	band->numbps = tccp->roishift +
				   (uint8_t)std::max<int8_t>(0, int8_t(step_size->expn + tccp->numgbits - 1U));
	// assert(band->numbps <= maxBitPlanesGRK);

	return true;
}


} // namespace grk
