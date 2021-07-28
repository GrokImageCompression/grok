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

#undef HWY_TARGET_INCLUDE
//#define HWY_DISABLED_TARGETS (HWY_AVX2)
#define HWY_TARGET_INCLUDE "point_transform/mct.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>
HWY_BEFORE_NAMESPACE();
namespace grk
{
namespace HWY_NAMESPACE
{
	using namespace hwy::HWY_NAMESPACE;

	/**
	 * Apply dc shift for irreversible decompressed image.
	 * (assumes mono with no  MCT)
	 * input is floating point, output is 32 bit integer
	 */
	class DecompressDcShiftIrrev
	{
	  public:
		/**
		 * vector version
		 */
		int32_t vtrans(std::vector<int32_t*> channels, std::vector<ShiftInfo> shiftInfo,
					   size_t index, size_t chunkSize)
		{
			float* GRK_RESTRICT chan0 = (float*)channels[0];
			size_t begin = (size_t)index * chunkSize;
			const HWY_FULL(int32_t) di;
			const HWY_FULL(float) df;
			auto vshift = Set(di, shiftInfo[0]._shift);
			auto vmin = Set(di, shiftInfo[0]._min);
			auto vmax = Set(di, shiftInfo[0]._max);
			for(auto j = begin; j < begin + chunkSize; j += Lanes(di))
			{
				auto ni = Clamp(NearestInt(Load(df, chan0 + j)) + vshift, vmin, vmax);
				Store(ni, di, (int32_t*)(chan0 + j));
			}
			return 0;
		}
		/**
		 * scalar version
		 */
		void trans(std::vector<int32_t*> channels, std::vector<ShiftInfo> shiftInfo, size_t i,
				   size_t n)
		{
			float* GRK_RESTRICT chan0 = (float*)channels[0];
			int32_t* GRK_RESTRICT c0 = channels[0];
			int32_t shift = shiftInfo[0]._shift;
			int32_t _min = shiftInfo[0]._min;
			int32_t _max = shiftInfo[0]._max;
			for(; i < n; ++i)
				c0[i] = std::clamp<int32_t>((int32_t)grk_lrintf(chan0[i]) + shift, _min, _max);
		}
	};

	/**
	 * Apply dc shift for reversible decompressed image
	 * (assumes mono with no MCT)
	 * input and output buffers are both 32 bit integer
	 */
	class DecompressDcShiftRev
	{
	  public:
		/**
		 * vector version
		 */
		int32_t vtrans(std::vector<int32_t*> channels, std::vector<ShiftInfo> shiftInfo,
					   size_t index, size_t chunkSize)
		{
			int32_t* GRK_RESTRICT chan0 = channels[0];
			size_t begin = (size_t)index * chunkSize;
			const HWY_FULL(int32_t) di;
			auto vshift = Set(di, shiftInfo[0]._shift);
			auto vmin = Set(di, shiftInfo[0]._min);
			auto vmax = Set(di, shiftInfo[0]._max);
			for(auto j = begin; j < begin + chunkSize; j += Lanes(di))
			{
				auto ni = Clamp(Load(di, chan0 + j) + vshift, vmin, vmax);
				Store(ni, di, chan0 + j);
			}
			return 0;
		}
		/**
		 * scalar version
		 */
		void trans(std::vector<int32_t*> channels, std::vector<ShiftInfo> shiftInfo, size_t index,
				   size_t numSamples)
		{
			int32_t* GRK_RESTRICT chan0 = channels[0];
			int32_t shift = shiftInfo[0]._shift;
			int32_t _min = shiftInfo[0]._min;
			int32_t _max = shiftInfo[0]._max;
			for(; index < numSamples; ++index)
				chan0[index] = std::clamp<int32_t>(chan0[index] + shift, _min, _max);
		}
	};

	/**
	 * Apply MCT with optional DC shift to reversible decompressed image
	 */
	class DecompressRev
	{
	  public:
		int32_t vtrans(std::vector<int32_t*> channels, std::vector<ShiftInfo> shiftInfo,
					   size_t index, size_t chunkSize)
		{
			int32_t* GRK_RESTRICT chan0 = channels[0];
			int32_t* GRK_RESTRICT chan1 = channels[1];
			int32_t* GRK_RESTRICT chan2 = channels[2];
			int32_t shift[3] = {shiftInfo[0]._shift, shiftInfo[1]._shift, shiftInfo[2]._shift};
			int32_t _min[3] = {shiftInfo[0]._min, shiftInfo[1]._min, shiftInfo[2]._min};
			int32_t _max[3] = {shiftInfo[0]._max, shiftInfo[1]._max, shiftInfo[2]._max};

			const HWY_FULL(int32_t) di;
			auto vdcr = Set(di, shift[0]);
			auto vdcg = Set(di, shift[1]);
			auto vdcb = Set(di, shift[2]);
			auto minr = Set(di, _min[0]);
			auto ming = Set(di, _min[1]);
			auto minb = Set(di, _min[2]);
			auto maxr = Set(di, _max[0]);
			auto maxg = Set(di, _max[1]);
			auto maxb = Set(di, _max[2]);

			size_t begin = (size_t)index * chunkSize;
			for(auto j = begin; j < begin + chunkSize; j += Lanes(di))
			{
				auto y = Load(di, chan0 + j);
				auto u = Load(di, chan1 + j);
				auto v = Load(di, chan2 + j);
				auto g = y - ShiftRight<2>(u + v);
				auto r = v + g;
				auto b = u + g;
				Store(Clamp(r + vdcr, minr, maxr), di, chan0 + j);
				Store(Clamp(g + vdcg, ming, maxg), di, chan1 + j);
				Store(Clamp(b + vdcb, minb, maxb), di, chan2 + j);
			}
			return 0;
		}
		void trans(std::vector<int32_t*> channels, std::vector<ShiftInfo> shiftInfo, size_t index,
				   size_t numSamples)
		{
			int32_t* GRK_RESTRICT chan0 = channels[0];
			int32_t* GRK_RESTRICT chan1 = channels[1];
			int32_t* GRK_RESTRICT chan2 = channels[2];

			int32_t shift[3] = {shiftInfo[0]._shift, shiftInfo[1]._shift, shiftInfo[2]._shift};
			int32_t _min[3] = {shiftInfo[0]._min, shiftInfo[1]._min, shiftInfo[2]._min};
			int32_t _max[3] = {shiftInfo[0]._max, shiftInfo[1]._max, shiftInfo[2]._max};
			for(; index < numSamples; ++index)
			{
				int32_t y = chan0[index];
				int32_t u = chan1[index];
				int32_t v = chan2[index];
				int32_t g = y - ((u + v) >> 2);
				int32_t r = v + g;
				int32_t b = u + g;
				chan0[index] = std::clamp<int32_t>(r + shift[0], _min[0], _max[0]);
				chan1[index] = std::clamp<int32_t>(g + shift[1], _min[1], _max[1]);
				chan2[index] = std::clamp<int32_t>(b + shift[2], _min[2], _max[2]);
			}
		}
	};

	/**
	 * Apply MCT with optional DC shift to irreversible decompressed image
	 */
	class DecompressIrrev
	{
	  public:
		int32_t vtrans(std::vector<int32_t*> channels, std::vector<ShiftInfo> shiftInfo,
					   size_t index, size_t chunkSize)
		{
			float* GRK_RESTRICT chan0 = (float*)channels[0];
			float* GRK_RESTRICT chan1 = (float*)channels[1];
			float* GRK_RESTRICT chan2 = (float*)channels[2];

			int32_t* GRK_RESTRICT c0 = channels[0];
			int32_t* GRK_RESTRICT c1 = channels[1];
			int32_t* GRK_RESTRICT c2 = channels[2];

			const HWY_FULL(float) df;
			const HWY_FULL(int32_t) di;

			int32_t shift[3] = {shiftInfo[0]._shift, shiftInfo[1]._shift, shiftInfo[2]._shift};
			int32_t _min[3] = {shiftInfo[0]._min, shiftInfo[1]._min, shiftInfo[2]._min};
			int32_t _max[3] = {shiftInfo[0]._max, shiftInfo[1]._max, shiftInfo[2]._max};
			auto vdcr = Set(di, shift[0]);
			auto vdcg = Set(di, shift[1]);
			auto vdcb = Set(di, shift[2]);
			auto minr = Set(di, _min[0]);
			auto ming = Set(di, _min[1]);
			auto minb = Set(di, _min[2]);
			auto maxr = Set(di, _max[0]);
			auto maxg = Set(di, _max[1]);
			auto maxb = Set(di, _max[2]);

			auto vrv = Set(df, 1.402f);
			auto vgu = Set(df, 0.34413f);
			auto vgv = Set(df, 0.71414f);
			auto vbu = Set(df, 1.772f);

			size_t begin = (size_t)index * chunkSize;
			for(auto j = begin; j < begin + chunkSize; j += Lanes(di))
			{
				auto vy = Load(df, chan0 + j);
				auto vu = Load(df, chan1 + j);
				auto vv = Load(df, chan2 + j);
				auto vr = vy + vv * vrv;
				auto vg = vy - vu * vgu - vv * vgv;
				auto vb = vy + vu * vbu;

				Store(Clamp(NearestInt(vr) + vdcr, minr, maxr), di, c0 + j);
				Store(Clamp(NearestInt(vg) + vdcg, ming, maxg), di, c1 + j);
				Store(Clamp(NearestInt(vb) + vdcb, minb, maxb), di, c2 + j);
			}
			return 0;
		}
		void trans(std::vector<int32_t*> channels, std::vector<ShiftInfo> shiftInfo, size_t index,
				   size_t numSamples)
		{
			float* GRK_RESTRICT chan0 = (float*)channels[0];
			float* GRK_RESTRICT chan1 = (float*)channels[1];
			float* GRK_RESTRICT chan2 = (float*)channels[2];

			int32_t* GRK_RESTRICT c0 = channels[0];
			int32_t* GRK_RESTRICT c1 = channels[1];
			int32_t* GRK_RESTRICT c2 = channels[2];

			int32_t shift[3] = {shiftInfo[0]._shift, shiftInfo[1]._shift, shiftInfo[2]._shift};
			int32_t _min[3] = {shiftInfo[0]._min, shiftInfo[1]._min, shiftInfo[2]._min};
			int32_t _max[3] = {shiftInfo[0]._max, shiftInfo[1]._max, shiftInfo[2]._max};

			for(; index < numSamples; ++index)
			{
				float y = chan0[index];
				float u = chan1[index];
				float v = chan2[index];
				float r = y + (v * 1.402f);
				float g = y - (u * 0.34413f) - (v * (0.71414f));
				float b = y + (u * 1.772f);

				c0[index] = std::clamp<int32_t>((int32_t)grk_lrintf(r) + shift[0], _min[0], _max[0]);
				c1[index] = std::clamp<int32_t>((int32_t)grk_lrintf(g) + shift[1], _min[1], _max[1]);
				c2[index] = std::clamp<int32_t>((int32_t)grk_lrintf(b) + shift[2], _min[2], _max[2]);
			}
		}
	};




	/**
	 * Apply MCT with optional DC shift to reversible compressed image
	 */
	class CompressRev
	{
	  public:
		int32_t vtrans(std::vector<int32_t*> channels, std::vector<ShiftInfo> shiftInfo,
					   size_t index, size_t chunkSize)
		{
			int32_t* GRK_RESTRICT chan0 = channels[0];
			int32_t* GRK_RESTRICT chan1 = channels[1];
			int32_t* GRK_RESTRICT chan2 = channels[2];

			const HWY_FULL(int32_t) di;
			int32_t shift[3] = {shiftInfo[0]._shift, shiftInfo[1]._shift, shiftInfo[2]._shift};

			auto vdcr = Set(di, shift[0]);
			auto vdcg = Set(di, shift[1]);
			auto vdcb = Set(di, shift[2]);

			size_t begin = (size_t)index * chunkSize;
			for(auto j = begin; j < begin + chunkSize; j += Lanes(di))
			{
				auto r = Load(di, chan0 + j) + vdcr;
				auto g = Load(di, chan1 + j) + vdcg;
				auto b = Load(di, chan2 + j) + vdcb;
				auto y = ShiftRight<2>((g + g) + b + r);
				auto u = b - g;
				auto v = r - g;
				Store(y, di, chan0 + j);
				Store(u, di, chan1 + j);
				Store(v, di, chan2 + j);
			}
			return 0;
		}
		void trans(std::vector<int32_t*> channels, std::vector<ShiftInfo> shiftInfo, size_t index,
				   size_t numSamples)
		{
			int32_t* GRK_RESTRICT chan0 = channels[0];
			int32_t* GRK_RESTRICT chan1 = channels[1];
			int32_t* GRK_RESTRICT chan2 = channels[2];
			int32_t shift[3] = {shiftInfo[0]._shift, shiftInfo[1]._shift, shiftInfo[2]._shift};

			for(; index < numSamples; ++index)
			{
				int32_t r = chan0[index] + shift[0];
				int32_t g = chan1[index] + shift[1];
				int32_t b = chan2[index] + shift[2];
				int32_t y = (r + (g * 2) + b) >> 2;
				int32_t u = b - g;
				int32_t v = r - g;
				chan0[index] = y;
				chan1[index] = u;
				chan2[index] = v;
			}
		}
	};

	/**
	 * Apply MCT with optional DC shift to irreversible compressed image
	 */
	class CompressIrrev
	{
	  public:
		int32_t vtrans(std::vector<int32_t*> channels, std::vector<ShiftInfo> shiftInfo,
					   size_t index, size_t chunkSize)
		{
			int32_t* GRK_RESTRICT chan0 = channels[0];
			int32_t* GRK_RESTRICT chan1 = channels[1];
			int32_t* GRK_RESTRICT chan2 = channels[2];

			int32_t shift[3] = {shiftInfo[0]._shift, shiftInfo[1]._shift, shiftInfo[2]._shift};

			const HWY_FULL(float) df;
			const HWY_FULL(int32_t) di;

			auto va_r = Set(df, a_r);
			auto va_g = Set(df, a_g);
			auto va_b = Set(df, a_b);
			auto vcb = Set(df, cb);
			auto vcr = Set(df, cr);

			auto vdcr = Set(di, shift[0]);
			auto vdcg = Set(di, shift[1]);
			auto vdcb = Set(di, shift[2]);

			size_t begin = (size_t)index * chunkSize;
			for(auto j = begin; j < begin + chunkSize; j += Lanes(di))
			{
				auto r = ConvertTo(df, Load(di, chan0 + j) + vdcr);
				auto g = ConvertTo(df, Load(di, chan1 + j) + vdcg);
				auto b = ConvertTo(df, Load(di, chan2 + j) + vdcb);

				auto y = va_r * r + va_g * g + va_b * b;
				auto u = vcb * (b - y);
				auto v = vcr * (r - y);

				Store(y, df, (float*)(chan0 + j));
				Store(u, df, (float*)(chan1 + j));
				Store(v, df, (float*)(chan2 + j));
			}

			return 0;
		}
		void trans(std::vector<int32_t*> channels, std::vector<ShiftInfo> shiftInfo, size_t index,
				   size_t numSamples)
		{
			int32_t* GRK_RESTRICT chan0 = channels[0];
			int32_t* GRK_RESTRICT chan1 = channels[1];
			int32_t* GRK_RESTRICT chan2 = channels[2];

			int32_t shift[3] = {shiftInfo[0]._shift, shiftInfo[1]._shift, shiftInfo[2]._shift};

			float* GRK_RESTRICT chan0f = (float*)chan0;
			float* GRK_RESTRICT chan1f = (float*)chan1;
			float* GRK_RESTRICT chan2f = (float*)chan2;

			for(; index < numSamples; ++index)
			{
				float r = (float)(chan0[index] + shift[0]);
				float g = (float)(chan1[index] + shift[1]);
				float b = (float)(chan2[index] + shift[2]);

				float y = a_r * r + a_g * g + a_b * b;
				float u = cb * (b - y);
				float v = cr * (r - y);

				chan0f[index] = y;
				chan1f[index] = u;
				chan2f[index] = v;
			}
		}

	  private:
		const float a_r = 0.299f;
		const float a_g = 0.587f;
		const float a_b = 0.114f;
		const float cb = 0.5f / (1.0f - a_b);
		const float cr = 0.5f / (1.0f - a_r);
	};

	template<class T>
	size_t vscheduler(std::vector<int32_t*> channels, std::vector<ShiftInfo> shiftInfo, size_t numSamples)
	{
		size_t i = 0;
		size_t num_threads = ThreadPool::get()->num_threads();
		size_t chunkSize = numSamples / num_threads;
		const HWY_FULL(int32_t) di;
		auto numLanes = Lanes(di);
		chunkSize = (chunkSize / numLanes) * numLanes;
		if(chunkSize > numLanes)
		{
			std::vector<std::future<int>> results;
			for(size_t tr = 0; tr < num_threads; ++tr)
			{
				size_t index = tr;
				auto compressor = [index, chunkSize, channels, shiftInfo]() {
					T transform;
					transform.vtrans(channels, shiftInfo, index, chunkSize);
					return 0;
				};
				if(num_threads > 1)
					results.emplace_back(ThreadPool::get()->enqueue(compressor));
				else
					compressor();
			}
			for(auto& result : results)
			{
				result.get();
			}
			i = chunkSize * num_threads;
		}
		T transform;
		transform.trans(channels, shiftInfo, i, numSamples);

		return i;
	}

	size_t hwy_compress_rev(std::vector<int32_t*> channels, std::vector<ShiftInfo> shiftInfo,
			  size_t n)
	{
		return vscheduler<CompressRev>(channels, shiftInfo, n);
	}

	size_t hwy_compress_irrev(std::vector<int32_t*> channels, std::vector<ShiftInfo> shiftInfo,
			  size_t n)
	{
		return vscheduler<CompressIrrev>(channels, shiftInfo, n);
	}

	size_t hwy_decompress_rev(std::vector<int32_t*> channels, std::vector<ShiftInfo> shiftInfo,
							  size_t n)
	{
		return vscheduler<DecompressRev>(channels, shiftInfo, n);
	}

	size_t hwy_decompress_irrev(std::vector<int32_t*> channels, std::vector<ShiftInfo> shiftInfo,
								size_t n)
	{
		return vscheduler<DecompressIrrev>(channels, shiftInfo, n);
	}

	size_t hwy_decompress_dc_shift_irrev(std::vector<int32_t*> channels,
										 std::vector<ShiftInfo> shiftInfo, size_t n)
	{
		return vscheduler<DecompressDcShiftIrrev>(channels, shiftInfo, n);
	}

	size_t hwy_decompress_dc_shift_rev(std::vector<int32_t*> channels,
									   std::vector<ShiftInfo> shiftInfo, size_t n)
	{
		return vscheduler<DecompressDcShiftRev>(channels, shiftInfo, n);
	}
} // namespace HWY_NAMESPACE
} // namespace grk
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace grk
{
HWY_EXPORT(hwy_compress_rev);
HWY_EXPORT(hwy_compress_irrev);
HWY_EXPORT(hwy_decompress_rev);
HWY_EXPORT(hwy_decompress_irrev);
HWY_EXPORT(hwy_decompress_dc_shift_irrev);
HWY_EXPORT(hwy_decompress_dc_shift_rev);

void mct::decompress_dc_shift_irrev(Tile* tile, GrkImage* image,
									TileComponentCodingParams* tccps,
									uint32_t compno)
{
	int32_t* GRK_RESTRICT c0 =
		  tile->comps[compno].getBuffer()->getHighestBufferResWindowREL()->getBuffer();
	size_t n = (tile->comps + compno)->getBuffer()->stridedArea();
	std::vector<ShiftInfo> shiftInfo;

	genShift(compno, image,tccps,1,shiftInfo);
	HWY_DYNAMIC_DISPATCH(hwy_decompress_dc_shift_irrev)({c0}, shiftInfo, n);
}

/**
 * inverse irreversible MCT
 * (vector routines are disabled)
 */
void mct::decompress_irrev(Tile* tile, GrkImage* image, TileComponentCodingParams* tccps)
{
	uint64_t n = tile->comps->getBuffer()->stridedArea();
	int32_t* c0_i = tile->comps[0].getBuffer()->getHighestBufferResWindowREL()->getBuffer();
	int32_t* c1_i = tile->comps[1].getBuffer()->getHighestBufferResWindowREL()->getBuffer();
	int32_t* c2_i = tile->comps[2].getBuffer()->getHighestBufferResWindowREL()->getBuffer();
	std::vector<ShiftInfo> shiftInfo;

	hwy::DisableTargets(uint32_t(~HWY_SCALAR));

	genShift(image,tccps,1,shiftInfo);
	HWY_DYNAMIC_DISPATCH(hwy_decompress_irrev)
	({c0_i, c1_i, c2_i},
	 shiftInfo,
	 n);
}

void mct::decompress_dc_shift_rev(Tile* tile, GrkImage* image, TileComponentCodingParams* tccps,
								  uint32_t compno)
{
	int32_t* GRK_RESTRICT c0 =
		tile->comps[compno].getBuffer()->getHighestBufferResWindowREL()->getBuffer();
	std::vector<ShiftInfo> shiftInfo;

	size_t n = (tile->comps + compno)->getBuffer()->stridedArea();
	genShift(compno, image,tccps,1,shiftInfo);
	HWY_DYNAMIC_DISPATCH(hwy_decompress_dc_shift_rev)({c0}, shiftInfo, n);
}

/* <summary> */
/* Inverse reversible MCT. */
/* </summary> */
void mct::decompress_rev(Tile* tile, GrkImage* image, TileComponentCodingParams* tccps)
{
	int32_t* GRK_RESTRICT c0 =
		tile->comps[0].getBuffer()->getHighestBufferResWindowREL()->getBuffer();
	int32_t* GRK_RESTRICT c1 =
		tile->comps[1].getBuffer()->getHighestBufferResWindowREL()->getBuffer();
	int32_t* GRK_RESTRICT c2 =
		tile->comps[2].getBuffer()->getHighestBufferResWindowREL()->getBuffer();
	uint64_t n = tile->comps->getBuffer()->stridedArea();
	std::vector<ShiftInfo> shiftInfo;

	genShift(image,tccps,1,shiftInfo);
	HWY_DYNAMIC_DISPATCH(hwy_decompress_rev)
	({c0, c1, c2},
	 shiftInfo,
	 n);
}
/* <summary> */
/* Forward reversible MCT. */
/* </summary> */
void mct::compress_rev(Tile* tile, GrkImage* image, TileComponentCodingParams* tccps)
{
	int32_t* GRK_RESTRICT c0 =
		tile->comps[0].getBuffer()->getHighestBufferResWindowREL()->getBuffer();
	int32_t* GRK_RESTRICT c1 =
		tile->comps[1].getBuffer()->getHighestBufferResWindowREL()->getBuffer();
	int32_t* GRK_RESTRICT c2 =
		tile->comps[2].getBuffer()->getHighestBufferResWindowREL()->getBuffer();

	uint64_t n = tile->comps->getBuffer()->stridedArea();
	std::vector<ShiftInfo> shiftInfo;

	genShift(image,tccps,-1,shiftInfo);
	HWY_DYNAMIC_DISPATCH(hwy_compress_rev)
	({c0, c1, c2},
	 shiftInfo,
	 n);
}
/* <summary> */
/* Forward irreversible MCT. */
/* </summary> */
void mct::compress_irrev(Tile* tile, GrkImage* image, TileComponentCodingParams* tccps)
{
	int32_t* GRK_RESTRICT c0 =
		tile->comps[0].getBuffer()->getHighestBufferResWindowREL()->getBuffer();
	int32_t* GRK_RESTRICT c1 =
		tile->comps[1].getBuffer()->getHighestBufferResWindowREL()->getBuffer();
	int32_t* GRK_RESTRICT c2 =
		tile->comps[2].getBuffer()->getHighestBufferResWindowREL()->getBuffer();

	uint64_t n = tile->comps->getBuffer()->stridedArea();
	std::vector<ShiftInfo> shiftInfo;

	genShift(image,tccps,-1,shiftInfo);

	HWY_DYNAMIC_DISPATCH(hwy_compress_irrev)
	({c0, c1, c2},
	 shiftInfo,
	 n);
}

void mct::genShift(uint16_t compno,
					GrkImage* image,
					TileComponentCodingParams* tccps,
					int32_t sign,
					std::vector<ShiftInfo> &shiftInfo){

	int32_t _min,_max,shift;
	auto img_comp = image->comps + compno;
	if(img_comp->sgnd)
	{
		_min = -(1 << (img_comp->prec - 1));
		_max = (1 << (img_comp->prec - 1)) - 1;
	}
	else
	{
		_min = 0;
		_max = (1 << img_comp->prec) - 1;
	}
	auto tccp = tccps + compno;
	shift = sign * tccp->m_dc_level_shift;
	shiftInfo.push_back({_min,_max,shift});
}
void mct::genShift(GrkImage* image,
					TileComponentCodingParams* tccps,
					int32_t sign,
					std::vector<ShiftInfo> &shiftInfo){
	for (uint16_t i = 0; i < 3; ++i)
		genShift(i,image,tccps,sign,shiftInfo);
}



void mct::calculate_norms(double* pNorms, uint32_t pNbComps, float* pMatrix)
{
	float CurrentValue;
	double* Norms = (double*)pNorms;
	float* Matrix = (float*)pMatrix;

	uint32_t i;
	for(i = 0; i < pNbComps; ++i)
	{
		Norms[i] = 0;
		uint32_t Index = i;
		uint32_t j;
		for(j = 0; j < pNbComps; ++j)
		{
			CurrentValue = Matrix[Index];
			Index += pNbComps;
			Norms[i] += CurrentValue * CurrentValue;
		}
		Norms[i] = sqrt(Norms[i]);
	}
}

bool mct::compress_custom(uint8_t* mct_matrix, uint64_t n, uint8_t** pData, uint32_t pNbComp,
						  uint32_t isSigned)
{
	auto Mct = (float*)mct_matrix;
	uint32_t NbMatCoeff = pNbComp * pNbComp;
	auto data = (int32_t**)pData;
	uint32_t Multiplicator = 1 << 13;
	GRK_UNUSED(isSigned);

	auto CurrentData = (int32_t*)grkMalloc((pNbComp + NbMatCoeff) * sizeof(int32_t));
	if(!CurrentData)
		return false;

	auto CurrentMatrix = CurrentData + pNbComp;

	for(uint64_t i = 0; i < NbMatCoeff; ++i)
		CurrentMatrix[i] = (int32_t)(*(Mct++) * (float)Multiplicator);

	for(uint64_t i = 0; i < n; ++i)
	{
		for(uint32_t j = 0; j < pNbComp; ++j)
			CurrentData[j] = (*(data[j]));
		for(uint32_t j = 0; j < pNbComp; ++j)
		{
			auto MctPtr = CurrentMatrix;
			*(data[j]) = 0;
			for(uint32_t k = 0; k < pNbComp; ++k)
			{
				*(data[j]) += fix_mul(*MctPtr, CurrentData[k]);
				++MctPtr;
			}
			++data[j];
		}
	}
	grkFree(CurrentData);

	return true;
}

bool mct::decompress_custom(uint8_t* mct_matrix, uint64_t n, uint8_t** pData, uint32_t num_comps,
							uint32_t is_signed)
{
	auto data = (float**)pData;

	GRK_UNUSED(is_signed);

	auto pixel = new float[2 * num_comps];
	auto current_pixel = pixel + num_comps;

	for(uint64_t i = 0; i < n; ++i)
	{
		auto Mct = (float*)mct_matrix;
		for(uint32_t j = 0; j < num_comps; ++j)
		{
			pixel[j] = (float)(*(data[j]));
		}
		for(uint32_t j = 0; j < num_comps; ++j)
		{
			current_pixel[j] = 0;
			for(uint32_t k = 0; k < num_comps; ++k)
				current_pixel[j] += *(Mct++) * pixel[k];
			*(data[j]++) = (float)(current_pixel[j]);
		}
	}
	delete[] pixel;
	return true;
}

/* <summary> */
/* This table contains the norms of the basis function of the reversible MCT. */
/* </summary> */
static const double mct_norms_rev[3] = {1.732, .8292, .8292};

/* <summary> */
/* This table contains the norms of the basis function of the irreversible MCT. */
/* </summary> */
static const double mct_norms_irrev[3] = {1.732, 1.805, 1.573};

const double* mct::get_norms_rev()
{
	return mct_norms_rev;
}
const double* mct::get_norms_irrev()
{
	return mct_norms_irrev;
}

} // namespace grk
#endif
