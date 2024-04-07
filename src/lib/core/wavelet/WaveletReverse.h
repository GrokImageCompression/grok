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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#pragma once

#include <cstdint>

namespace grk
{

typedef vec<float, 4> vec4f;

template<typename T, typename S>
struct TaskInfo
{
   TaskInfo(S data, grk_buf2d_simple<T> winLL, grk_buf2d_simple<T> winHL, grk_buf2d_simple<T> winLH,
			grk_buf2d_simple<T> winHH, grk_buf2d_simple<T> winDest, uint32_t indexMin,
			uint32_t indexMax)
	   : data(data), winLL(winLL), winHL(winHL), winLH(winLH), winHH(winHH), winDest(winDest),
		 indexMin_(indexMin), indexMax_(indexMax)
   {}
   TaskInfo(S data, uint32_t indexMin, uint32_t indexMax)
	   : data(data), indexMin_(indexMin), indexMax_(indexMax)
   {}
   ~TaskInfo(void)
   {
	  data.release();
   }
   S data;
   grk_buf2d_simple<T> winLL;
   grk_buf2d_simple<T> winHL;
   grk_buf2d_simple<T> winLH;
   grk_buf2d_simple<T> winHH;
   grk_buf2d_simple<T> winDest;

   uint32_t indexMin_;
   uint32_t indexMax_;
};

uint32_t max_resolution(Resolution* GRK_RESTRICT r, uint32_t i);

template<class T>
constexpr T getHorizontalPassHeight(bool lossless)
{
   return T(lossless ? 1 : vec4f::NUM_ELTS);
}

template<typename T>
struct dwt_data
{
   dwt_data(void)
	   : allocatedMem(nullptr), lenBytes_(0), paddingBytes_(0), mem(nullptr), memL(nullptr),
		 memH(nullptr), sn_full(0), dn_full(0), parity(0), resno(0)
   {}
   dwt_data(const dwt_data& rhs)
	   : allocatedMem(nullptr), lenBytes_(0), paddingBytes_(0), mem(nullptr), memL(nullptr),
		 memH(nullptr), sn_full(rhs.sn_full), dn_full(rhs.dn_full), parity(rhs.parity),
		 win_l(rhs.win_l), win_h(rhs.win_h), resno(rhs.resno)
   {}
   ~dwt_data(void)
   {
	  release();
   }
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
		 Logger::logger_.error("data size overflow");
		 return false;
	  }
	  paddingBytes_ = grk_make_aligned_width((uint32_t)padding * 2 + 32) * sizeof(T);
	  lenBytes_ = len * sizeof(T) + 2 * paddingBytes_;
	  allocatedMem = (T*)grk_aligned_malloc(lenBytes_);
	  if(!allocatedMem)
	  {
		 Logger::logger_.error("Failed to allocate %u bytes", lenBytes_);
		 return false;
	  }
	  mem = allocatedMem + paddingBytes_ / sizeof(T);

	  return (allocatedMem != nullptr) ? true : false;
   }
   void release(void)
   {
	  grk_aligned_free(allocatedMem);
	  allocatedMem = nullptr;
	  mem = nullptr;
	  memL = nullptr;
	  memH = nullptr;
   }
   T* allocatedMem;
   size_t lenBytes_;
   size_t paddingBytes_;
   T* mem;
   T* memL;
   T* memH;
   uint32_t sn_full; /* number of elements in low pass band */
   uint32_t dn_full; /* number of elements in high pass band */
   uint32_t parity; /* 0 = start on even coord, 1 = start on odd coord */
   grk_line32 win_l;
   grk_line32 win_h;
   uint8_t resno;
};

struct Params97
{
   Params97(void) : dataPrev(nullptr), data(nullptr), len(0), lenMax(0) {}
   vec4f* dataPrev;
   vec4f* data;
   uint32_t len;
   uint32_t lenMax;
};

class WaveletReverse
{
 public:
   WaveletReverse(TileProcessor* tileProcessor, TileComponent* tilec, uint16_t compno,
				  grk_rect32 window, uint8_t numres, uint8_t qmfbid);
   ~WaveletReverse(void);
   bool decompress(void);

   static void decompress_step_97(dwt_data<vec4f>* GRK_RESTRICT dwt);

 private:
   template<typename T, uint32_t FILTER_WIDTH, uint32_t VERT_PASS_WIDTH, typename D>
   bool decompress_partial_tile(ISparseCanvas* sa, std::vector<TaskInfo<T, dwt_data<T>>*>& tasks);
   static void decompress_step1_97(const Params97& d, const float c);
#ifdef __SSE__
   static void decompress_step1_sse_97(Params97 d, const __m128 c);
#endif
   static Params97 makeParams97(dwt_data<vec4f>* dwt, bool isBandL, bool step1);
   void interleave_h_97(dwt_data<vec4f>* GRK_RESTRICT dwt, grk_buf2d_simple<float> winL,
						grk_buf2d_simple<float> winH, uint32_t remaining_height);
   void decompress_h_strip_97(dwt_data<vec4f>* GRK_RESTRICT horiz, const uint32_t resHeight,
							  grk_buf2d_simple<float> winL, grk_buf2d_simple<float> winH,
							  grk_buf2d_simple<float> winDest);
   bool decompress_h_97(uint8_t res, uint32_t numThreads, size_t dataLength,
						dwt_data<vec4f>& GRK_RESTRICT horiz, const uint32_t resHeight,
						grk_buf2d_simple<float> winL, grk_buf2d_simple<float> winH,
						grk_buf2d_simple<float> winDest);
   void interleave_v_97(dwt_data<vec4f>* GRK_RESTRICT dwt, grk_buf2d_simple<float> winL,
						grk_buf2d_simple<float> winH, uint32_t nb_elts_read);
   void decompress_v_strip_97(dwt_data<vec4f>* GRK_RESTRICT vert, const uint32_t resWidth,
							  const uint32_t resHeight, grk_buf2d_simple<float> winL,
							  grk_buf2d_simple<float> winH, grk_buf2d_simple<float> winDest);
   bool decompress_v_97(uint8_t res, uint32_t numThreads, size_t dataLength,
						dwt_data<vec4f>& GRK_RESTRICT vert, const uint32_t resWidth,
						const uint32_t resHeight, grk_buf2d_simple<float> winL,
						grk_buf2d_simple<float> winH, grk_buf2d_simple<float> winDest);
   bool decompress_tile_97(void);
   void decompress_h_parity_even_53(int32_t* buf, int32_t* bandL, /* even */
									const uint32_t wL, int32_t* bandH, const uint32_t wH,
									int32_t* dest);
   void decompress_h_parity_odd_53(int32_t* buf, int32_t* bandL, /* odd */
								   const uint32_t wL, int32_t* bandH, const uint32_t wH,
								   int32_t* dest);
   void decompress_v_parity_even_53(int32_t* buf, int32_t* bandL, const uint32_t hL,
									const uint32_t strideL, int32_t* bandH, const uint32_t hH,
									const uint32_t strideH, int32_t* dest,
									const uint32_t strideDest);
   void decompress_v_parity_odd_53(int32_t* buf, int32_t* bandL, const uint32_t hL,
								   const uint32_t strideL, int32_t* bandH, const uint32_t hH,
								   const uint32_t strideH, int32_t* dest,
								   const uint32_t strideDest);
   void decompress_h_53(const dwt_data<int32_t>* dwt, int32_t* bandL, int32_t* bandH,
						int32_t* dest);
   void decompress_v_53(const dwt_data<int32_t>* dwt, grk_buf2d_simple<int32_t> winL,
						grk_buf2d_simple<int32_t> winH, grk_buf2d_simple<int32_t> winDest,
						uint32_t nb_cols);
   void decompress_h_strip_53(const dwt_data<int32_t>* horiz, uint32_t hMin, uint32_t hMax,
							  grk_buf2d_simple<int32_t> winL, grk_buf2d_simple<int32_t> winH,
							  grk_buf2d_simple<int32_t> winDest);
   bool decompress_h_53(uint8_t res, TileComponentWindow<int32_t>* buf, uint32_t resHeight,
						size_t dataLength);
   void decompress_v_strip_53(const dwt_data<int32_t>* vert, uint32_t wMin, uint32_t wMax,
							  grk_buf2d_simple<int32_t> winL, grk_buf2d_simple<int32_t> winH,
							  grk_buf2d_simple<int32_t> winDest);
   bool decompress_v_53(uint8_t res, TileComponentWindow<int32_t>* buf, uint32_t resWidth,
						size_t dataLength);
   bool decompress_tile_53(void);

   TileProcessor* tileProcessor_;
   Scheduler* scheduler_;
   TileComponent* tilec_;
   uint16_t compno_;
   grk_rect32 unreducedWindow_;
   uint8_t numres_;
   uint8_t qmfbid_;

   dwt_data<int32_t> horiz_;
   dwt_data<int32_t> vert_;

   dwt_data<vec4f> horizF_;
   dwt_data<vec4f> vertF_;

   std::vector<TaskInfo<vec4f, dwt_data<vec4f>>*> tasksF_;
   std::vector<TaskInfo<int32_t, dwt_data<int32_t>>*> tasks_;
};

} // namespace grk
