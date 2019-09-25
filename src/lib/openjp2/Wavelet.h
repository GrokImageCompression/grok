/*
 *    Copyright (C) 2016-2019 Grok Image Compression Inc.
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

template <typename DWT> class Wavelet
{

public:
	/**
	 Forward wavelet transform in 2-D.
	 @param tilec Tile component information (current tile)
	 */
	bool encode(tcd_tilecomp_t *tilec);

	/**
	 Inverse wavelet transform in 2-D.
	 @param tilec Tile component information (current tile)
	 @param numres Number of resolution levels to decode
	 */
	bool decode(tcd_tilecomp_t *tilec, uint32_t numres, uint32_t numThreads);


};


/**
 Forward wavelet transform in 2-D.
 @param tilec Tile component information (current tile)
 */
template <typename DWT> bool Wavelet<DWT>::encode(tcd_tilecomp_t *tilec){
	if (tilec->numresolutions == 1U)
		return true;

	size_t l_data_size = dwt_utils::max_resolution(tilec->resolutions,
			tilec->numresolutions) * sizeof(int32_t);
	/* overflow check */
	if (l_data_size > SIZE_MAX) {
		GROK_ERROR("Wavelet encode: overflow");
		return false;
	}
	if (!l_data_size)
		return false;

	bool rc = true;
	uint32_t rw,rh,rw1,rh1;
	uint8_t cas_row,cas_col;
	uint32_t stride = tilec->x1 - tilec->x0;
	int32_t num_decomps = (int32_t) tilec->numresolutions - 1;
	int32_t *a = tile_buf_get_ptr(tilec->buf, 0, 0, 0, 0);
	tcd_resolution_t *l_cur_res = tilec->resolutions + num_decomps;
	tcd_resolution_t *l_last_res = l_cur_res - 1;

	int32_t **bj_array = new int32_t*[Scheduler::g_TS.GetNumTaskThreads()];
	for (uint32_t i = 0; i < Scheduler::g_TS.GetNumTaskThreads(); ++i){
		bj_array[i] = nullptr;
	}
	for (uint32_t i = 0; i < Scheduler::g_TS.GetNumTaskThreads(); ++i){
		bj_array[i] = (int32_t*)grok_aligned_malloc(l_data_size);
		if (!bj_array[i]){
			rc = false;
			goto cleanup;
		}
	}

	for (int32_t i = 0; i < num_decomps; ++i) {

		/* width of the resolution level computed   */
		rw = l_cur_res->x1 - l_cur_res->x0;
		/* height of the resolution level computed  */
		rh = l_cur_res->y1 - l_cur_res->y0;
		// width of the resolution level once lower than computed one
		rw1 = l_last_res->x1 - l_last_res->x0;
		//height of the resolution level once lower than computed one
		rh1 = l_last_res->y1 - l_last_res->y0;

		/* 0 = non inversion on horizontal filtering 1 = inversion between low-pass and high-pass filtering */
		cas_row = l_cur_res->x0 & 1;
		/* 0 = non inversion on vertical filtering 1 = inversion between low-pass and high-pass filtering   */
		cas_col = l_cur_res->y0 & 1;

		// transform vertical
		if (rw) {
			const uint32_t linesPerThreadV = (uint32_t)(std::ceil((float)rw / Scheduler::g_TS.GetNumTaskThreads()));
			const uint32_t s_n = rh1;
			const uint32_t d_n = rh - rh1;
			enki::TaskSet task(Scheduler::g_TS.GetNumTaskThreads(),
					[bj_array,a,
					 stride, rw,rh,
					 d_n, s_n, cas_col,
					 linesPerThreadV](enki::TaskSetPartition range, uint32_t threadnum) {

				DWT wavelet;

				for (auto m = range.start * linesPerThreadV;
						m < std::min<uint32_t>((range.end)*linesPerThreadV, rw); ++m) {
					int32_t *bj = bj_array[threadnum];
					int32_t *aj = a + m;
					for (uint32_t k = 0; k < rh; ++k) {
						bj[k] = aj[k * stride];
					}
					wavelet.encode_line(bj, d_n, s_n, cas_col);
					dwt_utils::deinterleave_v(bj, aj, d_n, s_n, stride, cas_col);
				}

			});
			Scheduler::g_TS.AddTaskSetToPipe(&task);
			Scheduler::g_TS.WaitforTask(&task);
		}

		// transform horizontal
		if (rh){
			const uint32_t s_n = rw1;
			const uint32_t d_n = rw - rw1;
			const uint32_t linesPerThreadH = (uint32_t)std::ceil((float)rh / Scheduler::g_TS.GetNumTaskThreads());
			enki::TaskSet task(Scheduler::g_TS.GetNumTaskThreads(),
								[bj_array,a,
								 stride, rw,rh,
								 d_n, s_n, cas_row,
								 linesPerThreadH](enki::TaskSetPartition range, uint32_t threadnum) {

				DWT wavelet;
				for (auto m = range.start * linesPerThreadH;
						m < std::min<uint32_t>((range.end)*linesPerThreadH, rh); ++m) {
					int32_t *bj = bj_array[threadnum];
					int32_t *aj = a + m * stride;
					memcpy(bj,aj,rw << 2);
					wavelet.encode_line(bj, d_n, s_n, cas_row);
					dwt_utils::deinterleave_h(bj, aj, d_n, s_n, cas_row);
				}
			});
			Scheduler::g_TS.AddTaskSetToPipe(&task);
			Scheduler::g_TS.WaitforTask(&task);
		}
		l_cur_res = l_last_res;
		l_last_res--;
	}
cleanup:
	for (uint32_t i = 0; i < Scheduler::g_TS.GetNumTaskThreads(); ++i)
		grok_aligned_free(bj_array[i]);
	delete[] bj_array;
	return rc;
}

/**
 Inverse wavelet transform in 2-D.
 @param tilec Tile component information (current tile)
 @param numres Number of resolution levels to decode
 */
template <typename DWT> bool Wavelet<DWT>::decode(tcd_tilecomp_t *tilec, uint32_t numres, uint32_t numThreads){

}



}
