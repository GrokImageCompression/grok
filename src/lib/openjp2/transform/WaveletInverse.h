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
 */

#pragma once

#include "grok_includes.h"

namespace grk {

template <typename DWT, typename T, typename STR> class WaveletInverse
{

public:
	/**
	 Inverse wavelet transform in 2-D.
	 @param tilec Tile component information (current tile)
	 */
	bool run(grk_tcd_tilecomp *tilec, uint32_t numres);
};


/**
 Inverse wavelet transform in 2-D.
 @param tilec Tile component information (current tile)
 */
template <typename DWT, typename T, typename STR> bool WaveletInverse<DWT,T,STR>::run(grk_tcd_tilecomp *tilec, uint32_t numres){
	if (tilec->numresolutions == 1U)
		return true;

	size_t l_data_size = dwt_utils::max_resolution(tilec->resolutions,
			numres) * sizeof(T);
	/* overflow check */
	if (l_data_size > SIZE_MAX) {
		GROK_ERROR("Wavelet encode: overflow");
		return false;
	}
	if (!l_data_size)
		return false;

	bool rc = true;
	uint32_t rw,rh,rw_next,rh_next;
	uint8_t cas_row,cas_col;
	uint32_t stride = tilec->x1 - tilec->x0;
	int32_t num_decomps = (int32_t) tilec->numresolutions - 1;
	int32_t *a = tile_buf_get_ptr(tilec->buf, 0, 0, 0, 0);
	grk_tcd_resolution *cur_res = tilec->resolutions;
	grk_tcd_resolution *next_res = cur_res + 1;

	T **bj_array = new T*[hardware_concurrency()];
	for (uint32_t i = 0; i < hardware_concurrency(); ++i){
		bj_array[i] = nullptr;
	}
	for (uint32_t i = 0; i < hardware_concurrency(); ++i){
		bj_array[i] = (T*)grok_aligned_malloc(l_data_size);
		if (!bj_array[i]){
			rc = false;
			goto cleanup;
		}
	}

	for (int32_t i = 0; i < numres; ++i) {

		/* width of the resolution level computed   */
		rw = cur_res->x1 - cur_res->x0;
		/* height of the resolution level computed  */
		rh = cur_res->y1 - cur_res->y0;
		// width of the next resolution level
		rw_next = next_res->x1 - next_res->x0;
		//height of the next resolution level
		rh_next = next_res->y1 - next_res->y0;

		/* 0 = non inversion on horizontal filtering 1 = inversion between low-pass and high-pass filtering */
		cas_row = cur_res->x0 & 1;
		/* 0 = non inversion on vertical filtering 1 = inversion between low-pass and high-pass filtering   */
		cas_col = cur_res->y0 & 1;

		// transform horizontal
		if (rh){
			const uint32_t s_n = rw_next;
			const uint32_t d_n = rw - rw_next;
			const uint32_t linesPerThreadH = (uint32_t)std::ceil((float)rh / hardware_concurrency());

			std::vector< std::future<int> > results;
			for(size_t i = 0; i < hardware_concurrency(); ++i) {
				uint64_t index = i;
				results.emplace_back(
					Scheduler::g_tp->enqueue([this, index, bj_array,a,
												 stride, rw,rh,
												 d_n, s_n, cas_row,
												 linesPerThreadH] {
					DWT wavelet;
					for (auto m = index * linesPerThreadH;
							m < std::min<uint32_t>((index+1)*linesPerThreadH, rh); ++m) {
						T *bj = bj_array[index];
						T *aj = a + m * stride;
						memcpy(bj,aj,rw << 2);
						wavelet.interleave_h(bj, aj, d_n, s_n, cas_row);
						wavelet.decode_line(bj, d_n, s_n, cas_row);
					}
						return 0;
					})
				);
			}
			for(auto && result: results){
				result.get();
			}
		}


		// transform vertical
		if (rw) {
			const uint32_t linesPerThreadV = (uint32_t)(std::ceil((float)rw / hardware_concurrency()));
			const uint32_t s_n = rh_next;
			const uint32_t d_n = rh - rh_next;


			std::vector< std::future<int> > results;
			for(size_t i = 0; i < hardware_concurrency(); ++i) {
				uint64_t index = i;
				results.emplace_back(
					Scheduler::g_tp->enqueue([this, index, bj_array,a,
												 stride, rw,rh,
												 d_n, s_n, cas_col,
												 linesPerThreadV] {
						DWT wavelet;
						for (auto m = index * linesPerThreadV;
								m < std::min<uint32_t>((index+1)*linesPerThreadV, rw); ++m) {
							int32_t *bj = bj_array[index];
							int32_t *aj = a + m;
							for (uint32_t k = 0; k < rh; ++k) {
								bj[k] = aj[k * stride];
							}
							wavelet.interleave_v(bj, aj, d_n, s_n, stride, cas_col);
							wavelet.decode_line(bj, d_n, s_n, cas_col);
						}
						return 0;
					})
				);
			}
			for(auto && result: results){
				result.get();
			}
		}
		cur_res = next_res;
		next_res++;
	}
cleanup:
	for (uint32_t i = 0; i < hardware_concurrency(); ++i)
		grok_aligned_free(bj_array[i]);
	delete[] bj_array;
	return rc;
}

}
