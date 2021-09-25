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
#include <algorithm>
#include <limits>
#include <sstream>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "transform/WaveletReverse.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>
HWY_BEFORE_NAMESPACE();
namespace grk
{
namespace HWY_NAMESPACE
{
	using namespace hwy::HWY_NAMESPACE;

	static size_t hwy_num_lanes(void)
	{
		const HWY_FULL(int32_t) di;
		return Lanes(di);
	}

#define HWY_PLL_COLS_53 (2 * Lanes(di))

	static void hwy_decompress_v_final_memcpy_53(const int32_t* buf, const uint32_t height,
												 int32_t* dest, const size_t strideDest)
	{
		const HWY_FULL(int32_t) di;
		for(uint32_t i = 0; i < height; ++i)
		{
			StoreU(Load(di, buf + HWY_PLL_COLS_53 * i + 0), di, &dest[(size_t)i * strideDest + 0]);
			StoreU(Load(di, buf + HWY_PLL_COLS_53 * i + Lanes(di)), di,
				   dest + (size_t)i * strideDest + Lanes(di));
		}
	}
	/** Vertical inverse 5x3 wavelet transform for 8 columns in SSE2, or
	 * 16 in AVX2, when top-most pixel is on even coordinate */
	static void hwy_decompress_v_cas0_mcols_53(int32_t* buf, int32_t* bandL, /* even */
											   const uint32_t hL, const size_t strideL,
											   int32_t* bandH, /* odd */
											   const uint32_t hH, const size_t strideH,
											   int32_t* dest, const uint32_t strideDest)
	{
		const HWY_FULL(int32_t) di;
		auto two = Set(di, 2);

		const uint32_t total_height = hL + hH;
		assert(total_height > 1);

		/* Note: loads of input even/odd values must be done in a unaligned */
		/* fashion. But stores in tmp can be done with aligned store, since */
		/* the temporary buffer is properly aligned */
		assert((size_t)buf % (sizeof(int32_t) * Lanes(di)) == 0);

		auto s1n_0 = LoadU(di, bandL + 0);
		auto s1n_1 = LoadU(di, bandL + Lanes(di));
		auto d1n_0 = LoadU(di, bandH);
		auto d1n_1 = LoadU(di, bandH + Lanes(di));

		/* s0n = s1n - ((d1n + 1) >> 1); <==> */
		/* s0n = s1n - ((d1n + d1n + 2) >> 2); */
		auto s0n_0 = s1n_0 - ShiftRight<2>(d1n_0 + d1n_0 + two);
		auto s0n_1 = s1n_1 - ShiftRight<2>(d1n_1 + d1n_1 + two);

		uint32_t i = 0;
		if(total_height > 3)
		{
			uint32_t j;
			for(i = 0, j = 1; i < (total_height - 3); i += 2, j++)
			{
				auto d1c_0 = d1n_0;
				auto s0c_0 = s0n_0;
				auto d1c_1 = d1n_1;
				auto s0c_1 = s0n_1;

				s1n_0 = LoadU(di, bandL + j * strideL);
				s1n_1 = LoadU(di, bandL + j * strideL + Lanes(di));
				d1n_0 = LoadU(di, bandH + j * strideH);
				d1n_1 = LoadU(di, bandH + j * strideH + Lanes(di));

				/*s0n = s1n - ((d1c + d1n + 2) >> 2);*/
				s0n_0 = s1n_0 - ShiftRight<2>(d1c_0 + d1n_0 + two);
				s0n_1 = s1n_1 - ShiftRight<2>(d1c_1 + d1n_1 + two);

				Store(s0c_0, di, buf + HWY_PLL_COLS_53 * (i + 0));
				Store(s0c_1, di, buf + HWY_PLL_COLS_53 * (i + 0) + Lanes(di));

				/* d1c + ((s0c + s0n) >> 1) */
				Store(d1c_0 + ShiftRight<1>(s0c_0 + s0n_0), di,
					  buf + HWY_PLL_COLS_53 * (i + 1) + 0);
				Store(d1c_1 + ShiftRight<1>(s0c_1 + s0n_1), di,
					  buf + HWY_PLL_COLS_53 * (i + 1) + Lanes(di));
			}
		}

		Store(s0n_0, di, buf + HWY_PLL_COLS_53 * (i + 0) + 0);
		Store(s0n_1, di, buf + HWY_PLL_COLS_53 * (i + 0) + Lanes(di));

		if(total_height & 1)
		{
			s1n_0 = LoadU(di, bandL + (size_t)((total_height - 1) / 2) * strideL);
			/* tmp_len_minus_1 = s1n - ((d1n + 1) >> 1); */
			auto tmp_len_minus_1 = s1n_0 - ShiftRight<2>(d1n_0 + d1n_0 + two);
			Store(tmp_len_minus_1, di, buf + HWY_PLL_COLS_53 * (total_height - 1));
			/* d1n + ((s0n + tmp_len_minus_1) >> 1) */
			Store(d1n_0 + ShiftRight<1>(s0n_0 + tmp_len_minus_1), di,
				  buf + HWY_PLL_COLS_53 * (total_height - 2));

			s1n_1 = LoadU(di, bandL + (size_t)((total_height - 1) / 2) * strideL + Lanes(di));
			/* tmp_len_minus_1 = s1n - ((d1n + 1) >> 1); */
			tmp_len_minus_1 = s1n_1 - ShiftRight<2>(d1n_1 + d1n_1 + two);
			Store(tmp_len_minus_1, di, buf + HWY_PLL_COLS_53 * (total_height - 1) + Lanes(di));
			/* d1n + ((s0n + tmp_len_minus_1) >> 1) */
			Store(d1n_1 + ShiftRight<1>(s0n_1 + tmp_len_minus_1), di,
				  buf + HWY_PLL_COLS_53 * (total_height - 2) + Lanes(di));
		}
		else
		{
			Store(d1n_0 + s0n_0, di, buf + HWY_PLL_COLS_53 * (total_height - 1) + 0);
			Store(d1n_1 + s0n_1, di, buf + HWY_PLL_COLS_53 * (total_height - 1) + Lanes(di));
		}
		hwy_decompress_v_final_memcpy_53(buf, total_height, dest, strideDest);
	}

	/** Vertical inverse 5x3 wavelet transform for 8 columns in SSE2, or
	 * 16 in AVX2, when top-most pixel is on odd coordinate */
	static void hwy_decompress_v_cas1_mcols_53(int32_t* buf, int32_t* bandL, const uint32_t hL,
											   const uint32_t strideL, int32_t* bandH,
											   const uint32_t hH, const uint32_t strideH,
											   int32_t* dest, const uint32_t strideDest)
	{
		const HWY_FULL(int32_t) di;
		auto two = Set(di, 2);

		const uint32_t total_height = hL + hH;
		assert(total_height > 2);
		/* Note: loads of input even/odd values must be done in a unaligned */
		/* fashion. But stores in buf can be done with aligned store, since */
		/* the temporary buffer is properly aligned */
		assert((size_t)buf % (sizeof(int32_t) * Lanes(di)) == 0);

		const int32_t* in_even = bandH;
		const int32_t* in_odd = bandL;
		auto s1_0 = LoadU(di, in_even + strideH);
		/* in_odd[0] - ((in_even[0] + s1 + 2) >> 2); */
		auto dc_0 = LoadU(di, in_odd + 0) - ShiftRight<2>(LoadU(di, in_even + 0) + s1_0 + two);
		Store(LoadU(di, in_even + 0) + dc_0, di, buf + HWY_PLL_COLS_53 * 0);

		auto s1_1 = LoadU(di, in_even + strideH + Lanes(di));
		/* in_odd[0] - ((in_even[0] + s1 + 2) >> 2); */
		auto dc_1 = LoadU(di, in_odd + Lanes(di)) -
					ShiftRight<2>(LoadU(di, in_even + Lanes(di)) + s1_1 + two);
		Store(LoadU(di, in_even + Lanes(di)) + dc_1, di, buf + HWY_PLL_COLS_53 * 0 + Lanes(di));

		uint32_t i;
		size_t j;
		for(i = 1, j = 1; i < (total_height - 2 - !(total_height & 1)); i += 2, j++)
		{
			auto s2_0 = LoadU(di, in_even + (j + 1) * strideH);
			auto s2_1 = LoadU(di, in_even + (j + 1) * strideH + Lanes(di));

			/* dn = in_odd[j * stride] - ((s1 + s2 + 2) >> 2); */
			auto dn_0 = LoadU(di, in_odd + j * strideL) - ShiftRight<2>(s1_0 + s2_0 + two);
			auto dn_1 =
				LoadU(di, in_odd + j * strideL + Lanes(di)) - ShiftRight<2>(s1_1 + s2_1 + two);

			Store(dc_0, di, buf + HWY_PLL_COLS_53 * i);
			Store(dc_1, di, buf + HWY_PLL_COLS_53 * i + Lanes(di));

			/* buf[i + 1] = s1 + ((dn + dc) >> 1); */
			Store(s1_0 + ShiftRight<1>(dn_0 + dc_0), di, buf + HWY_PLL_COLS_53 * (i + 1) + 0);
			Store(s1_1 + ShiftRight<1>(dn_1 + dc_1), di,
				  buf + HWY_PLL_COLS_53 * (i + 1) + Lanes(di));

			dc_0 = dn_0;
			s1_0 = s2_0;
			dc_1 = dn_1;
			s1_1 = s2_1;
		}
		Store(dc_0, di, buf + HWY_PLL_COLS_53 * i);
		Store(dc_1, di, buf + HWY_PLL_COLS_53 * i + Lanes(di));

		if(!(total_height & 1))
		{
			/*dn = in_odd[(len / 2 - 1) * stride] - ((s1 + 1) >> 1); */
			auto dn_0 = LoadU(di, in_odd + (size_t)(total_height / 2 - 1) * strideL) -
						ShiftRight<2>(s1_0 + s1_0 + two);
			auto dn_1 = LoadU(di, in_odd + (size_t)(total_height / 2 - 1) * strideL + Lanes(di)) -
						ShiftRight<2>(s1_1 + s1_1 + two);

			/* buf[len - 2] = s1 + ((dn + dc) >> 1); */
			Store(s1_0 + ShiftRight<1>(dn_0 + dc_0), di,
				  buf + HWY_PLL_COLS_53 * (total_height - 2) + 0);
			Store(s1_1 + ShiftRight<1>(dn_1 + dc_1), di,
				  buf + HWY_PLL_COLS_53 * (total_height - 2) + Lanes(di));

			Store(dn_0, di, buf + HWY_PLL_COLS_53 * (total_height - 1) + 0);
			Store(dn_1, di, buf + HWY_PLL_COLS_53 * (total_height - 1) + Lanes(di));
		}
		else
		{
			Store(s1_0 + dc_0, di, buf + HWY_PLL_COLS_53 * (total_height - 1) + 0);
			Store(s1_1 + dc_1, di, buf + HWY_PLL_COLS_53 * (total_height - 1) + Lanes(di));
		}
		hwy_decompress_v_final_memcpy_53(buf, total_height, dest, strideDest);
	}

} // namespace HWY_NAMESPACE
} // namespace grk
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace grk
{
HWY_EXPORT(hwy_num_lanes);
HWY_EXPORT(hwy_decompress_v_cas0_mcols_53);
HWY_EXPORT(hwy_decompress_v_cas1_mcols_53);
/* <summary>                             */
/* Determine maximum computed resolution level for inverse wavelet transform */
/* </summary>                            */
uint32_t max_resolution(Resolution* GRK_RESTRICT r, uint32_t i)
{
	uint32_t mr = 0;
	while(--i)
	{
		++r;
		uint32_t w;
		if(mr < (w = r->x1 - r->x0))
			mr = w;
		if(mr < (w = r->y1 - r->y0))
			mr = w;
	}
	return mr;
}

template<typename T, typename S>
struct decompress_job
{
	decompress_job(S data, T* GRK_RESTRICT LL, uint32_t sLL, T* GRK_RESTRICT HL, uint32_t sHL,
				   T* GRK_RESTRICT LH, uint32_t sLH, T* GRK_RESTRICT HH, uint32_t sHH,
				   T* GRK_RESTRICT destination, uint32_t strideDestination, uint32_t min_j,
				   uint32_t max_j)
		: data(data), bandLL(LL), strideLL(sLL), bandHL(HL), strideHL(sHL), bandLH(LH),
		  strideLH(sLH), bandHH(HH), strideHH(sHH), dest(destination),
		  strideDest(strideDestination), min_j(min_j), max_j(max_j)
	{}
	decompress_job(S data, uint32_t min_j, uint32_t max_j)
		: decompress_job(data, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, min_j,
						 max_j)
	{}
	S data;
	T* GRK_RESTRICT bandLL;
	uint32_t strideLL;
	T* GRK_RESTRICT bandHL;
	uint32_t strideHL;
	T* GRK_RESTRICT bandLH;
	uint32_t strideLH;
	T* GRK_RESTRICT bandHH;
	uint32_t strideHH;
	T* GRK_RESTRICT dest;
	uint32_t strideDest;

	uint32_t min_j;
	uint32_t max_j;
};

/** Number of columns that we can process in parallel in the vertical pass */
#undef PLL_COLS_53
#define PLL_COLS_53 (2 * uint32_t(HWY_DYNAMIC_DISPATCH(hwy_num_lanes)()))
template<typename T>
struct dwt_data
{
	dwt_data()
		: allocatedMem(nullptr), m_lenBytes(0), m_paddingBytes(0), mem(nullptr), memL(nullptr),
		  memH(nullptr), dn_full(0), sn_full(0), parity(0), resno(0)
	{}

	dwt_data(const dwt_data& rhs)
		: allocatedMem(nullptr), m_lenBytes(0), m_paddingBytes(0), mem(nullptr), memL(nullptr),
		  memH(nullptr), dn_full(rhs.dn_full), sn_full(rhs.sn_full), parity(rhs.parity),
		  win_l(rhs.win_l), win_h(rhs.win_h), resno(rhs.resno)
	{}

	bool alloc(size_t len)
	{
		return alloc(len, 0);
	}

	bool alloc(size_t len, size_t padding)
	{
		release();

		/* overflow check */
		if(len > (SIZE_MAX / sizeof(T)))
		{
			GRK_ERROR("data size overflow");
			return false;
		}
		m_paddingBytes = grkMakeAlignedWidth((uint32_t)padding * 2 + 32) * sizeof(T);
		m_lenBytes = len * sizeof(T) + 2 * m_paddingBytes;
		allocatedMem = (T*)grkAlignedMalloc(m_lenBytes);
		if(!allocatedMem)
		{
			GRK_ERROR("Failed to allocate %d bytes", m_lenBytes);
			return false;
		}
		// memset(allocatedMem, 128, m_lenBytes);
		mem = allocatedMem + m_paddingBytes / sizeof(T);
		return (allocatedMem != nullptr) ? true : false;
	}
	void release()
	{
		grkAlignedFree(allocatedMem);
		allocatedMem = nullptr;
		mem = nullptr;
		memL = nullptr;
		memH = nullptr;
	}
	T* allocatedMem;
	size_t m_lenBytes;
	size_t m_paddingBytes;
	T* mem;
	T* memL;
	T* memH;
	uint32_t dn_full; /* number of elements in high pass band */
	uint32_t sn_full; /* number of elements in low pass band */
	uint32_t parity; /* 0 = start on even coord, 1 = start on odd coord */
	grkLineU32 win_l;
	grkLineU32 win_h;
	uint8_t resno;
};

struct Params97
{
	Params97() : dataPrev(nullptr), data(nullptr), len(0), lenMax(0) {}
	vec4f* dataPrev;
	vec4f* data;
	uint32_t len;
	uint32_t lenMax;
};

static Params97 makeParams97(dwt_data<vec4f>* dwt, bool isBandL, bool step1);

static const float dwt_alpha = 1.586134342f; /*  12994 */
static const float dwt_beta = 0.052980118f; /*    434 */
static const float dwt_gamma = -0.882911075f; /*  -7233 */
static const float dwt_delta = -0.443506852f; /*  -3633 */

static const float K = 1.230174105f; /*  10078 */
static const float twice_invK = 1.625732422f;

static void decompress_h_cas0_53(int32_t* buf, int32_t* bandL, /* even */
								 const uint32_t wL, int32_t* bandH, const uint32_t wH,
								 int32_t* dest)
{ /* odd */
	const uint32_t total_width = wL + wH;
	assert(total_width > 1);

	/* Improved version of the TWO_PASS_VERSION: */
	/* Performs lifting in one single iteration. Saves memory */
	/* accesses and explicit interleaving. */
	int32_t s1n = bandL[0];
	int32_t d1n = bandH[0];
	int32_t s0n = s1n - ((d1n + 1) >> 1);

	uint32_t i = 0;

	if(total_width > 2)
	{
		for(uint32_t j = 1; i < (total_width - 3); i += 2, j++)
		{
			int32_t d1c = d1n;
			int32_t s0c = s0n;

			s1n = bandL[j];
			d1n = bandH[j];
			s0n = s1n - ((d1c + d1n + 2) >> 2);
			buf[i] = s0c;
			buf[i + 1] = d1c + ((s0c + s0n) >> 1);
		}
	}

	buf[i] = s0n;
	if(total_width & 1)
	{
		buf[total_width - 1] = bandL[(total_width - 1) >> 1] - ((d1n + 1) >> 1);
		buf[total_width - 2] = d1n + ((s0n + buf[total_width - 1]) >> 1);
	}
	else
	{
		buf[total_width - 1] = d1n + s0n;
	}
	memcpy(dest, buf, total_width * sizeof(int32_t));
}

static void decompress_h_cas1_53(int32_t* buf, int32_t* bandL, /* odd */
								 const uint32_t wL, int32_t* bandH, const uint32_t wH,
								 int32_t* dest)
{ /* even */
	const uint32_t total_width = wL + wH;
	assert(total_width > 2);

	/* Improved version of the TWO_PASS_VERSION:
	   Performs lifting in one single iteration. Saves memory
	   accesses and explicit interleaving. */
	int32_t s1 = bandH[1];
	int32_t dc = bandL[0] - ((bandH[0] + s1 + 2) >> 2);
	buf[0] = bandH[0] + dc;
	uint32_t i, j;
	for(i = 1, j = 1; i < (total_width - 2 - !(total_width & 1)); i += 2, j++)
	{
		int32_t s2 = bandH[j + 1];
		int32_t dn = bandL[j] - ((s1 + s2 + 2) >> 2);

		buf[i] = dc;
		buf[i + 1] = s1 + ((dn + dc) >> 1);
		dc = dn;
		s1 = s2;
	}

	buf[i] = dc;

	if(!(total_width & 1))
	{
		int32_t dn = bandL[total_width / 2 - 1] - ((s1 + 1) >> 1);
		buf[total_width - 2] = s1 + ((dn + dc) >> 1);
		buf[total_width - 1] = dn;
	}
	else
	{
		buf[total_width - 1] = s1 + dc;
	}
	memcpy(dest, buf, total_width * sizeof(int32_t));
}

/** Vertical inverse 5x3 wavelet transform for one column, when top-most
 * pixel is on even coordinate */
static void decompress_v_cas0_53(int32_t* buf, int32_t* bandL, const uint32_t hL,
								 const uint32_t strideL, int32_t* bandH, const uint32_t hH,
								 const uint32_t strideH, int32_t* dest, const uint32_t strideDest)
{
	const uint32_t total_height = hL + hH;
	assert(total_height > 1);

	/* Performs lifting in one single iteration. Saves memory */
	/* accesses and explicit interleaving. */
	int32_t s1n = bandL[0];
	int32_t d1n = bandH[0];
	int32_t s0n = s1n - ((d1n + 1) >> 1);

	uint32_t i = 0;
	if(total_height > 2)
	{
		auto bL = bandL + strideL;
		auto bH = bandH + strideH;
		for(uint32_t j = 0; i < (total_height - 3); i += 2, j++)
		{
			int32_t d1c = d1n;
			int32_t s0c = s0n;
			s1n = *bL;
			bL += strideL;
			d1n = *bH;
			bH += strideH;
			s0n = s1n - ((d1c + d1n + 2) >> 2);
			buf[i] = s0c;
			buf[i + 1] = d1c + ((s0c + s0n) >> 1);
		}
	}
	buf[i] = s0n;
	if(total_height & 1)
	{
		buf[total_height - 1] = bandL[((total_height - 1) / 2) * strideL] - ((d1n + 1) >> 1);
		buf[total_height - 2] = d1n + ((s0n + buf[total_height - 1]) >> 1);
	}
	else
	{
		buf[total_height - 1] = d1n + s0n;
	}
	for(i = 0; i < total_height; ++i)
	{
		*dest = buf[i];
		dest += strideDest;
	}
}

/** Vertical inverse 5x3 wavelet transform for one column, when top-most
 * pixel is on odd coordinate */
static void decompress_v_cas1_53(int32_t* buf, int32_t* bandL, const uint32_t hL,
								 const uint32_t strideL, int32_t* bandH, const uint32_t hH,
								 const uint32_t strideH, int32_t* dest, const uint32_t strideDest)
{
	const uint32_t total_height = hL + hH;
	assert(total_height > 2);

	/* Performs lifting in one single iteration. Saves memory */
	/* accesses and explicit interleaving. */
	int32_t s1 = bandH[strideH];
	int32_t dc = bandL[0] - ((bandH[0] + s1 + 2) >> 2);
	buf[0] = bandH[0] + dc;
	auto s2_ptr = bandH + (strideH << 1);
	auto dn_ptr = bandL + strideL;
	uint32_t i, j;
	for(i = 1, j = 1; i < (total_height - 2 - !(total_height & 1)); i += 2, j++)
	{
		int32_t s2 = *s2_ptr;
		s2_ptr += strideH;

		int32_t dn = *dn_ptr - ((s1 + s2 + 2) >> 2);
		dn_ptr += strideL;

		buf[i] = dc;
		buf[i + 1] = s1 + ((dn + dc) >> 1);
		dc = dn;
		s1 = s2;
	}
	buf[i] = dc;
	if(!(total_height & 1))
	{
		int32_t dn = bandL[((total_height >> 1) - 1) * strideL] - ((s1 + 1) >> 1);
		buf[total_height - 2] = s1 + ((dn + dc) >> 1);
		buf[total_height - 1] = dn;
	}
	else
	{
		buf[total_height - 1] = s1 + dc;
	}
	for(i = 0; i < total_height; ++i)
	{
		*dest = buf[i];
		dest += strideDest;
	}
}

/* <summary>                            */
/* Inverse 5-3 wavelet transform in 1-D for one row. */
/* </summary>                           */
/* Performs interleave, inverse wavelet transform and copy back to buffer */
static void decompress_h_53(const dwt_data<int32_t>* dwt, int32_t* bandL, int32_t* bandH,
							int32_t* dest)
{
	const uint32_t total_width = dwt->sn_full + dwt->dn_full;
	assert(total_width != 0);
	if(dwt->parity == 0)
	{ /* Left-most sample is on even coordinate */
		if(total_width > 1)
		{
			decompress_h_cas0_53(dwt->mem, bandL, dwt->sn_full, bandH, dwt->dn_full, dest);
		}
		else
		{
			assert(dwt->sn_full == 1);
			// only L op: only one sample in L band and H band is empty
			dest[0] = bandL[0];
		}
	}
	else
	{ /* Left-most sample is on odd coordinate */
		if(total_width == 1)
		{
			assert(dwt->dn_full == 1);
			// only H op: only one sample in H band and L band is empty
			dest[0] = bandH[0] / 2;
		}
		else if(total_width == 2)
		{
			dwt->mem[1] = bandL[0] - ((bandH[0] + 1) >> 1);
			dest[0] = bandH[0] + dwt->mem[1];
			dest[1] = dwt->mem[1];
		}
		else
		{
			decompress_h_cas1_53(dwt->mem, bandL, dwt->sn_full, bandH, dwt->dn_full, dest);
		}
	}
}

/* <summary>                            */
/* Inverse vertical 5-3 wavelet transform in 1-D for several columns. */
/* </summary>                           */
/* Performs interleave, inverse wavelet transform and copy back to buffer */
static void decompress_v_53(const dwt_data<int32_t>* dwt, int32_t* bandL, const uint32_t strideL,
							int32_t* bandH, const uint32_t strideH, int32_t* dest,
							const uint32_t strideDest, uint32_t nb_cols)
{
	const uint32_t total_height = dwt->sn_full + dwt->dn_full;
	assert(total_height != 0);
	if(dwt->parity == 0)
	{
		if(total_height == 1)
		{
			for(uint32_t c = 0; c < nb_cols; c++, bandL++, dest++)
				dest[0] = bandL[0];
		} else {
			if(nb_cols == PLL_COLS_53)
			{
				/* Same as below general case, except that thanks to SSE2/AVX2 */
				/* we can efficiently process 8/16 columns in parallel */
				HWY_DYNAMIC_DISPATCH(hwy_decompress_v_cas0_mcols_53)
				(dwt->mem, bandL, dwt->sn_full, strideL, bandH, dwt->dn_full, strideH, dest,
				 strideDest);
			}
			else
			{
				for(uint32_t c = 0; c < nb_cols; c++, bandL++, bandH++, dest++)
					decompress_v_cas0_53(dwt->mem, bandL, dwt->sn_full, strideL, bandH, dwt->dn_full,
										 strideH, dest, strideDest);
			}
		}
	}
	else
	{
		if(total_height == 1)
		{
			for(uint32_t c = 0; c < nb_cols; c++, bandL++, dest++)
				dest[0] = bandL[0] >> 1;
		}
		else if(total_height == 2)
		{
			auto out = dwt->mem;
			for(uint32_t c = 0; c < nb_cols; c++, bandL++, bandH++, dest++)
			{
				out[1] = bandL[0] - ((bandH[0] + 1) >> 1);
				dest[0] = bandH[0] + out[1];
				dest[1] = out[1];
			}
		} else {
			if(nb_cols == PLL_COLS_53)
			{
				/* Same as below general case, except that thanks to SSE2/AVX2 */
				/* we can efficiently process 8/16 columns in parallel */
				HWY_DYNAMIC_DISPATCH(hwy_decompress_v_cas1_mcols_53)
				(dwt->mem, bandL, dwt->sn_full, strideL, bandH, dwt->dn_full, strideH, dest,
				 strideDest);
			} else {
				for(uint32_t c = 0; c < nb_cols; c++, bandL++, bandH++, dest++)
					decompress_v_cas1_53(dwt->mem, bandL, dwt->sn_full, strideL, bandH, dwt->dn_full,
										 strideH, dest, strideDest);
			}
		}
	}
}

static void decompress_h_strip_53(const dwt_data<int32_t>* horiz, uint32_t hMin, uint32_t hMax,
								  int32_t* bandL, const uint32_t strideL, int32_t* bandH,
								  const uint32_t strideH, int32_t* dest, const uint32_t strideDest)
{
	for(uint32_t j = hMin; j < hMax; ++j)
	{
		decompress_h_53(horiz, bandL, bandH, dest);
		bandL += strideL;
		bandH += strideH;
		dest += strideDest;
	}
}

static bool decompress_h_mt_53(uint32_t num_threads, size_t data_size, dwt_data<int32_t>& horiz,
							   dwt_data<int32_t>& vert, uint32_t rh, int32_t* bandL,
							   const uint32_t strideL, int32_t* bandH, const uint32_t strideH,
							   int32_t* dest, const uint32_t strideDest)
{
	if(num_threads == 1 || rh <= 1)
	{
		if(!horiz.mem)
		{
			if(!horiz.alloc(data_size))
			{
				GRK_ERROR("Out of memory");
				return false;
			}
			vert.mem = horiz.mem;
		}
		decompress_h_strip_53(&horiz, 0, rh, bandL, strideL, bandH, strideH, dest, strideDest);
	}
	else
	{
		uint32_t num_jobs = (uint32_t)num_threads;
		if(rh < num_jobs)
			num_jobs = rh;
		uint32_t step_j = (rh / num_jobs);
		std::vector<std::future<int>> results;
		for(uint32_t j = 0; j < num_jobs; ++j)
		{
			auto min_j = j * step_j;
			auto job = new decompress_job<int32_t, dwt_data<int32_t>>(
				horiz, bandL + min_j * strideL, strideL, bandH + min_j * strideH, strideH, nullptr,
				0, nullptr, 0, dest + min_j * strideDest, strideDest, j * step_j,
				j < (num_jobs - 1U) ? (j + 1U) * step_j : rh);
			if(!job->data.alloc(data_size))
			{
				GRK_ERROR("Out of memory");
				horiz.release();
				return false;
			}
			results.emplace_back(ThreadPool::get()->enqueue([job] {
				decompress_h_strip_53(&job->data, job->min_j, job->max_j, job->bandLL,
									  job->strideLL, job->bandHL, job->strideHL, job->dest,
									  job->strideDest);
				job->data.release();
				delete job;
				return 0;
			}));
		}
		for(auto& result : results)
			result.get();
	}
	return true;
}

static void decompress_v_strip_53(const dwt_data<int32_t>* vert, uint32_t wMin, uint32_t wMax,
								  int32_t* bandL, const uint32_t strideL, int32_t* bandH,
								  const uint32_t strideH, int32_t* dest, const uint32_t strideDest)
{
	uint32_t j;
	for(j = wMin; j + PLL_COLS_53 <= wMax; j += PLL_COLS_53)
	{
		decompress_v_53(vert, bandL, strideL, bandH, strideH, dest, strideDest, PLL_COLS_53);
		bandL += PLL_COLS_53;
		bandH += PLL_COLS_53;
		dest += PLL_COLS_53;
	}
	if(j < wMax)
		decompress_v_53(vert, bandL, strideL, bandH, strideH, dest, strideDest, wMax - j);
}

static bool decompress_v_mt_53(uint32_t num_threads, size_t data_size, dwt_data<int32_t>& horiz,
							   dwt_data<int32_t>& vert, uint32_t rw, int32_t* bandL,
							   const uint32_t strideL, int32_t* bandH, const uint32_t strideH,
							   int32_t* dest, const uint32_t strideDest)
{
	if(num_threads == 1 || rw <= 1)
	{
		if(!horiz.mem)
		{
			if(!horiz.alloc(data_size))
			{
				GRK_ERROR("Out of memory");
				return false;
			}
			vert.mem = horiz.mem;
		}
		decompress_v_strip_53(&vert, 0, rw, bandL, strideL, bandH, strideH, dest, strideDest);
	}
	else
	{
		uint32_t num_jobs = (uint32_t)num_threads;
		if(rw < num_jobs)
			num_jobs = rw;
		uint32_t step_j = (rw / num_jobs);
		std::vector<std::future<int>> results;
		for(uint32_t j = 0; j < num_jobs; j++)
		{
			auto min_j = j * step_j;
			auto job = new decompress_job<int32_t, dwt_data<int32_t>>(
				vert, bandL + min_j, strideL, nullptr, 0, bandH + min_j, strideH, nullptr, 0,
				dest + min_j, strideDest, j * step_j, j < (num_jobs - 1U) ? (j + 1U) * step_j : rw);
			if(!job->data.alloc(data_size))
			{
				GRK_ERROR("Out of memory");
				vert.release();
				return false;
			}
			results.emplace_back(ThreadPool::get()->enqueue([job] {
				decompress_v_strip_53(&job->data, job->min_j, job->max_j, job->bandLL,
									  job->strideLL, job->bandLH, job->strideLH, job->dest,
									  job->strideDest);
				job->data.release();
				delete job;
				return 0;
			}));
		}
		for(auto& result : results)
			result.get();
	}
	return true;
}

/* <summary>                            */
/* Inverse wavelet transform in 2-D.    */
/* </summary>                           */
static bool decompress_tile_53(TileComponent* tilec, uint32_t numres)
{
	if(numres == 1U)
		return true;

	auto tr = tilec->tileCompResolution;
	uint32_t rw = tr->width();
	uint32_t rh = tr->height();

	uint32_t num_threads = (uint32_t)ThreadPool::get()->num_threads();
	size_t data_size = max_resolution(tr, numres);
	/* overflow check */
	if(data_size > (SIZE_MAX / PLL_COLS_53 / sizeof(int32_t)))
	{
		GRK_ERROR("Overflow");
		return false;
	}
	/* We need PLL_COLS_53 times the height of the array, */
	/* since for the vertical pass */
	/* we process PLL_COLS_53 columns at a time */
	dwt_data<int32_t> horiz;
	dwt_data<int32_t> vert;
	data_size *= PLL_COLS_53 * sizeof(int32_t);
	bool rc = true;
	for(uint8_t res = 1; res < numres; ++res)
	{
		horiz.sn_full = rw;
		vert.sn_full = rh;
		++tr;
		rw = tr->width();
		rh = tr->height();
		if(rw == 0 || rh == 0)
			continue;
		horiz.dn_full = rw - horiz.sn_full;
		horiz.parity = tr->x0 & 1;
		if(!decompress_h_mt_53(
			   num_threads, data_size, horiz, vert, vert.sn_full,
			   // LL
			   tilec->getBuffer()->getResWindowBufferREL(res - 1U)->getBuffer(),
			   tilec->getBuffer()->getResWindowBufferREL(res - 1U)->stride,
			   // HL
			   tilec->getBuffer()->getBandWindowBufferPaddedREL(res, BAND_ORIENT_HL)->getBuffer(),
			   tilec->getBuffer()->getBandWindowBufferPaddedREL(res, BAND_ORIENT_HL)->stride,
			   // lower split window
			   tilec->getBuffer()->getResWindowBufferSplitREL(res, SPLIT_L)->getBuffer(),
			   tilec->getBuffer()->getResWindowBufferSplitREL(res, SPLIT_L)->stride))
			return false;
		if(!decompress_h_mt_53(
			   num_threads, data_size, horiz, vert, rh - vert.sn_full,
			   // LH
			   tilec->getBuffer()->getBandWindowBufferPaddedREL(res, BAND_ORIENT_LH)->getBuffer(),
			   tilec->getBuffer()->getBandWindowBufferPaddedREL(res, BAND_ORIENT_LH)->stride,
			   // HH
			   tilec->getBuffer()->getBandWindowBufferPaddedREL(res, BAND_ORIENT_HH)->getBuffer(),
			   tilec->getBuffer()->getBandWindowBufferPaddedREL(res, BAND_ORIENT_HH)->stride,
			   // higher split window
			   tilec->getBuffer()->getResWindowBufferSplitREL(res, SPLIT_H)->getBuffer(),
			   tilec->getBuffer()->getResWindowBufferSplitREL(res, SPLIT_H)->stride))
			return false;
		vert.dn_full = rh - vert.sn_full;
		vert.parity = tr->y0 & 1;
		if(!decompress_v_mt_53(num_threads, data_size, horiz, vert, rw,
							   // lower split window
							   tilec->getBuffer()->getResWindowBufferSplitREL(res, SPLIT_L)->getBuffer(),
							   tilec->getBuffer()->getResWindowBufferSplitREL(res, SPLIT_L)->stride,
							   // higher split window
							   tilec->getBuffer()->getResWindowBufferSplitREL(res, SPLIT_H)->getBuffer(),
							   tilec->getBuffer()->getResWindowBufferSplitREL(res, SPLIT_H)->stride,
							   // resolution buffer
							   tilec->getBuffer()->getResWindowBufferREL(res)->getBuffer(),
							   tilec->getBuffer()->getResWindowBufferREL(res)->stride))
			return false;
	}
	horiz.release();
	return rc;
}

//#undef __SSE__

#ifdef __SSE__
static void decompress_step1_sse_97(Params97 d, const __m128 c)
{
	// process 4 floats at a time
	auto mmData = (__m128*)d.data;
	uint32_t i;
	for(i = 0; i + 3 < d.len; i += 4, mmData += 8)
	{
		mmData[0] = _mm_mul_ps(mmData[0], c);
		mmData[2] = _mm_mul_ps(mmData[2], c);
		mmData[4] = _mm_mul_ps(mmData[4], c);
		mmData[6] = _mm_mul_ps(mmData[6], c);
	}
	for(; i < d.len; ++i, mmData += 2)
		mmData[0] = _mm_mul_ps(mmData[0], c);
}
#endif

static void decompress_step1_97(const Params97& d, const float c)
{
#ifdef __SSE__
	decompress_step1_sse_97(d, _mm_set1_ps(c));
#else
	float* GRK_RESTRICT fw = (float*)d.data;

	for(uint32_t i = 0; i < d.len; ++i, fw += 8)
	{
		fw[0] *= c;
		fw[1] *= c;
		fw[2] *= c;
		fw[3] *= c;
		;
	}
#endif
}

#ifdef __SSE__
static void decompress_step2_sse_97(const Params97& d, __m128 c)
{
	__m128* GRK_RESTRICT vec_data = (__m128*)d.data;

	uint32_t imax = (std::min<uint32_t>)(d.len, d.lenMax);

	// initial tmp1 value is only necessary when
	// absolute start of line is at 0
	auto tmp1 = ((__m128*)d.dataPrev)[0];
	uint32_t i = 0;
	for(; i + 3 < imax; i += 4)
	{
		auto tmp2 = vec_data[-1];
		auto tmp3 = vec_data[0];
		auto tmp4 = vec_data[1];
		auto tmp5 = vec_data[2];
		auto tmp6 = vec_data[3];
		auto tmp7 = vec_data[4];
		auto tmp8 = vec_data[5];
		auto tmp9 = vec_data[6];
		vec_data[-1] = _mm_add_ps(tmp2, _mm_mul_ps(_mm_add_ps(tmp1, tmp3), c));
		vec_data[1] = _mm_add_ps(tmp4, _mm_mul_ps(_mm_add_ps(tmp3, tmp5), c));
		vec_data[3] = _mm_add_ps(tmp6, _mm_mul_ps(_mm_add_ps(tmp5, tmp7), c));
		vec_data[5] = _mm_add_ps(tmp8, _mm_mul_ps(_mm_add_ps(tmp7, tmp9), c));
		tmp1 = tmp9;
		vec_data += 8;
	}

	for(; i < imax; ++i)
	{
		auto tmp2 = vec_data[-1];
		auto tmp3 = vec_data[0];
		vec_data[-1] = _mm_add_ps(tmp2, _mm_mul_ps(_mm_add_ps(tmp1, tmp3), c));
		tmp1 = tmp3;
		vec_data += 2;
	}
	if(d.lenMax < d.len)
	{
		assert(d.lenMax + 1 == d.len);
		c = _mm_add_ps(c, c);
		c = _mm_mul_ps(c, vec_data[-2]);
		vec_data[-1] = _mm_add_ps(vec_data[-1], c);
	}
}
#endif

static void decompress_step2_97(const Params97& d, float c)
{
#ifdef __SSE__
	decompress_step2_sse_97(d, _mm_set1_ps(c));
#else

	float* dataPrev = (float*)d.dataPrev;
	float* data = (float*)d.data;

	uint32_t imax = (std::min<uint32_t>)(d.len, d.lenMax);
	for(uint32_t i = 0; i < imax; ++i)
	{
		float tmp1_1 = dataPrev[0];
		float tmp1_2 = dataPrev[1];
		float tmp1_3 = dataPrev[2];
		float tmp1_4 = dataPrev[3];
		float tmp2_1 = data[-4];
		float tmp2_2 = data[-3];
		float tmp2_3 = data[-2];
		float tmp2_4 = data[-1];
		float tmp3_1 = data[0];
		float tmp3_2 = data[1];
		float tmp3_3 = data[2];
		float tmp3_4 = data[3];
		data[-4] = tmp2_1 + ((tmp1_1 + tmp3_1) * c);
		data[-3] = tmp2_2 + ((tmp1_2 + tmp3_2) * c);
		data[-2] = tmp2_3 + ((tmp1_3 + tmp3_3) * c);
		data[-1] = tmp2_4 + ((tmp1_4 + tmp3_4) * c);
		dataPrev = data;
		data += 8;
	}
	if(d.lenMax < d.len)
	{
		assert(d.lenMax + 1 == d.len);
		c += c;
		data[-4] = data[-4] + dataPrev[0] * c;
		data[-3] = data[-3] + dataPrev[1] * c;
		data[-2] = data[-2] + dataPrev[2] * c;
		data[-1] = data[-1] + dataPrev[3] * c;
	}
#endif
}

/* <summary>                             */
/* Inverse 9-7 wavelet transform in 1-D. */
/* </summary>                            */
static void decompress_step_97(dwt_data<vec4f>* GRK_RESTRICT dwt)
{
	if((!dwt->parity && dwt->dn_full == 0 && dwt->sn_full <= 1) ||
	   (dwt->parity && dwt->sn_full == 0 && dwt->dn_full >= 1))
		return;

	decompress_step1_97(makeParams97(dwt, true, true), K);
	decompress_step1_97(makeParams97(dwt, false, true), twice_invK);
	decompress_step2_97(makeParams97(dwt, true, false), dwt_delta);
	decompress_step2_97(makeParams97(dwt, false, false), dwt_gamma);
	decompress_step2_97(makeParams97(dwt, true, false), dwt_beta);
	decompress_step2_97(makeParams97(dwt, false, false), dwt_alpha);
}

static void interleave_h_97(dwt_data<vec4f>* GRK_RESTRICT dwt, float* GRK_RESTRICT bandL,
							const uint32_t strideL, float* GRK_RESTRICT bandH,
							const uint32_t strideH, uint32_t remaining_height)
{
	float* GRK_RESTRICT bi = (float*)(dwt->mem + dwt->parity);
	uint32_t x0 = dwt->win_l.x0;
	uint32_t x1 = dwt->win_l.x1;
	const size_t vec4f_elts = sizeof(vec4f) / sizeof(float);
	for(uint32_t k = 0; k < 2; ++k)
	{
		auto band = (k == 0) ? bandL : bandH;
		uint32_t stride = (k == 0) ? strideL : strideH;
		if(remaining_height >= vec4f_elts && ((size_t)band & 0x0f) == 0 &&
		   ((size_t)bi & 0x0f) == 0 && (stride & 0x0f) == 0)
		{
			/* Fast code path */
			for(uint32_t i = x0; i < x1; ++i, bi += vec4f_elts * 2)
			{
				uint32_t j = i;
				bi[0] = band[j];
				j += stride;
				bi[1] = band[j];
				j += stride;
				bi[2] = band[j];
				j += stride;
				bi[3] = band[j];
			}
		}
		else
		{
			/* Slow code path */
			for(uint32_t i = x0; i < x1; ++i, bi += vec4f_elts * 2)
			{
				uint32_t j = i;
				bi[0] = band[j];
				j += stride;
				if(remaining_height == 1)
					continue;
				bi[1] = band[j];
				j += stride;
				if(remaining_height == 2)
					continue;
				bi[2] = band[j];
				j += stride;
				if(remaining_height == 3)
					continue;
				bi[3] = band[j];
			}
		}

		bi = (float*)(dwt->mem + 1 - dwt->parity);
		x0 = dwt->win_h.x0;
		x1 = dwt->win_h.x1;
	}
}

static void decompress_h_strip_97(dwt_data<vec4f>* GRK_RESTRICT horiz, const uint32_t rh,
								  float* GRK_RESTRICT bandL, const uint32_t strideL,
								  float* GRK_RESTRICT bandH, const uint32_t strideH, float* dest,
								  const size_t strideDest)
{
	uint32_t j;
	const size_t vec4f_elts = sizeof(vec4f) / sizeof(float);
	for(j = 0; j < (rh & (uint32_t)(~(vec4f_elts - 1))); j += vec4f_elts)
	{
		interleave_h_97(horiz, bandL, strideL, bandH, strideH, rh - j);
		decompress_step_97(horiz);
		for(uint32_t k = 0; k < horiz->sn_full + horiz->dn_full; k++)
		{
			dest[k] = horiz->mem[k].f[0];
			dest[k + (size_t)strideDest] = horiz->mem[k].f[1];
			dest[k + (size_t)strideDest * 2] = horiz->mem[k].f[2];
			dest[k + (size_t)strideDest * 3] = horiz->mem[k].f[3];
		}
		bandL += strideL << 2;
		bandH += strideH << 2;
		dest += strideDest << 2;
	}
	if(j < rh)
	{
		interleave_h_97(horiz, bandL, strideL, bandH, strideH, rh - j);
		decompress_step_97(horiz);
		for(uint32_t k = 0; k < horiz->sn_full + horiz->dn_full; k++)
		{
			switch(rh - j)
			{
				case 3:
					dest[k + strideDest * 2] = horiz->mem[k].f[2];
				/* FALLTHRU */
				case 2:
					dest[k + strideDest] = horiz->mem[k].f[1];
				/* FALLTHRU */
				case 1:
					dest[k] = horiz->mem[k].f[0];
			}
		}
	}
}
static bool decompress_h_mt_97(uint32_t num_threads, size_t data_size,
							   dwt_data<vec4f>& GRK_RESTRICT horiz, const uint32_t rh,
							   float* GRK_RESTRICT bandL, const uint32_t strideL,
							   float* GRK_RESTRICT bandH, const uint32_t strideH,
							   float* GRK_RESTRICT dest, const uint32_t strideDest)
{
	uint32_t num_jobs = num_threads;
	if(rh < num_jobs)
		num_jobs = rh;
	uint32_t step_j = num_jobs ? (rh / num_jobs) : 0;
	const size_t vec4f_elts = sizeof(vec4f) / sizeof(float);
	if(num_threads == 1 || step_j < vec4f_elts)
	{
		decompress_h_strip_97(&horiz, rh, bandL, strideL, bandH, strideH, dest, strideDest);
	}
	else
	{
		std::vector<std::future<int>> results;
		for(uint32_t j = 0; j < num_jobs; ++j)
		{
			auto min_j = j * step_j;
			auto job = new decompress_job<float, dwt_data<vec4f>>(
				horiz, bandL + min_j * strideL, strideL, bandH + min_j * strideH, strideH, nullptr,
				0, nullptr, 0, dest + min_j * strideDest, strideDest, 0,
				(j < (num_jobs - 1U) ? (j + 1U) * step_j : rh) - min_j);
			if(!job->data.alloc(data_size))
			{
				GRK_ERROR("Out of memory");
				horiz.release();
				return false;
			}
			results.emplace_back(ThreadPool::get()->enqueue([job] {
				decompress_h_strip_97(&job->data, job->max_j, job->bandLL, job->strideLL,
									  job->bandHL, job->strideHL, job->dest, job->strideDest);
				job->data.release();
				delete job;
				return 0;
			}));
		}
		for(auto& result : results)
			result.get();
	}
	return true;
}

static void interleave_v_97(dwt_data<vec4f>* GRK_RESTRICT dwt, float* GRK_RESTRICT bandL,
							const uint32_t strideL, float* GRK_RESTRICT bandH,
							const uint32_t strideH, uint32_t nb_elts_read)
{
	vec4f* GRK_RESTRICT bi = dwt->mem + dwt->parity;
	auto band = bandL + dwt->win_l.x0 * strideL;
	for(uint32_t i = dwt->win_l.x0; i < dwt->win_l.x1; ++i, bi += 2)
	{
		memcpy((float*)bi, band, nb_elts_read * sizeof(float));
		band += strideL;
	}

	bi = dwt->mem + 1 - dwt->parity;
	band = bandH + dwt->win_h.x0 * strideH;
	for(uint32_t i = dwt->win_h.x0; i < dwt->win_h.x1; ++i, bi += 2)
	{
		memcpy((float*)bi, band, nb_elts_read * sizeof(float));
		band += strideH;
	}
}
static void decompress_v_strip_97(dwt_data<vec4f>* GRK_RESTRICT vert, const uint32_t rw,
								  const uint32_t rh, float* GRK_RESTRICT bandL,
								  const uint32_t strideL, float* GRK_RESTRICT bandH,
								  const uint32_t strideH, float* GRK_RESTRICT dest,
								  const uint32_t strideDest)
{
	uint32_t j;
	const size_t vec4f_elts = sizeof(vec4f) / sizeof(float);
	for(j = 0; j < (rw & (uint32_t) ~(vec4f_elts - 1)); j += vec4f_elts)
	{
		interleave_v_97(vert, bandL, strideL, bandH, strideH, vec4f_elts);
		decompress_step_97(vert);
		auto destPtr = dest;
		for(uint32_t k = 0; k < rh; ++k)
		{
			memcpy(destPtr, vert->mem + k, sizeof(vec4f));
			destPtr += strideDest;
		}
		bandL += vec4f_elts;
		bandH += vec4f_elts;
		dest += vec4f_elts;
	}
	if(j < rw)
	{
		j = rw & (vec4f_elts - 1);
		interleave_v_97(vert, bandL, strideL, bandH, strideH, j);
		decompress_step_97(vert);
		auto destPtr = dest;
		for(uint32_t k = 0; k < rh; ++k)
		{
			memcpy(destPtr, vert->mem + k, j * sizeof(float));
			destPtr += strideDest;
		}
	}
}

static bool decompress_v_mt_97(uint32_t num_threads, size_t data_size,
							   dwt_data<vec4f>& GRK_RESTRICT vert, const uint32_t rw,
							   const uint32_t rh, float* GRK_RESTRICT bandL, const uint32_t strideL,
							   float* GRK_RESTRICT bandH, const uint32_t strideH,
							   float* GRK_RESTRICT dest, const uint32_t strideDest)
{
	auto num_jobs = (uint32_t)num_threads;
	if(rw < num_jobs)
		num_jobs = rw;
	const size_t vec4f_elts = sizeof(vec4f) / sizeof(float);
	auto step_j = num_jobs ? (rw / num_jobs) : 0;
	if(num_threads == 1 || step_j < vec4f_elts)
	{
		decompress_v_strip_97(&vert, rw, rh, bandL, strideL, bandH, strideH, dest, strideDest);
	}
	else
	{
		std::vector<std::future<int>> results;
		for(uint32_t j = 0; j < num_jobs; j++)
		{
			auto min_j = j * step_j;
			auto job = new decompress_job<float, dwt_data<vec4f>>(
				vert, bandL + min_j, strideL, nullptr, 0, bandH + min_j, strideH, nullptr, 0,
				dest + min_j, strideDest, 0,
				(j < (num_jobs - 1U) ? (j + 1U) * step_j : rw) - min_j);
			if(!job->data.alloc(data_size))
			{
				GRK_ERROR("Out of memory");
				vert.release();
				return false;
			}
			results.emplace_back(ThreadPool::get()->enqueue([job, rh] {
				decompress_v_strip_97(&job->data, job->max_j, rh, job->bandLL, job->strideLL,
									  job->bandLH, job->strideLH, job->dest, job->strideDest);
				job->data.release();
				delete job;
				return 0;
			}));
		}
		for(auto& result : results)
			result.get();
	}

	return true;
}

/* <summary>                             */
/* Inverse 9-7 wavelet transform in 2-D. */
/* </summary>                            */
static bool decompress_tile_97(TileComponent* GRK_RESTRICT tilec, uint32_t numres)
{
	if(numres == 1U)
		return true;

	auto tr = tilec->tileCompResolution;
	uint32_t rw = tr->width();
	uint32_t rh = tr->height();

	size_t data_size = max_resolution(tr, numres);
	dwt_data<vec4f> horiz;
	dwt_data<vec4f> vert;
	if(!horiz.alloc(data_size))
	{
		GRK_ERROR("Out of memory");
		return false;
	}
	vert.mem = horiz.mem;
	uint32_t num_threads = (uint32_t)ThreadPool::get()->num_threads();
	for(uint8_t res = 1; res < numres; ++res)
	{
		horiz.sn_full = rw;
		vert.sn_full = rh;
		++tr;
		rw = tr->width();
		rh = tr->height();
		if(rw == 0 || rh == 0)
			continue;
		horiz.dn_full = rw - horiz.sn_full;
		horiz.parity = tr->x0 & 1;
		horiz.win_l = grkLineU32(0, horiz.sn_full);
		horiz.win_h = grkLineU32(0, horiz.dn_full);
		if(!decompress_h_mt_97(
			   num_threads, data_size, horiz, vert.sn_full,
			   // LL
			   (float*)tilec->getBuffer()->getResWindowBufferREL(res - 1U)->getBuffer(),
			   tilec->getBuffer()->getResWindowBufferREL(res - 1U)->stride,
			   // HL
			   (float*)tilec->getBuffer()->getBandWindowBufferPaddedREL(res, BAND_ORIENT_HL)->getBuffer(),
			   tilec->getBuffer()->getBandWindowBufferPaddedREL(res, BAND_ORIENT_HL)->stride,
			   // lower split window
			   (float*)tilec->getBuffer()->getResWindowBufferSplitREL(res, SPLIT_L)->getBuffer(),
			   tilec->getBuffer()->getResWindowBufferSplitREL(res, SPLIT_L)->stride))
			return false;
		if(!decompress_h_mt_97(
			   num_threads, data_size, horiz, rh - vert.sn_full,
			   // LH
			   (float*)tilec->getBuffer()->getBandWindowBufferPaddedREL(res, BAND_ORIENT_LH)->getBuffer(),
			   tilec->getBuffer()->getBandWindowBufferPaddedREL(res, BAND_ORIENT_LH)->stride,
			   // HH
			   (float*)tilec->getBuffer()->getBandWindowBufferPaddedREL(res, BAND_ORIENT_HH)->getBuffer(),
			   tilec->getBuffer()->getBandWindowBufferPaddedREL(res, BAND_ORIENT_HH)->stride,
			   // higher split window
			   (float*)tilec->getBuffer()->getResWindowBufferSplitREL(res, SPLIT_H)->getBuffer(),
			   tilec->getBuffer()->getResWindowBufferSplitREL(res, SPLIT_H)->stride))
			return false;
		vert.dn_full = rh - vert.sn_full;
		vert.parity = tr->y0 & 1;
		vert.win_l = grkLineU32(0, vert.sn_full);
		vert.win_h = grkLineU32(0, vert.dn_full);
		if(!decompress_v_mt_97(
			   num_threads, data_size, vert, rw, rh,
			   // lower split window
			   (float*)tilec->getBuffer()->getResWindowBufferSplitREL(res, SPLIT_L)->getBuffer(),
			   tilec->getBuffer()->getResWindowBufferSplitREL(res, SPLIT_L)->stride,
			   // higher split window
			   (float*)tilec->getBuffer()->getResWindowBufferSplitREL(res, SPLIT_H)->getBuffer(),
			   tilec->getBuffer()->getResWindowBufferSplitREL(res, SPLIT_H)->stride,
			   // resolution window
			   (float*)tilec->getBuffer()->getResWindowBufferREL(res)->getBuffer(),
			   tilec->getBuffer()->getResWindowBufferREL(res)->stride))
			return false;
	}
	horiz.release();
	return true;
}

/**
 * ************************************************************************************
 *
 * 5/3 operates on elements of type int32_t while 9/7 operates on elements of type vec4f
 *
 * Horizontal pass
 *
 * Each thread processes a strip running the length of the window, with height
 *   5/3
 *   Height : sizeof(T)/sizeof(int32_t)
 *
 *   9/7
 *   Height : sizeof(T)/sizeof(int32_t)
 *
 * Vertical pass
 *
 * Each thread processes a strip running the height of the window, with width
 *
 *  5/3
 *  Width :  4
 *
 *  9/7
 *  Width :  4
 *
 ****************************************************************************/
template<typename T, uint32_t FILTER_WIDTH, uint32_t VERT_PASS_WIDTH>
class PartialInterleaver
{
  public:
	/**
	 * interleaved data is laid out in the dwt->mem buffer in increments of h_chunk
	 */
	void interleave_h(dwt_data<T>* dwt, ISparseCanvas* sa, uint32_t y_offset, uint32_t height)
	{
		const uint32_t h_chunk = (uint32_t)(sizeof(T) / sizeof(int32_t));
		for(uint32_t i = 0; i < height; i++)
		{
			bool ret = false;

			// read one row of L band and write interleaved
			if(dwt->sn_full)
			{
				ret = sa->read(
					dwt->resno,BAND_ORIENT_LL,
					grkRectU32(dwt->win_l.x0, y_offset + i,
							   std::min<uint32_t>(dwt->win_l.x1 + FILTER_WIDTH, dwt->sn_full),
							   y_offset + i + 1),
					(int32_t*)dwt->memL + i, 2 * h_chunk, 0, true);
				assert(ret);
			}
			// read one row of H band and write interleaved
			if(dwt->dn_full)
			{
				ret = sa->read(
					dwt->resno,BAND_ORIENT_LL,
					grkRectU32(dwt->sn_full + dwt->win_h.x0, y_offset + i,
							   dwt->sn_full +
								   std::min<uint32_t>(dwt->win_h.x1 + FILTER_WIDTH, dwt->dn_full),
							   y_offset + i + 1),
					(int32_t*)dwt->memH + i, 2 * h_chunk, 0, true);
				assert(ret);
			}
			GRK_UNUSED(ret);
		}
	}
	/*
	 * interleaved data is laid out in the dwt->mem buffer in
	 * v_chunk lines
	 */
	void interleave_v(dwt_data<T>* GRK_RESTRICT dwt, ISparseCanvas* sa, uint32_t x_offset,
					  uint32_t x_num_elements)
	{
		const uint32_t v_chunk = (uint32_t)(sizeof(T) / sizeof(int32_t)) * VERT_PASS_WIDTH;
		// read one vertical strip (of width x_num_elements <= v_chunk) of L band and write
		// interleaved
		bool ret = false;

		if(dwt->sn_full)
		{
			ret =
				sa->read(dwt->resno,BAND_ORIENT_LL,
						 grkRectU32(x_offset, dwt->win_l.x0, x_offset + x_num_elements,
									std::min<uint32_t>(dwt->win_l.x1 + FILTER_WIDTH, dwt->sn_full)),
						 (int32_t*)dwt->memL, 1, 2 * v_chunk, true);
			assert(ret);
		}
		// read one vertical strip (of width x_num_elements <= v_chunk) of H band and write
		// interleaved
		if(dwt->dn_full)
		{
			ret = sa->read(
				dwt->resno, BAND_ORIENT_LL,
				grkRectU32(x_offset, dwt->sn_full + dwt->win_h.x0, x_offset + x_num_elements,
						   dwt->sn_full +
							   std::min<uint32_t>(dwt->win_h.x1 + FILTER_WIDTH, dwt->dn_full)),
				(int32_t*)dwt->memH, 1, 2 * v_chunk, true);
			assert(ret);
		}
		GRK_UNUSED(ret);
	}
};

template<typename T, uint32_t FILTER_WIDTH, uint32_t VERT_PASS_WIDTH>
class Partial53 : public PartialInterleaver<T, FILTER_WIDTH, VERT_PASS_WIDTH>
{
  public:
	void decompress_h(dwt_data<T>* dwt)
	{
#ifndef GRK_DEBUG_SPARSE
#define get_S(buf, i) buf[(i) << 1]
#define get_D(buf, i) buf[(1 + ((i) << 1))]
#endif

#define S(buf, i) buf[(i) << 1]
#define D(buf, i) buf[(1 + ((i) << 1))]

// parity == 0
#define S_(buf, i) \
	((i) < -win_l_x0 ? get_S(buf, -win_l_x0) : ((i) >= sn ? get_S(buf, sn - 1) : get_S(buf, i)))
#define D_(buf, i) \
	((i) < -win_h_x0 ? get_D(buf, -win_h_x0) : ((i) >= dn ? get_D(buf, dn - 1) : get_D(buf, i)))

// parity == 1
#define SS_(buf, i) \
	((i) < -win_h_x0 ? get_S(buf, -win_h_x0) : ((i) >= dn ? get_S(buf, dn - 1) : get_S(buf, i)))
#define DD_(buf, i) \
	((i) < -win_l_x0 ? get_D(buf, -win_l_x0) : ((i) >= sn ? get_D(buf, sn - 1) : get_D(buf, i)))

		int64_t i;
		int64_t parity = dwt->parity;
		int64_t win_l_x0 = dwt->win_l.x0;
		int64_t win_l_x1 = dwt->win_l.x1;
		int64_t win_h_x0 = dwt->win_h.x0;
		int64_t win_h_x1 = dwt->win_h.x1;
		assert(dwt->win_l.x0 <= dwt->sn_full);
		int64_t sn = (int64_t)dwt->sn_full - (int64_t)dwt->win_l.x0;
		int64_t sn_full = dwt->sn_full;
		assert(dwt->win_h.x0 <= dwt->dn_full);
		int64_t dn = (int64_t)dwt->dn_full - (int64_t)dwt->win_h.x0;
		int64_t dn_full = dwt->dn_full;

		adjust_bounds(dwt, sn_full, dn_full, &sn, &dn);

		assert(dwt->win_l.x1 <= sn_full && dwt->win_h.x1 <= dn_full);

		auto buf = dwt->mem;
		if(!parity)
		{
			if((dn_full != 0) || (sn_full > 1))
			{
				/* Naive version is :
				for (i = win_l_x0; i < i_max; i++) {
					S(i) -= (D_(i - 1) + D_(i) + 2) >> 2;
				}
				for (i = win_h_x0; i < win_h_x1; i++) {
					D(i) += (S_(i) + S_(i + 1)) >> 1;
				}
				but the compiler doesn't manage to unroll it to avoid bound
				checking in S_ and D_ macros
				*/
				i = 0;
				int64_t i_max = win_l_x1 - win_l_x0;
				if(i < i_max)
				{
					/* Left-most case */
					S(buf, i) -= (D_(buf, i - 1) + D_(buf, i) + 2) >> 2;
					i++;

					if(i_max > dn)
						i_max = dn;
					for(; i < i_max; i++)
						/* No bound checking */
						S(buf, i) -= (get_D(buf, i - 1) + get_D(buf, i) + 2) >> 2;
					for(; i < win_l_x1 - win_l_x0; i++)
						/* Right-most case */
						S(buf, i) -= (D_(buf, i - 1) + D_(buf, i) + 2) >> 2;
				}
				i = 0;
				i_max = win_h_x1 - win_h_x0;
				if(i < i_max)
				{
					if(i_max >= sn)
						i_max = sn - 1;
					for(; i < i_max; i++)
						/* No bound checking */
						D(buf, i) += (S(buf, i) + S(buf, i + 1)) >> 1;
					for(; i < win_h_x1 - win_h_x0; i++)
						/* Right-most case */
						D(buf, i) += (S_(buf, i) + S_(buf, i + 1)) >> 1;
				}
			}
		}
		else
		{
			if(sn_full == 0 && dn_full == 1)
			{
				// only do L band (high pass)
				S(buf, 0) >>= 1;
			}
			else
			{
				for(i = 0; i < win_l_x1 - win_l_x0; i++)
					D(buf, i) -= (SS_(buf, i) + SS_(buf, i + 1) + 2) >> 2;
				for(i = 0; i < win_h_x1 - win_h_x0; i++)
					S(buf, i) += (DD_(buf, i) + DD_(buf, i - 1)) >> 1;
			}
		}
	}
	void decompress_v(dwt_data<T>* dwt)
	{
#ifndef GRK_DEBUG_SPARSE
#define get_S_off(buf, i, off) buf[(i)*2 * VERT_PASS_WIDTH + off]
#define get_D_off(buf, i, off) buf[(1 + (i)*2) * VERT_PASS_WIDTH + off]
#endif

#define S_off(buf, i, off) buf[(i)*2 * VERT_PASS_WIDTH + off]
#define D_off(buf, i, off) buf[(1 + (i)*2) * VERT_PASS_WIDTH + off]

// parity == 0
#define S_off_(buf, i, off) (((i) >= sn ? get_S_off(buf, sn - 1, off) : get_S_off(buf, i, off)))
#define D_off_(buf, i, off) (((i) >= dn ? get_D_off(buf, dn - 1, off) : get_D_off(buf, i, off)))

#define S_sgnd_off_(buf, i, off) \
	(((i) < (-win_l_x0) ? get_S_off(buf, -win_l_x0, off) : S_off_(buf, i, off)))
#define D_sgnd_off_(buf, i, off) \
	(((i) < (-win_h_x0) ? get_D_off(buf, -win_h_x0, off) : D_off_(buf, i, off)))

// case == 1
#define SS_sgnd_off_(buf, i, off)                       \
	((i) < (-win_h_x0) ? get_S_off(buf, -win_h_x0, off) \
					   : ((i) >= dn ? get_S_off(buf, dn - 1, off) : get_S_off(buf, i, off)))
#define DD_sgnd_off_(buf, i, off)                       \
	((i) < (-win_l_x0) ? get_D_off(buf, -win_l_x0, off) \
					   : ((i) >= sn ? get_D_off(buf, sn - 1, off) : get_D_off(buf, i, off)))

#define SS_off_(buf, i, off) (((i) >= dn ? get_S_off(buf, dn - 1, off) : get_S_off(buf, i, off)))
#define DD_off_(buf, i, off) (((i) >= sn ? get_D_off(buf, sn - 1, off) : get_D_off(buf, i, off)))

		int64_t i;
		int64_t parity = dwt->parity;
		int64_t win_l_x0 = dwt->win_l.x0;
		int64_t win_l_x1 = dwt->win_l.x1;
		int64_t win_h_x0 = dwt->win_h.x0;
		int64_t win_h_x1 = dwt->win_h.x1;
		int64_t sn = (int64_t)dwt->sn_full - (int64_t)dwt->win_l.x0;
		int64_t sn_full = dwt->sn_full;
		int64_t dn = (int64_t)dwt->dn_full - (int64_t)dwt->win_h.x0;
		int64_t dn_full = dwt->dn_full;

		adjust_bounds(dwt, sn_full, dn_full, &sn, &dn);

		assert(dwt->win_l.x1 <= sn_full && dwt->win_h.x1 <= dn_full);

		auto buf = dwt->mem;
		if(!parity)
		{
			if((dn_full != 0) || (sn_full > 1))
			{
				/* Naive version is :
				for (i = win_l_x0; i < i_max; i++) {
					S(i) -= (D_(i - 1) + D_(i) + 2) >> 2;
				}
				for (i = win_h_x0; i < win_h_x1; i++) {
					D(i) += (S_(i) + S_(i + 1)) >> 1;
				}
				but the compiler doesn't manage to unroll it to avoid bound
				checking in S_ and D_ macros
				*/

				// 1. low pass
				i = 0;
				int64_t i_max = win_l_x1 - win_l_x0;
				assert(win_l_x1 >= win_l_x0);
				if(i < i_max)
				{
					/* Left-most case */
					for(int64_t off = 0; off < VERT_PASS_WIDTH; off++)
						S_off(buf, i, off) -=
							(D_sgnd_off_(buf, i - 1, off) + D_off_(buf, i, off) + 2) >> 2;
					i++;
					if(i_max > dn)
						i_max = dn;
#ifdef __SSE2__
					if(i + 1 < i_max)
					{
						const __m128i two = _mm_set1_epi32(2);
						auto Dm1 = _mm_load_si128((__m128i*)(buf + (2 * i - 1) * VERT_PASS_WIDTH));
						for(; i + 1 < i_max; i += 2)
						{
							/* No bound checking */
							auto S = _mm_load_si128((__m128i*)(buf + (i * 2) * VERT_PASS_WIDTH));
							auto D =
								_mm_load_si128((__m128i*)(buf + (i * 2 + 1) * VERT_PASS_WIDTH));
							auto S1 =
								_mm_load_si128((__m128i*)(buf + (i * 2 + 2) * VERT_PASS_WIDTH));
							auto D1 =
								_mm_load_si128((__m128i*)(buf + (i * 2 + 3) * VERT_PASS_WIDTH));
							S = _mm_sub_epi32(
								S, _mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(Dm1, D), two), 2));
							S1 = _mm_sub_epi32(
								S1, _mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(D, D1), two), 2));
							_mm_store_si128((__m128i*)(buf + i * 2 * VERT_PASS_WIDTH), S);
							_mm_store_si128((__m128i*)(buf + (i + 1) * 2 * VERT_PASS_WIDTH), S1);
							Dm1 = D1;
						}
					}
#endif
					for(; i < i_max; i++)
					{
						/* No bound checking */
						for(uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
							S_off(buf, i, off) -=
								(D_sgnd_off_(buf, i - 1, off) + D_off(buf, i, off) + 2) >> 2;
					}
					for(; i < win_l_x1 - win_l_x0; i++)
					{
						/* Right-most case */
						for(uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
							S_off(buf, i, off) -=
								(D_sgnd_off_(buf, i - 1, off) + D_off_(buf, i, off) + 2) >> 2;
					}
				}

				// 2. high pass
				i = 0;
				assert(win_h_x1 >= win_h_x0);
				i_max = win_h_x1 - win_h_x0;
				if(i < i_max)
				{
					if(i_max >= sn)
						i_max = sn - 1;
#ifdef __SSE2__
					if(i + 1 < i_max)
					{
						auto S = _mm_load_si128((__m128i*)(buf + i * 2 * VERT_PASS_WIDTH));
						for(; i + 1 < i_max; i += 2)
						{
							/* No bound checking */
							auto D =
								_mm_load_si128((__m128i*)(buf + (1 + i * 2) * VERT_PASS_WIDTH));
							auto S1 =
								_mm_load_si128((__m128i*)(buf + ((i + 1) * 2) * VERT_PASS_WIDTH));
							auto D1 = _mm_load_si128(
								(__m128i*)(buf + (1 + (i + 1) * 2) * VERT_PASS_WIDTH));
							auto S2 =
								_mm_load_si128((__m128i*)(buf + ((i + 2) * 2) * VERT_PASS_WIDTH));
							D = _mm_add_epi32(D, _mm_srai_epi32(_mm_add_epi32(S, S1), 1));
							D1 = _mm_add_epi32(D1, _mm_srai_epi32(_mm_add_epi32(S1, S2), 1));
							_mm_store_si128((__m128i*)(buf + (1 + i * 2) * VERT_PASS_WIDTH), D);
							_mm_store_si128((__m128i*)(buf + (1 + (i + 1) * 2) * VERT_PASS_WIDTH),
											D1);
							S = S2;
						}
					}
#endif
					for(; i < i_max; i++)
					{
						/* No bound checking */
						for(uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
							D_off(buf, i, off) +=
								(S_off(buf, i, off) + S_off(buf, i + 1, off)) >> 1;
					}
					for(; i < win_h_x1 - win_h_x0; i++)
					{
						/* Right-most case */
						for(uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
							D_off(buf, i, off) +=
								(S_off_(buf, i, off) + S_off_(buf, i + 1, off)) >> 1;
					}
				}
			}
		}
		else
		{
			if(sn_full == 0 && dn_full == 1)
			{
				// edge case at origin
				for(uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
					S_off(buf, 0, off) >>= 1;
			}
			else
			{
				assert((uint64_t)(dwt->memL + (win_l_x1 - win_l_x0) * VERT_PASS_WIDTH) -
						   (uint64_t)dwt->allocatedMem <
					   dwt->m_lenBytes);
				for(i = 0; i < win_l_x1 - win_l_x0; i++)
				{
					for(uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
						D_off(buf, i, off) -=
							(SS_off_(buf, i, off) + SS_off_(buf, i + 1, off) + 2) >> 2;
				}
				assert((uint64_t)(dwt->memH + (win_h_x1 - win_h_x0) * VERT_PASS_WIDTH) -
						   (uint64_t)dwt->allocatedMem <
					   dwt->m_lenBytes);
				for(i = 0; i < win_h_x1 - win_h_x0; i++)
				{
					for(uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
						S_off(buf, i, off) +=
							(DD_off_(buf, i, off) + DD_sgnd_off_(buf, i - 1, off)) >> 1;
				}
			}
		}
	}

  private:
	void adjust_bounds(dwt_data<T>* dwt, int64_t sn_full, int64_t dn_full, int64_t* sn, int64_t* dn)
	{
		(void)sn_full;
		(void)dn_full;
		if((uint64_t)dwt->memH < (uint64_t)dwt->memL && *sn == *dn)
		{
			assert(dn_full == sn_full - 1);
			(*dn)--;
		}
		if((uint64_t)dwt->memL < (uint64_t)dwt->memH && *sn == *dn)
		{
			assert(sn_full == dn_full - 1);
			(*sn)--;
		}
	}

#ifdef GRK_DEBUG_SPARSE
	inline T get_S(T* buf, int64_t i)
	{
		auto ret = buf[(i) << 1];
		assert(abs(ret) < 0xFFFFFFF);
		return ret;
	}
	inline T get_D(T* buf, int64_t i)
	{
		auto ret = buf[(1 + ((i) << 1))];
		assert(abs(ret) < 0xFFFFFFF);
		return ret;
	}
	inline T get_S_off(T* buf, int64_t i, int64_t off)
	{
		auto ret = buf[(i)*2 * VERT_PASS_WIDTH + off];
		assert(abs(ret) < 0xFFFFFFF);
		return ret;
	}
	inline T get_D_off(T* buf, int64_t i, int64_t off)
	{
		auto ret = buf[(1 + (i)*2) * VERT_PASS_WIDTH + off];
		assert(abs(ret) < 0xFFFFFFF);
		return ret;
	}
#endif
};

template<typename T, uint32_t FILTER_WIDTH, uint32_t VERT_PASS_WIDTH>
class Partial97 : public PartialInterleaver<T, FILTER_WIDTH, VERT_PASS_WIDTH>
{
  public:
	void decompress_h(dwt_data<T>* dwt)
	{
		decompress_step_97(dwt);
	}
	void decompress_v(dwt_data<T>* dwt)
	{
		decompress_step_97(dwt);
	}
};

// Notes:
// 1. line buffer 0 offset == dwt->win_l.x0
// 2. dwt->memL and dwt->memH are only set for partial decode
static Params97 makeParams97(dwt_data<vec4f>* dwt, bool isBandL, bool step1)
{
	Params97 rc;
	// band_0 specifies absolute start of line buffer
	int64_t band_0 = isBandL ? dwt->win_l.x0 : dwt->win_h.x0;
	int64_t band_1 = isBandL ? dwt->win_l.x1 : dwt->win_h.x1;
	auto memPartial = isBandL ? dwt->memL : dwt->memH;
	int64_t parityOffset = isBandL ? dwt->parity : !dwt->parity;
	int64_t lenMax = isBandL ? (std::min<int64_t>)(dwt->sn_full, (int64_t)dwt->dn_full - parityOffset)
							 : (std::min<int64_t>)(dwt->dn_full, (int64_t)dwt->sn_full - parityOffset);
	if(lenMax < 0)
		lenMax = 0;
	assert(lenMax >= band_0);
	lenMax -= band_0;
	rc.data = memPartial ? memPartial : dwt->mem;

	assert(!memPartial || (dwt->win_l.x1 <= dwt->sn_full && dwt->win_h.x1 <= dwt->dn_full));
	assert(band_1 >= band_0);

	rc.data += parityOffset + band_0 - dwt->win_l.x0;
	rc.len = (uint32_t)(band_1 - band_0);
	if(!step1)
	{
		rc.data += 1;
		rc.dataPrev = parityOffset ? rc.data - 2 : rc.data;
		rc.lenMax = (uint32_t)lenMax;
	}

	if(memPartial)
	{
		assert((uint64_t)rc.data >= (uint64_t)dwt->allocatedMem);
		assert((uint64_t)rc.data <= (uint64_t)dwt->allocatedMem + dwt->m_lenBytes);
	}

	return rc;
};

/**
 * ************************************************************************************
 *
 * 5/3 operates on elements of type int32_t while 9/7 operates on elements of type vec4f
 *
 * Horizontal pass
 *
 * Each thread processes a strip running the length of the window, of the following dimensions:
 *
 *   5/3
 *   Height : 1
 *
 *   9/7
 *   Height : 4
 *
 * Vertical pass
 *
 *  5/3
 *  Width :  4
 *
 *  9/7
 *  Height : 1
 *
 ****************************************************************************
 *
 * FILTER_WIDTH value matches the maximum left/right extension given in tables
 * F.2 and F.3 of the standard
 */
template<typename T, uint32_t FILTER_WIDTH, uint32_t VERT_PASS_WIDTH, typename D>

bool decompress_partial_tile(TileComponent* GRK_RESTRICT tilec, uint16_t compno, grkRectU32 bounds,
							 uint8_t numres, ISparseCanvas* sa)
{
	bool rc = false;
	bool ret = false;
	uint8_t numresolutions = tilec->numresolutions;
	auto fullRes = tilec->tileCompResolution;
	auto fullResTopLevel = tilec->tileCompResolution + numres - 1;
	if(!fullResTopLevel->width() || !fullResTopLevel->height())
	{
		return true;
	}

	const uint16_t debug_compno = 0;
	(void)debug_compno;
	const uint32_t HORIZ_PASS_HEIGHT = sizeof(T) / sizeof(int32_t);
	const uint32_t pad = FILTER_WIDTH * std::max<uint32_t>(HORIZ_PASS_HEIGHT, VERT_PASS_WIDTH) *
						 sizeof(T) / sizeof(int32_t);

	auto synthesisWindow = bounds;
	synthesisWindow = synthesisWindow.rectceildivpow2(numresolutions - 1U - (numres - 1U));

	assert(fullResTopLevel->intersection(synthesisWindow) == synthesisWindow);
	synthesisWindow =
		synthesisWindow.pan(-(int64_t)fullResTopLevel->x0, -(int64_t)fullResTopLevel->y0);

	if(numres == 1U)
	{
		// simply copy into tile component buffer
		bool ret = sa->read(0, BAND_ORIENT_LL, synthesisWindow,
							tilec->getBuffer()->getResWindowBufferHighestREL()->getBuffer(),
							1,
							tilec->getBuffer()->getResWindowBufferHighestREL()->stride,
							true);
		assert(ret);
		GRK_UNUSED(ret);
		return true;
	}

	D decompressor;
	size_t num_threads = ThreadPool::get()->num_threads();

	for(uint8_t resno = 1; resno < numres; resno++)
	{
		auto fullResLower = fullRes;
		dwt_data<T> horiz;
		dwt_data<T> vert;

		horiz.sn_full = fullResLower->width();
		vert.sn_full = fullResLower->height();
		++fullRes;
		horiz.dn_full = fullRes->width() - horiz.sn_full;
		horiz.parity = fullRes->x0 & 1;
		vert.dn_full = fullRes->height() - vert.sn_full;
		vert.parity = fullRes->y0 & 1;

		// 1. set up windows for horizontal and vertical passes
		grkRectU32 bandWindowRect[BAND_NUM_ORIENTATIONS];
		bandWindowRect[BAND_ORIENT_LL] =
			*((grkRectU32*)tilec->getBuffer()->getBandWindowBufferPaddedREL(resno, BAND_ORIENT_LL));
		bandWindowRect[BAND_ORIENT_HL] =
			*((grkRectU32*)tilec->getBuffer()->getBandWindowBufferPaddedREL(resno, BAND_ORIENT_HL));
		bandWindowRect[BAND_ORIENT_LH] =
			*((grkRectU32*)tilec->getBuffer()->getBandWindowBufferPaddedREL(resno, BAND_ORIENT_LH));
		bandWindowRect[BAND_ORIENT_HH] =
			*((grkRectU32*)tilec->getBuffer()->getBandWindowBufferPaddedREL(resno, BAND_ORIENT_HH));

		// band windows in band coordinates - needed to pre-allocate sparse blocks
		grkRectU32 tileBandWindowRect[BAND_NUM_ORIENTATIONS];
		tileBandWindowRect[BAND_ORIENT_LL] = bandWindowRect[BAND_ORIENT_LL];
		tileBandWindowRect[BAND_ORIENT_HL] =
			bandWindowRect[BAND_ORIENT_HL].pan(fullRes->tileBand[BAND_INDEX_LH].width(), 0);
		tileBandWindowRect[BAND_ORIENT_LH] =
			bandWindowRect[BAND_ORIENT_LH].pan(0, fullRes->tileBand[BAND_INDEX_HL].height());
		tileBandWindowRect[BAND_ORIENT_HH] = bandWindowRect[BAND_ORIENT_HH].pan(
			fullRes->tileBand[BAND_INDEX_LH].width(), fullRes->tileBand[BAND_INDEX_HL].height());
		// 2. pre-allocate sparse blocks
		for(uint32_t i = 0; i < BAND_NUM_ORIENTATIONS; ++i)
		{
			auto temp = tileBandWindowRect[i];
			if(!sa->alloc(temp.grow(2 * FILTER_WIDTH, fullRes->width(), fullRes->height()), false))
				goto cleanup;
		}
		auto resWindowRect = *((grkRectU32*)tilec->getBuffer()->getResWindowBufferREL(resno));
		if(!sa->alloc(resWindowRect, false))
			goto cleanup;
		// two windows formed by horizontal pass and used as input for vertical pass
		grkRectU32 splitWindowRect[SPLIT_NUM_ORIENTATIONS];
		splitWindowRect[SPLIT_L] =
			*((grkRectU32*)tilec->getBuffer()->getResWindowBufferSplitREL(resno, SPLIT_L));
		splitWindowRect[SPLIT_H] =
			*((grkRectU32*)tilec->getBuffer()->getResWindowBufferSplitREL(resno, SPLIT_H));
		for(uint32_t k = 0; k < SPLIT_NUM_ORIENTATIONS; ++k)
		{
			auto temp = splitWindowRect[k];
			if(!sa->alloc(temp.grow(2 * FILTER_WIDTH, fullRes->width(), fullRes->height()), false))
				goto cleanup;
		}

		auto executor_h = [resno, compno, sa, resWindowRect,
						   &decompressor](decompress_job<T, dwt_data<T>>* job) {
			(void)compno;
			(void)resno;
			for(uint32_t j = job->min_j; j < job->max_j; j += HORIZ_PASS_HEIGHT)
			{
				auto height = std::min<uint32_t>((uint32_t)HORIZ_PASS_HEIGHT, job->max_j - j);
#ifdef GRK_DEBUG_VALGRIND
				// GRK_INFO("H: compno = %d, resno = %d,y begin = %d, height = %d,", compno, resno,
				// j, height);
				uint32_t len =
					(job->data.win_l.length() + job->data.win_h.length()) * HORIZ_PASS_HEIGHT;
				(void)len;
				std::ostringstream ss;
#endif
				job->data.memL = job->data.mem + job->data.parity;
				job->data.memH = job->data.mem + (int64_t)(!job->data.parity) +
								 2 * ((int64_t)job->data.win_h.x0 - (int64_t)job->data.win_l.x0);
				decompressor.interleave_h(&job->data, sa, j, height);
#ifdef GRK_DEBUG_VALGRIND
				auto ptr = ((uint64_t)job->data.memL < (uint64_t)job->data.memH) ? job->data.memL
																				 : job->data.memH;
				ss << "H interleave : compno = " << (uint32_t)compno
				   << ", resno= " << (uint32_t)(resno) << ", x begin = " << j
				   << ", total samples = " << len;
				grk_memcheck_all<int32_t>((int32_t*)ptr, len, ss.str());
#endif
				job->data.memL = job->data.mem;
				job->data.memH =
					job->data.mem + ((int64_t)job->data.win_h.x0 - (int64_t)job->data.win_l.x0);
				decompressor.decompress_h(&job->data);
#ifdef GRK_DEBUG_VALGRIND
				ss.clear();
				ss << "H decompress uninitialized value: compno = " << (uint32_t)compno
				   << ", resno= " << (uint32_t)(resno) << ", x begin = " << j
				   << ", total samples = " << len;
				grk_memcheck_all<int32_t>((int32_t*)job->data.mem, len, ss.str());
#endif
				if(!sa->write(resno, BAND_ORIENT_LL,
						grkRectU32(resWindowRect.x0, j, resWindowRect.x1, j + height),
							  (int32_t*)(job->data.mem + (int64_t)resWindowRect.x0 -
										 2 * (int64_t)job->data.win_l.x0),
							  HORIZ_PASS_HEIGHT, 1, true))
				{
					GRK_ERROR("sparse array write failure");
					job->data.release();
					delete job;
					return 1;
				}
			}
			job->data.release();
			delete job;
			return 0;
		};

		auto executor_v = [compno, resno, sa, resWindowRect,
						   &decompressor](decompress_job<T, dwt_data<T>>* job) {
			(void)compno;
			(void)resno;
			for(uint32_t j = job->min_j; j < job->max_j; j += VERT_PASS_WIDTH)
			{
				auto width = std::min<uint32_t>(VERT_PASS_WIDTH, (job->max_j - j));
#ifdef GRK_DEBUG_VALGRIND
				// GRK_INFO("V: compno = %d, resno = %d, x begin = %d, width = %d", compno, resno,
				// j, width);
				uint32_t len =
					(job->data.win_l.length() + job->data.win_h.length()) * VERT_PASS_WIDTH;
				(void)len;
				std::ostringstream ss;
#endif
				job->data.memL = job->data.mem + (job->data.parity) * VERT_PASS_WIDTH;
				job->data.memH = job->data.mem +
								 ((!job->data.parity) +
								  2 * ((int64_t)job->data.win_h.x0 - (int64_t)job->data.win_l.x0)) *
									 VERT_PASS_WIDTH;
				decompressor.interleave_v(&job->data, sa, j, width);
#ifdef GRK_DEBUG_VALGRIND
				auto ptr = ((uint64_t)job->data.memL < (uint64_t)job->data.memH) ? job->data.memL
																				 : job->data.memH;
				ss << "V interleave: compno = " << (uint32_t)compno
				   << ", resno= " << (uint32_t)(resno) << ", x begin = " << j
				   << ", total samples = " << len;
				grk_memcheck_all<int32_t>((int32_t*)ptr, len, ss.str());
#endif
				job->data.memL = job->data.mem;
				job->data.memH =
					job->data.mem +
					((int64_t)job->data.win_h.x0 - (int64_t)job->data.win_l.x0) * VERT_PASS_WIDTH;
				decompressor.decompress_v(&job->data);
#ifdef GRK_DEBUG_VALGRIND
				ss.clear();
				ss << "V decompress: compno = " << (uint32_t)compno
				   << ", resno= " << (uint32_t)(resno) << ", x begin = " << j
				   << ", total samples = " << len;
				grk_memcheck_all<int32_t>((int32_t*)job->data.mem, len, ss.str());
#endif
				if(!sa->write(resno,BAND_ORIENT_LL,
							  grkRectU32(j, resWindowRect.y0, j + width,
										 resWindowRect.y0 + job->data.win_l.length() +
											 job->data.win_h.length()),
							  (int32_t*)(job->data.mem + ((int64_t)resWindowRect.y0 -
														  2 * (int64_t)job->data.win_l.x0) *
															 VERT_PASS_WIDTH),
							  1, VERT_PASS_WIDTH * sizeof(T) / sizeof(int32_t), true))
				{
					GRK_ERROR("Sparse array write failure");
					job->data.release();
					delete job;
					return 1;
				}
			}
			job->data.release();
			delete job;
			return 0;
		};

		// 3. calculate synthesis
		horiz.win_l = bandWindowRect[BAND_ORIENT_LL].dimX();
		horiz.win_h = bandWindowRect[BAND_ORIENT_HL].dimX();
		horiz.resno = resno;
		size_t data_size = (splitWindowRect[0].width() + 2 * FILTER_WIDTH) * HORIZ_PASS_HEIGHT;

		for(uint32_t k = 0; k < 2; ++k)
		{
			uint32_t num_jobs = (uint32_t)num_threads;
			uint32_t num_rows = splitWindowRect[k].height();
			if(num_rows < num_jobs)
				num_jobs = num_rows;
			uint32_t step_j = num_jobs ? (num_rows / num_jobs) : 0;
			if(num_threads == 1 || step_j < HORIZ_PASS_HEIGHT)
				num_jobs = 1;
			std::vector<std::future<int>> results;
			bool blockError = false;
			for(uint32_t j = 0; j < num_jobs; ++j)
			{
				auto job = new decompress_job<T, dwt_data<T>>(
					horiz, splitWindowRect[k].y0 + j * step_j,
					j < (num_jobs - 1U) ? splitWindowRect[k].y0 + (j + 1U) * step_j
										: splitWindowRect[k].y1);
				if(!job->data.alloc(data_size, pad))
				{
					GRK_ERROR("Out of memory");
					delete job;
					goto cleanup;
				}
				if(num_jobs > 1)
				{
					results.emplace_back(
						ThreadPool::get()->enqueue([job, executor_h] { return executor_h(job); }));
				}
				else
				{
					blockError = (executor_h(job) != 0);
				}
			}
			for(auto& result : results)
			{
				if(result.get() != 0)
					blockError = true;
			}
			if(blockError)
				goto cleanup;
		}

		data_size = (resWindowRect.height() + 2 * FILTER_WIDTH) * VERT_PASS_WIDTH * sizeof(T) /
					sizeof(int32_t);

		vert.win_l = bandWindowRect[BAND_ORIENT_LL].dimY();
		vert.win_h = bandWindowRect[BAND_ORIENT_LH].dimY();
		vert.resno = resno;
		uint32_t num_jobs = (uint32_t)num_threads;
		uint32_t num_cols = resWindowRect.width();
		if(num_cols < num_jobs)
			num_jobs = num_cols;
		uint32_t step_j = num_jobs ? (num_cols / num_jobs) : 0;
		if(num_threads == 1 || step_j < 4)
			num_jobs = 1;
		bool blockError = false;
		std::vector<std::future<int>> results;
		for(uint32_t j = 0; j < num_jobs; ++j)
		{
			auto job = new decompress_job<T, dwt_data<T>>(
				vert, resWindowRect.x0 + j * step_j,
				j < (num_jobs - 1U) ? resWindowRect.x0 + (j + 1U) * step_j : resWindowRect.x1);
			if(!job->data.alloc(data_size, pad))
			{
				GRK_ERROR("Out of memory");
				delete job;
				goto cleanup;
			}
			if(num_jobs > 1)
			{
				results.emplace_back(
					ThreadPool::get()->enqueue([job, executor_v] { return executor_v(job); }));
			}
			else
			{
				blockError = (executor_v(job) != 0);
			}
		}
		for(auto& result : results)
		{
			if(result.get() != 0)
				blockError = true;
		}
		if(blockError)
			goto cleanup;
	}
	// final read into tile buffer
	ret = sa->read(numres - 1,
					BAND_ORIENT_LL,
					synthesisWindow,
				   tilec->getBuffer()->getResWindowBufferHighestREL()->getBuffer(),
				   1,
				   tilec->getBuffer()->getResWindowBufferHighestREL()->stride,
				   true);
	assert(ret);
	GRK_UNUSED(ret);

#ifdef GRK_DEBUG_VALGRIND
	{
		GRK_INFO("Final synthesis window for component %d", compno);
		auto tileSynthesisWindow = synthesisWindow.pan(tilec->x0, tilec->y0);
		if(compno == debug_compno)
		{
			for(uint32_t j = 0; j < tileSynthesisWindow.height(); j++)
			{
				auto bufPtr = tilec->getBuffer()->getResWindowBufferHighestREL()->buf +
							  j * tilec->getBuffer()->getResWindowBufferHighestREL()->stride;
				for(uint32_t i = 0; i < tileSynthesisWindow.width(); i++)
				{
					auto val = grk_memcheck(bufPtr, 1);
					if(val != grk_mem_ok)
					{
						GRK_ERROR("***** Partial wavelet after final read: uninitialized memory at "
								  "(x,y) =  (%d,%d) ******",
								  tileSynthesisWindow.x0 + i, tileSynthesisWindow.y0 + j);
					}
					bufPtr += tilec->getBuffer()->getResWindowBufferHighestREL()->stride;
				}
			}
		}
	}
#endif
	rc = true;

cleanup:
	return rc;
}

bool WaveletReverse::decompress(TileProcessor* p_tcd, TileComponent* tilec, uint16_t compno,
								grkRectU32 window, uint8_t numres, uint8_t qmfbid)
{
	if(qmfbid == 1)
	{
		if(p_tcd->wholeTileDecompress)
			return decompress_tile_53(tilec, numres);
		else
		{
			constexpr uint32_t VERT_PASS_WIDTH = 4;
			return decompress_partial_tile<
				int32_t, getFilterPad<uint32_t>(true), VERT_PASS_WIDTH,
				Partial53<int32_t, getFilterPad<uint32_t>(false), VERT_PASS_WIDTH>>(
				tilec, compno, window, numres, tilec->getSparseCanvas());
		}
	}
	else
	{
		if(p_tcd->wholeTileDecompress)
			return decompress_tile_97(tilec, numres);
		else
		{
			constexpr uint32_t VERT_PASS_WIDTH = 1;
			return decompress_partial_tile<
				vec4f, getFilterPad<uint32_t>(false), VERT_PASS_WIDTH,
				Partial97<vec4f, getFilterPad<uint32_t>(false), VERT_PASS_WIDTH>>(
				tilec, compno, window, numres, tilec->getSparseCanvas());
		}
	}
}

} // namespace grk
#endif
