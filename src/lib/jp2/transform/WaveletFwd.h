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

#include "grk_includes.h"

namespace grk {

template <typename DWT> class WaveletForward
{

public:
	/**
	 Forward wavelet transform in 2-D.
	 @param tilec Tile component information (current tile)
	 */
	bool run(TileComponent *tilec);

	/* <summary>                             */
	/* Forward lazy transform (vertical).    */
	/* </summary>                            */
	void deinterleave_v(int32_t *a, int32_t *b, uint32_t d_n, uint32_t s_n,
			uint32_t stride, int32_t cas);


	/* <summary>			                 */
	/* Forward lazy transform (horizontal).  */
	/* </summary>                            */
	void deinterleave_h(int32_t *a, int32_t *b, uint32_t d_n, uint32_t s_n,
			int32_t cas) ;
};


/* <summary>                             */
/* Forward lazy transform (vertical).    */
/* </summary>                            */
template <typename DWT> void WaveletForward<DWT>::deinterleave_v(int32_t *a, int32_t *b, uint32_t d_n, uint32_t s_n,
		uint32_t stride, int32_t cas) {
	uint32_t i = s_n;
	int32_t *dest = b;
	int32_t *src = a + cas;

	while (i--) {
		*dest = *src;
		dest += stride;
		src += 2;
	}

	dest = b + s_n * stride;
	src = a + 1 - cas;

	i = d_n;
	while (i--) {
		*dest = *src;
		dest += stride;
		src += 2;
	}
}

/* <summary>			                 */
/* Forward lazy transform (horizontal).  */
/* </summary>                            */
template <typename DWT> void WaveletForward<DWT>::deinterleave_h(int32_t *a, int32_t *b, uint32_t d_n, uint32_t s_n,
		int32_t cas) {
	int32_t *dest = b;
	int32_t *src = a + cas;

	for (uint32_t i = 0; i < s_n; ++i) {
		*dest++ = *src;
		src += 2;
	}

	dest = b + s_n;
	src = a + 1 - cas;

	for (uint32_t i = 0; i < d_n; ++i) {
		*dest++ = *src;
		src += 2;
	}
}


/**
 Forward wavelet transform in 2-D.
 @param tilec Tile component information (current tile)
 */
template <typename DWT> bool WaveletForward<DWT>::run(TileComponent *tilec){
	if (tilec->numresolutions == 1U)
		return true;

	size_t l_data_size = max_resolution(tilec->resolutions,
			tilec->numresolutions) * sizeof(int32_t);
	/* overflow check */
	if (l_data_size > SIZE_MAX) {
		GRK_ERROR("WaveletFwdImpl compress: overflow");
		return false;
	}
	if (!l_data_size)
		return false;

	bool rc = true;
	uint32_t rw,rh,rw_next,rh_next;
	uint8_t cas_row,cas_col;
	uint32_t stride = tilec->getBuffer()->stride();
	uint32_t num_decomps = (uint32_t) (tilec->numresolutions - 1);
	auto a = tilec->getBuffer()->ptr();
	auto cur_res = tilec->resolutions + num_decomps;
	auto next_res = cur_res - 1;

	auto bj_array = new int32_t*[ThreadPool::get()->num_threads()];
	for (uint32_t i = 0; i < ThreadPool::get()->num_threads(); ++i){
		bj_array[i] = nullptr;
	}
	for (uint32_t i = 0; i < ThreadPool::get()->num_threads(); ++i){
		bj_array[i] = (int32_t*)grk_aligned_malloc(l_data_size);
		if (!bj_array[i]){
			rc = false;
			goto cleanup;
		}
	}

	for (uint32_t decompno = 0; decompno < num_decomps; ++decompno) {

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

		// transform vertical
		if (rw) {
			const uint32_t linesPerThreadV = static_cast<uint32_t>(std::ceil((float)rw / (float)ThreadPool::get()->num_threads()));
			const uint32_t s_n = rh_next;
			const uint32_t d_n = rh - rh_next;
			if (ThreadPool::get()->num_threads() == 1){
				DWT wavelet;
				for (auto m = 0U;m < std::min<uint32_t>(linesPerThreadV, rw); ++m) {
					auto bj = bj_array[0];
					auto aj = a + m;
					for (uint32_t k = 0; k < rh; ++k)
						bj[k] = aj[k * stride];
					wavelet.compress_line(bj, (int32_t)d_n, (int32_t)s_n, cas_col);
					deinterleave_v(bj, aj, d_n, s_n, stride, cas_col);
				}
			} else {
				std::vector< std::future<int> > results;
				for(uint32_t i = 0; i < ThreadPool::get()->num_threads(); ++i) {
					uint32_t index = i;
					results.emplace_back(
						ThreadPool::get()->enqueue([this, index, bj_array,a,
													 stride, rw,rh,
													 d_n, s_n, cas_col,
													 linesPerThreadV] {
							DWT wavelet;
							for (uint32_t m = index * linesPerThreadV;
									m < std::min<uint32_t>((index+1)*linesPerThreadV, rw); ++m) {
								auto bj = bj_array[index];
								auto aj = a + m;
								for (uint32_t k = 0; k < rh; ++k)
									bj[k] = aj[k * stride];
								wavelet.compress_line(bj, (int32_t)d_n, (int32_t)s_n, cas_col);
								deinterleave_v(bj, aj, d_n, s_n, stride, cas_col);
							}
							return 0;
						})
					);
				}
				for(auto &result: results)
					result.get();
			}
		}

		// transform horizontal
		if (rh){
			const uint32_t s_n = rw_next;
			const uint32_t d_n = rw - rw_next;
			const uint32_t linesPerThreadH = static_cast<uint32_t>(std::ceil((float)rh / (float)ThreadPool::get()->num_threads()));
			if (ThreadPool::get()->num_threads() == 1){
				DWT wavelet;
				for (auto m = 0U;m < std::min<uint32_t>(linesPerThreadH, rh); ++m) {
					auto bj = bj_array[0];
					auto aj = a + m * stride;
					memcpy(bj,aj,rw << 2);
					wavelet.compress_line(bj, (int32_t)d_n, (int32_t)s_n, cas_row);
					deinterleave_h(bj, aj, d_n, s_n, cas_row);
				}

			} else {
				std::vector< std::future<int> > results;
				for(uint32_t i = 0; i < ThreadPool::get()->num_threads(); ++i) {
					uint32_t index = i;
					results.emplace_back(
						ThreadPool::get()->enqueue([this, index, bj_array,a,
													 stride, rw,rh,
													 d_n, s_n, cas_row,
													 linesPerThreadH] {
							DWT wavelet;
							for (auto m = index * linesPerThreadH;
									m < std::min<uint32_t>((index+1)*linesPerThreadH, rh); ++m) {
								int32_t *bj = bj_array[index];
								int32_t *aj = a + m * stride;
								memcpy(bj,aj,rw << 2);
								wavelet.compress_line(bj, (int32_t)d_n, (int32_t)s_n, cas_row);
								deinterleave_h(bj, aj, d_n, s_n, cas_row);
							}
							return 0;
						})
					);
				}
				for(auto &result: results)
					result.get();
			}
		}
		cur_res = next_res;
		next_res--;
	}
cleanup:
	for (uint32_t i = 0; i < ThreadPool::get()->num_threads(); ++i)
		grk_aligned_free(bj_array[i]);
	delete[] bj_array;
	return rc;
}

}
