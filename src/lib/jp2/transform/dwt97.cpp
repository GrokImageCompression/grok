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

#include "CPUArch.h"
#include "T1Decoder.h"
#include <atomic>
#include "testing.h"
#include "dwt97.h"

namespace grk {

#define GROK_S(i) a[(i)<<1]
#define GROK_D(i) a[(1+((i)<<1))]
#define GROK_S_(i) ((i)<0?GROK_S(0):((i)>=s_n?GROK_S(s_n-1):GROK_S(i)))
#define GROK_D_(i) ((i)<0?GROK_D(0):((i)>=d_n?GROK_D(d_n-1):GROK_D(i)))
#define GROK_SS_(i) ((i)<0?GROK_S(0):((i)>=d_n?GROK_S(d_n-1):GROK_S(i)))
#define GROK_DD_(i) ((i)<0?GROK_D(0):((i)>=s_n?GROK_D(s_n-1):GROK_D(i)))

#if 0
static const float dwt_alpha = 1.586134342f; /*  12994 */
static const float dwt_beta = 0.052980118f; /*    434 */
static const float dwt_gamma = -0.882911075f; /*  -7233 */
static const float dwt_delta = -0.443506852f; /*  -3633 */
static const float dwt_K = 1.230174105f; /*  10078 */
static const float dwt_c13318 = 1.625732422f;
#endif

/***************************************************************************************

 9/7 Synthesis Wavelet Transform

 *****************************************************************************************/
/* <summary>                             */
/* Forward 9-7 wavelet transform in 1-D. */
/* </summary>                            */
void dwt97::encode_line(int32_t* GRK_RESTRICT a, int32_t d_n, int32_t s_n, uint8_t cas) {
	if (!cas) {
	  if ((d_n > 0) || (s_n > 1)) { /* NEW :  CASE ONE ELEMENT */
		for (int32_t i = 0; i < d_n; i++)
			GROK_D(i)-= int_fix_mul(GROK_S_(i) + GROK_S_(i + 1), 12994);
		for (int32_t i = 0; i < s_n; i++)
			GROK_S(i) -= int_fix_mul(GROK_D_(i - 1) + GROK_D_(i), 434);
		for (int32_t i = 0; i < d_n; i++)
			GROK_D(i) += int_fix_mul(GROK_S_(i) + GROK_S_(i + 1), 7233);
		for (int32_t i = 0; i < s_n; i++)
			GROK_S(i) += int_fix_mul(GROK_D_(i - 1) + GROK_D_(i), 3633);
		for (int32_t i = 0; i < d_n; i++)
			GROK_D(i) = int_fix_mul(GROK_D(i), 5039);
		for (int32_t i = 0; i < s_n; i++)
			GROK_S(i) = int_fix_mul(GROK_S(i), 6659);
	  }
	}
	else {
		if ((s_n > 0) || (d_n > 1)) { /* NEW :  CASE ONE ELEMENT */
			for (int32_t i = 0; i < d_n; i++)
				GROK_S(i) -= int_fix_mul(GROK_DD_(i) + GROK_DD_(i - 1), 12994);
			for (int32_t i = 0; i < s_n; i++)
				GROK_D(i) -= int_fix_mul(GROK_SS_(i) + GROK_SS_(i + 1), 434);
			for (int32_t i = 0; i < d_n; i++)
				GROK_S(i) += int_fix_mul(GROK_DD_(i) + GROK_DD_(i - 1), 7233);
			for (int32_t i = 0; i < s_n; i++)
				GROK_D(i) += int_fix_mul(GROK_SS_(i) + GROK_SS_(i + 1), 3633);
			for (int32_t i = 0; i < d_n; i++)
				GROK_S(i) = int_fix_mul(GROK_S(i), 5039);
			for (int32_t i = 0; i < s_n; i++)
				GROK_D(i) = int_fix_mul(GROK_D(i), 6659);
		}
	}
}


}
