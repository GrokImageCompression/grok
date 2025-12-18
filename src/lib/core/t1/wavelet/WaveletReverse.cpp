/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

/***********************************************************************

Inverse Update (even)
F.3, page 118, ITU-T Rec. T.800 final draft
even -= (previous + next + 2) >> 2;

Inverse Predict (odd)
F.3, page 118, ITU-T Rec. T.800 final draft
odd += (previous + next) >> 1;

************************************************************************/

#include "grk_includes.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "wavelet/WaveletReverse.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace grk
{
namespace HWY_NAMESPACE
{
  static uint32_t GetHWY_PLL_COLS_53()
  {
    static const uint32_t value = []() {
      const HWY_FULL(int32_t) di;
      return 2 * (uint32_t)Lanes(di);
    }();
    return value;
  }
  static uint32_t HWY_PLL_COLS_53 = GetHWY_PLL_COLS_53();

  static void hwy_v_final_store_53(const int32_t* scratch, const uint32_t height, int32_t* dest,
                                   const size_t strideDest)
  {
    const HWY_FULL(int32_t) di;
    for(uint32_t i = 0; i < height; ++i)
    {
      StoreU(Load(di, scratch + HWY_PLL_COLS_53 * i + 0), di, &dest[(size_t)i * strideDest + 0]);
      StoreU(Load(di, scratch + HWY_PLL_COLS_53 * i + Lanes(di)), di,
             dest + (size_t)i * strideDest + Lanes(di));
    }
  }
  /** Vertical inverse 5x3 wavelet transform for 8 columns in SSE2, or
   * 16 in AVX2, when top-most pixel is on even coordinate */
  static void hwy_v_p0_53(int32_t* scratch, const uint32_t height, int32_t* bandL,
                          const size_t strideL, int32_t* bandH, const size_t strideH, int32_t* dest,
                          const uint32_t strideDest)
  {
    const HWY_FULL(int32_t) di;
    const auto two = Set(di, 2);
    const auto one = Set(di, 1);

    assert(height > 1);

    /* Note: loads of input even/odd values must be done in an unaligned */
    /* fashion. But stores in tmp can be done with aligned store, since */
    /* the temporary buffer is properly aligned */
    assert((size_t)scratch % (sizeof(int32_t) * Lanes(di)) == 0);

    auto s1n_0 = LoadU(di, bandL + 0);
    auto s1n_1 = LoadU(di, bandL + Lanes(di));
    auto d1n_0 = LoadU(di, bandH);
    auto d1n_1 = LoadU(di, bandH + Lanes(di));

    /* s0n = s1n - ((d1n + 1) >> 1); <==> */
    /* s0n = s1n - ((d1n + d1n + 2) >> 2); */
    auto s0n_0 = s1n_0 - ShiftRight<1>(d1n_0 + one);
    auto s0n_1 = s1n_1 - ShiftRight<1>(d1n_1 + one);

    uint32_t i = 0;
    if(height > 3)
    {
      uint32_t j;
      for(i = 0, j = 1; i < (height - 3); i += 2, j++)
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

        Store(s0c_0, di, scratch + HWY_PLL_COLS_53 * (i + 0));
        Store(s0c_1, di, scratch + HWY_PLL_COLS_53 * (i + 0) + Lanes(di));

        /* d1c + ((s0c + s0n) >> 1) */
        Store(d1c_0 + ShiftRight<1>(s0c_0 + s0n_0), di, scratch + HWY_PLL_COLS_53 * (i + 1) + 0);
        Store(d1c_1 + ShiftRight<1>(s0c_1 + s0n_1), di,
              scratch + HWY_PLL_COLS_53 * (i + 1) + Lanes(di));
      }
    }
    Store(s0n_0, di, scratch + HWY_PLL_COLS_53 * (i + 0) + 0);
    Store(s0n_1, di, scratch + HWY_PLL_COLS_53 * (i + 0) + Lanes(di));

    if(height & 1)
    {
      s1n_0 = LoadU(di, bandL + (size_t)(height >> 1) * strideL);
      /* s0n_len_minus_1 = s1n - ((d1n + 1) >> 1); */
      auto s0n_len_minus_1 = s1n_0 - ShiftRight<2>(d1n_0 + d1n_0 + two);
      Store(s0n_len_minus_1, di, scratch + HWY_PLL_COLS_53 * (height - 1));
      /* d1n + ((s0n + s0n_len_minus_1) >> 1) */
      Store(d1n_0 + ShiftRight<1>(s0n_0 + s0n_len_minus_1), di,
            scratch + HWY_PLL_COLS_53 * (height - 2));

      s1n_1 = LoadU(di, bandL + (size_t)(height >> 1) * strideL + Lanes(di));
      /* s0n_len_minus_1 = s1n - ((d1n + 1) >> 1); */
      s0n_len_minus_1 = s1n_1 - ShiftRight<2>(d1n_1 + d1n_1 + two);
      Store(s0n_len_minus_1, di, scratch + HWY_PLL_COLS_53 * (height - 1) + Lanes(di));
      /* d1n + ((s0n + s0n_len_minus_1) >> 1) */
      Store(d1n_1 + ShiftRight<1>(s0n_1 + s0n_len_minus_1), di,
            scratch + HWY_PLL_COLS_53 * (height - 2) + Lanes(di));
    }
    else
    {
      Store(d1n_0 + s0n_0, di, scratch + HWY_PLL_COLS_53 * (height - 1) + 0);
      Store(d1n_1 + s0n_1, di, scratch + HWY_PLL_COLS_53 * (height - 1) + Lanes(di));
    }
    hwy_v_final_store_53(scratch, height, dest, strideDest);
  }

  /** Vertical inverse 5x3 wavelet transform for 8 columns in SSE2, or
   * 16 in AVX2, when top-most pixel is on odd coordinate */
  static void hwy_v_p1_53(int32_t* scratch, const uint32_t height, int32_t* bandL,
                          const uint32_t strideL, int32_t* bandH, const uint32_t strideH,
                          int32_t* dest, const uint32_t strideDest)
  {
    const HWY_FULL(int32_t) di;
    const auto two = Set(di, 2);

    assert(height > 2);
    /* Note: loads of input even/odd values must be done in a unaligned */
    /* fashion. But stores in buf can be done with aligned store, since */
    /* the temporary buffer is properly aligned */
    assert((size_t)scratch % (sizeof(int32_t) * Lanes(di)) == 0);

    auto d1_0 = LoadU(di, bandH + strideH);

    /* bandL[0] - ((bandH[0] + d1 + 2) >> 2); */
    auto sc_0 = LoadU(di, bandL + 0) - ShiftRight<2>(LoadU(di, bandH + 0) + d1_0 + two);
    Store(LoadU(di, bandH + 0) + sc_0, di, scratch + HWY_PLL_COLS_53 * 0);
    auto d1_1 = LoadU(di, bandH + strideH + Lanes(di));

    /* bandL[0] - ((H[0] + d1 + 2) >> 2); */
    auto sc_1 =
        LoadU(di, bandL + Lanes(di)) - ShiftRight<2>(LoadU(di, bandH + Lanes(di)) + d1_1 + two);
    Store(LoadU(di, bandH + Lanes(di)) + sc_1, di, scratch + HWY_PLL_COLS_53 * 0 + Lanes(di));

    uint32_t i = 1;
    size_t j = 1;
    for(; i < (height - 2 - !(height & 1)); i += 2, j++)
    {
      auto d2_0 = LoadU(di, bandH + (j + 1) * strideH);
      auto d2_1 = LoadU(di, bandH + (j + 1) * strideH + Lanes(di));

      /* sn = bandH[j * stride] - ((d1 + d2 + 2) >> 2); */
      auto sn_0 = LoadU(di, bandL + j * strideL) - ShiftRight<2>(d1_0 + d2_0 + two);
      auto sn_1 = LoadU(di, bandL + j * strideL + Lanes(di)) - ShiftRight<2>(d1_1 + d2_1 + two);

      Store(sc_0, di, scratch + HWY_PLL_COLS_53 * i);
      Store(sc_1, di, scratch + HWY_PLL_COLS_53 * i + Lanes(di));

      /* buf[i + 1] = d1 + ((sn + sc) >> 1); */
      Store(d1_0 + ShiftRight<1>(sn_0 + sc_0), di, scratch + HWY_PLL_COLS_53 * (i + 1) + 0);
      Store(d1_1 + ShiftRight<1>(sn_1 + sc_1), di, scratch + HWY_PLL_COLS_53 * (i + 1) + Lanes(di));

      sc_0 = sn_0;
      sc_1 = sn_1;
      d1_0 = d2_0;
      d1_1 = d2_1;
    }
    Store(sc_0, di, scratch + HWY_PLL_COLS_53 * i);
    Store(sc_1, di, scratch + HWY_PLL_COLS_53 * i + Lanes(di));

    if(!(height & 1))
    {
      /*dn = bandH[(len / 2 - 1) * stride] - ((s1 + 1) >> 1); */
      auto sn_0 =
          LoadU(di, bandL + (size_t)(height / 2 - 1) * strideL) - ShiftRight<2>(d1_0 + d1_0 + two);
      auto sn_1 = LoadU(di, bandL + (size_t)(height / 2 - 1) * strideL + Lanes(di)) -
                  ShiftRight<2>(d1_1 + d1_1 + two);

      /* buf[len - 2] = s1 + ((dn + dc) >> 1); */
      Store(d1_0 + ShiftRight<1>(sn_0 + sc_0), di, scratch + HWY_PLL_COLS_53 * (height - 2) + 0);
      Store(d1_1 + ShiftRight<1>(sn_1 + sc_1), di,
            scratch + HWY_PLL_COLS_53 * (height - 2) + Lanes(di));

      Store(sn_0, di, scratch + HWY_PLL_COLS_53 * (height - 1) + 0);
      Store(sn_1, di, scratch + HWY_PLL_COLS_53 * (height - 1) + Lanes(di));
    }
    else
    {
      Store(d1_0 + sc_0, di, scratch + HWY_PLL_COLS_53 * (height - 1) + 0);
      Store(d1_1 + sc_1, di, scratch + HWY_PLL_COLS_53 * (height - 1) + Lanes(di));
    }
    hwy_v_final_store_53(scratch, height, dest, strideDest);
  }

} // namespace HWY_NAMESPACE
} // namespace grk
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace grk
{
HWY_EXPORT(hwy_v_p0_53);
HWY_EXPORT(hwy_v_p1_53);
HWY_EXPORT(GetHWY_PLL_COLS_53); // Export GetHWY_PLL_COLS_53

std::unique_ptr<WaveletReverse::BufferPtr[]> WaveletReverse::horizPoolData_ = nullptr;
std::unique_ptr<WaveletReverse::BufferPtr[]> WaveletReverse::vertPoolData_ = nullptr;
bool WaveletReverse::is_allocated_ = false;
std::once_flag WaveletReverse::alloc_flag_;

uint32_t get_PLL_COLS_53()
{
  static uint32_t value = HWY_DYNAMIC_DISPATCH(GetHWY_PLL_COLS_53)();
  return value;
}

bool WaveletReverse::allocPoolData(size_t maxDim)
{
  if(maxDim == 0)
  {
    return false;
  }
  size_t num_threads = ExecSingleton::num_threads();
  if(num_threads == 0)
  {
    return false;
  }
  std::call_once(alloc_flag_, [&]() {
    try
    {
      horizPoolData_ = std::make_unique<BufferPtr[]>(num_threads);
      vertPoolData_ = std::make_unique<BufferPtr[]>(num_threads);

      size_t buffer_size = maxDim;
      auto multiplier = std::max(sizeof(int32_t) * get_PLL_COLS_53(), sizeof(vec4f));
      buffer_size *= multiplier;
      for(size_t i = 0; i < num_threads; ++i)
      {
        void* horiz_ptr = grk_aligned_malloc(buffer_size);
        void* vert_ptr = grk_aligned_malloc(buffer_size);
        if(!horiz_ptr || !vert_ptr)
        {
          if(horiz_ptr)
            grk_aligned_free(horiz_ptr);
          if(vert_ptr)
            grk_aligned_free(vert_ptr);
          throw std::bad_alloc();
        }
        horizPoolData_[i] = BufferPtr(static_cast<uint8_t*>(horiz_ptr));
        vertPoolData_[i] = BufferPtr(static_cast<uint8_t*>(vert_ptr));
      }

      is_allocated_ = true;
    }
    catch(const std::bad_alloc&)
    {
      horizPoolData_.reset();
      vertPoolData_.reset();
      is_allocated_ = false;
    }
  });

  return is_allocated_;
}

WaveletReverse::WaveletReverse(TileProcessor* tileProcessor, TileComponent* tilec, uint16_t compno,
                               Rect32 unreducedWindow, uint8_t numres, uint8_t qmfbid)
    : tileProcessor_(tileProcessor), scheduler_(tileProcessor->getScheduler()), tilec_(tilec),
      compno_(compno), unreducedWindow_(unreducedWindow), numres_(numres), qmfbid_(qmfbid)
{}
WaveletReverse::~WaveletReverse(void)
{
  for(const auto& t : partialTasks53_)
    delete t;
  for(const auto& t : partialTasks97_)
    delete t;
}

/* Performs lifting in one single iteration. Saves memory */
/* accesses and explicit interleaving. */
void WaveletReverse::load_h_p0_53(int32_t* scratch, const uint32_t width, int32_t* bandL,
                                  int32_t* bandH, int32_t* dest)
{
  assert(width > 1);

  int32_t s1n = bandL[0];
  int32_t d1n = bandH[0];
  int32_t s0n = s1n - ((d1n + 1) >> 1);
  uint32_t i = 0;
  if(width > 2)
  {
    for(uint32_t j = 1; i < (width - 3); i += 2, j++)
    {
      int32_t d1c = d1n;
      int32_t s0c = s0n;

      s1n = bandL[j];
      d1n = bandH[j];
      s0n = s1n - ((d1c + d1n + 2) >> 2);
      scratch[i] = s0c;
      scratch[i + 1] = d1c + ((s0c + s0n) >> 1);
    }
  }
  scratch[i] = s0n;
  if(width & 1)
  {
    scratch[width - 1] = bandL[(width - 1) >> 1] - ((d1n + 1) >> 1);
    scratch[width - 2] = d1n + ((s0n + scratch[width - 1]) >> 1);
  }
  else
  {
    scratch[width - 1] = d1n + s0n;
  }
  memcpy(dest, scratch, (size_t)width * sizeof(int32_t));
}

/* Performs lifting in one single iteration. Saves memory
   accesses and explicit interleaving. */
void WaveletReverse::load_h_p1_53(int32_t* scratch, const uint32_t width, int32_t* bandL,
                                  int32_t* bandH, int32_t* dest)
{
  assert(width > 2);
  int32_t d1c = bandH[1];
  int32_t s0c = bandL[0] - ((bandH[0] + d1c + 2) >> 2);
  scratch[0] = bandH[0] + s0c; // reflection at boundary
  uint32_t i, j;
  for(i = 1, j = 1; i < (width - 2 - !(width & 1)); i += 2, j++)
  {
    int32_t d1n = bandH[j + 1];
    int32_t s0n = bandL[j] - ((d1c + d1n + 2) >> 2);

    scratch[i] = s0c;
    scratch[i + 1] = d1c + ((s0n + s0c) >> 1);

    s0c = s0n;
    d1c = d1n;
  }
  scratch[i] = s0c;
  if(!(width & 1))
  {
    int32_t sn = bandL[(width >> 1) - 1] - ((d1c + 1) >> 1);

    scratch[width - 2] = d1c + ((sn + s0c) >> 1);
    scratch[width - 1] = sn;
  }
  else
  {
    scratch[width - 1] = d1c + s0c;
  }
  memcpy(dest, scratch, (size_t)width * sizeof(int32_t));
}

/* <summary>                            */
/* Inverse 5-3 wavelet transform in 1-D for one row. */
/* </summary>                           */
/* Performs interleave, inverse wavelet transform and copy back to buffer */
void WaveletReverse::load_h_53(const dwt_scratch<int32_t>* scratch, int32_t* bandL, int32_t* bandH,
                               int32_t* dest)
{
  const uint32_t width = scratch->sn + scratch->dn;
  assert(width != 0);
  if(scratch->parity == 0)
  {
    if(width > 1)
    {
      load_h_p0_53(scratch->mem, width, bandL, bandH, dest);
    }
    else
    {
      assert(scratch->sn == 1);
      // only L op: only one sample in L band and H band is empty
      dest[0] = bandL[0];
    }
  }
  else
  {
    if(width == 1)
    {
      assert(scratch->dn == 1);
      // only H op: only one sample in H band and L band is empty
      // todo: explain why we use bandOdd i.e. low pass band to calculate this
      dest[0] = bandH[0] >> 1;
    }
    else if(width == 2)
    {
      const int32_t s0 = bandL[0] - ((bandH[0] + 1) >> 1);
      dest[0] = bandH[0] + s0;
      dest[1] = s0;
    }
    else
    {
      load_h_p1_53(scratch->mem, width, bandL, bandH, dest);
    }
  }
}

void WaveletReverse::h_strip_53(const dwt_scratch<int32_t>* scratch, uint32_t hMin, uint32_t hMax,
                                Buffer2dSimple<int32_t> winL, Buffer2dSimple<int32_t> winH,
                                Buffer2dSimple<int32_t> winDest)
{
  for(uint32_t j = hMin; j < hMax; ++j)
  {
    load_h_53(scratch, winL.buf_, winH.buf_, winDest.buf_);
    winL.incY_IN_PLACE(1);
    winH.incY_IN_PLACE(1);
    winDest.incY_IN_PLACE(1);
  }
}
bool WaveletReverse::h_53(uint8_t res, TileComponentWindow<int32_t>* tileBuffer, uint32_t resHeight)
{
  uint32_t num_threads = (uint32_t)ExecSingleton::num_threads();
  Buffer2dSimple<int32_t> winL, winH, winDest;
  auto imageComponentFlow = ((DecompressScheduler*)scheduler_)->getImageComponentFlow(compno_);
  auto resFlow = imageComponentFlow->getResflow(res - 1);
  uint32_t numTasks[2] = {0, 0};
  uint32_t height[2] = {0, 0};

  // top "half" of buffer becomes vertical L orientation, and bottom "half" of buffer
  // becomes vertical H orientation
  for(uint32_t orient = 0; orient < 2; ++orient)
  {
    height[orient] = (orient == 0) ? vert_.sn : resHeight - vert_.sn;
    if(num_threads > 1)
      numTasks[orient] = height[orient] < num_threads ? height[orient] : num_threads;
    height[orient] = (orient == 0) ? vertPool_[0].sn : resHeight - vertPool_[0].sn;
    if(num_threads > 1)
      numTasks[orient] = height[orient] < num_threads ? height[orient] : num_threads;
  }
  for(uint32_t orient = 0; orient < 2; ++orient)
  {
    if(height[orient] == 0)
      continue;
    if(orient == 0)
    {
      winL = tileBuffer->getResWindowBufferSimple((uint8_t)(res - 1U));
      winH = tileBuffer->getBandWindowBufferPaddedSimple(res, BAND_ORIENT_HL);
      winDest = tileBuffer->getResWindowBufferSplitSimple(res, SPLIT_L);
    }
    else
    {
      winL = tileBuffer->getBandWindowBufferPaddedSimple(res, BAND_ORIENT_LH);
      winH = tileBuffer->getBandWindowBufferPaddedSimple(res, BAND_ORIENT_HH);
      winDest = tileBuffer->getResWindowBufferSplitSimple(res, SPLIT_H);
    }
    if(num_threads == 1)
    {
      h_strip_53(&horizPool_[0], 0, height[orient], winL, winH, winDest);
    }
    else
    {
      uint32_t heightIncr = height[orient] / numTasks[orient];
      for(uint32_t j = 0; j < numTasks[orient]; ++j)
      {
        auto hMin = j * heightIncr;
        auto hMax = j < (numTasks[orient] - 1U) ? (j + 1U) * heightIncr : height[orient];
        uint32_t sn = horiz_.sn;
        uint32_t dn = horiz_.dn;
        uint32_t parity = horiz_.parity;
        resFlow->waveletHoriz_->nextTask().work(
            [this, sn, dn, parity, winL, winH, winDest, hMin, hMax] {
              horizPool_[ExecSingleton::workerId()].sn = sn;
              horizPool_[ExecSingleton::workerId()].dn = dn;
              horizPool_[ExecSingleton::workerId()].parity = parity;
              h_strip_53(&horizPool_[ExecSingleton::workerId()], hMin, hMax, winL, winH, winDest);
            });
        winL.incY_IN_PLACE(heightIncr);
        winH.incY_IN_PLACE(heightIncr);
        winDest.incY_IN_PLACE(heightIncr);
      }
    }
  }

  return true;
}

/** Vertical inverse 5x3 wavelet transform for one column, when top-most
 * pixel is on even coordinate */
void WaveletReverse::v_p0_53(int32_t* scratch, const uint32_t height, int32_t* bandL,
                             const uint32_t strideL, int32_t* bandH, const uint32_t strideH,
                             int32_t* dest, const uint32_t strideDest)
{
  assert(height > 1);

  /* Performs lifting in one single iteration. Saves memory */
  /* accesses and explicit interleaving. */
  int32_t s1n = bandL[0];
  int32_t d1n = bandH[0];
  int32_t s0n = s1n - ((d1n + 1) >> 1);

  uint32_t i = 0;
  if(height > 2)
  {
    auto bL = bandL + strideL;
    auto bH = bandH + strideH;
    for(uint32_t j = 0; i < (height - 3); i += 2, j++)
    {
      int32_t d1c = d1n;
      int32_t s0c = s0n;
      s1n = *bL;
      bL += strideL;
      d1n = *bH;
      bH += strideH;
      s0n = s1n - ((d1c + d1n + 2) >> 2);
      scratch[i] = s0c;
      scratch[i + 1] = d1c + ((s0c + s0n) >> 1);
    }
  }
  scratch[i] = s0n;
  if(height & 1)
  {
    scratch[height - 1] = bandL[((height - 1) >> 1) * strideL] - ((d1n + 1) >> 1);
    scratch[height - 2] = d1n + ((s0n + scratch[height - 1]) >> 1);
  }
  else
  {
    scratch[height - 1] = d1n + s0n;
  }
  for(i = 0; i < height; ++i)
  {
    *dest = scratch[i];
    dest += strideDest;
  }
}
/** Vertical inverse 5x3 wavelet transform for one column, when top-most
 * pixel is on odd coordinate */
void WaveletReverse::v_p1_53(int32_t* scratch, const uint32_t height, int32_t* bandH,
                             const uint32_t strideH, int32_t* bandL, const uint32_t strideL,
                             int32_t* dest, const uint32_t strideDest)
{
  assert(height > 2);

  /* Performs lifting in one single iteration. Saves memory */
  /* accesses and explicit interleaving. */
  int32_t s1 = bandL[strideL];
  int32_t dc = bandH[0] - ((bandL[0] + s1 + 2) >> 2);
  scratch[0] = bandL[0] + dc;
  auto s2_ptr = bandL + (strideL << 1);
  auto dn_ptr = bandH + strideH;
  uint32_t i, j;
  for(i = 1, j = 1; i < (height - 2 - !(height & 1)); i += 2, j++)
  {
    int32_t s2 = *s2_ptr;
    s2_ptr += strideL;

    int32_t dn = *dn_ptr - ((s1 + s2 + 2) >> 2);
    dn_ptr += strideH;

    scratch[i] = dc;
    scratch[i + 1] = s1 + ((dn + dc) >> 1);
    dc = dn;
    s1 = s2;
  }
  scratch[i] = dc;
  if(!(height & 1))
  {
    int32_t dn = bandH[((height >> 1) - 1) * strideH] - ((s1 + 1) >> 1);
    scratch[height - 2] = s1 + ((dn + dc) >> 1);
    scratch[height - 1] = dn;
  }
  else
  {
    scratch[height - 1] = s1 + dc;
  }
  for(i = 0; i < height; ++i)
  {
    *dest = scratch[i];
    dest += strideDest;
  }
}

/* <summary>                            */
/* Inverse vertical 5-3 wavelet transform in 1-D for several columns. */
/* </summary>                           */
/* Performs interleave, inverse wavelet transform and copy back to buffer */
/** Number of columns that we can process in parallel in the vertical pass */
void WaveletReverse::v_53(const dwt_scratch<int32_t>* scratch, Buffer2dSimple<int32_t> winL,
                          Buffer2dSimple<int32_t> winH, Buffer2dSimple<int32_t> winDest,
                          uint32_t nb_cols)
{
  const uint32_t height = scratch->sn + scratch->dn;
  assert(height != 0);
  if(scratch->parity == 0)
  {
    if(height == 1)
    {
      for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winDest.buf_++)
        winDest.buf_[0] = winL.buf_[0];
    }
    else
    {
      if(nb_cols == get_PLL_COLS_53())
      {
        /* Same as below general case, except that thanks to SSE2/AVX2 */
        /* we can efficiently process 8/16 columns in parallel */
        HWY_DYNAMIC_DISPATCH(hwy_v_p0_53)
        (scratch->mem, height, winL.buf_, winL.stride_, winH.buf_, winH.stride_, winDest.buf_,
         winDest.stride_);
      }
      else
      {
        for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winH.buf_++, winDest.buf_++)
          v_p0_53(scratch->mem, height, winL.buf_, winL.stride_, winH.buf_, winL.stride_,
                  winDest.buf_, winDest.stride_);
      }
    }
  }
  else
  {
    if(height == 1)
    {
      for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winDest.buf_++)
        winDest.buf_[0] = winL.buf_[0] >> 1;
    }
    else if(height == 2)
    {
      for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winH.buf_++, winDest.buf_++)
      {
        scratch->mem[1] = winL.buf_[0] - ((winH.buf_[0] + 1) >> 1);
        winDest.buf_[0] = winH.buf_[0] + scratch->mem[1];
        winDest.buf_[1] = scratch->mem[1];
      }
    }
    else
    {
      if(nb_cols == get_PLL_COLS_53())
      {
        /* Same as below general case, except that thanks to SSE2/AVX2 */
        /* we can efficiently process 8/16 columns in parallel */
        HWY_DYNAMIC_DISPATCH(hwy_v_p1_53)
        (scratch->mem, height, winL.buf_, winL.stride_, winH.buf_, winH.stride_, winDest.buf_,
         winDest.stride_);
      }
      else
      {
        for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winH.buf_++, winDest.buf_++)
          v_p1_53(scratch->mem, height, winL.buf_, winL.stride_, winH.buf_, winH.stride_,
                  winDest.buf_, winDest.stride_);
      }
    }
  }
}

void WaveletReverse::v_strip_53(const dwt_scratch<int32_t>* scratch, uint32_t wMin, uint32_t wMax,
                                Buffer2dSimple<int32_t> winL, Buffer2dSimple<int32_t> winH,
                                Buffer2dSimple<int32_t> winDest)
{
  uint32_t j;
  for(j = wMin; j + get_PLL_COLS_53() <= wMax; j += get_PLL_COLS_53())
  {
    v_53(scratch, winL, winH, winDest, get_PLL_COLS_53());
    winL.incX_IN_PLACE(get_PLL_COLS_53());
    winH.incX_IN_PLACE(get_PLL_COLS_53());
    winDest.incX_IN_PLACE(get_PLL_COLS_53());
  }
  if(j < wMax)
    v_53(scratch, winL, winH, winDest, wMax - j);
}

bool WaveletReverse::v_53(uint8_t res, TileComponentWindow<int32_t>* buf, uint32_t resWidth)
{
  if(resWidth == 0)
    return true;
  uint32_t num_threads = (uint32_t)ExecSingleton::num_threads();
  auto winL = buf->getResWindowBufferSplitSimple(res, SPLIT_L);
  auto winH = buf->getResWindowBufferSplitSimple(res, SPLIT_H);
  auto winDest = buf->getResWindowBufferSimple(res);
  if(num_threads == 1)
  {
    v_strip_53(&vertPool_[0], 0, resWidth, winL, winH, winDest);
  }
  else
  {
    auto imageComponentFlow = ((DecompressScheduler*)scheduler_)->getImageComponentFlow(compno_);
    auto resFlow = imageComponentFlow->getResflow(res - 1);
    const uint32_t numTasks = resWidth < num_threads ? resWidth : num_threads;
    uint32_t widthIncr = resWidth / numTasks;
    for(uint32_t j = 0; j < numTasks; j++)
    {
      auto wMin = j * widthIncr;
      auto wMax = j < (numTasks - 1U) ? (j + 1U) * widthIncr : resWidth;
      uint32_t sn = vert_.sn;
      uint32_t dn = vert_.dn;
      uint32_t parity = vert_.parity;
      resFlow->waveletVert_->nextTask().work(
          [this, sn, dn, parity, wMin, wMax, winL, winH, winDest] {
            vertPool_[ExecSingleton::workerId()].dn = dn;
            vertPool_[ExecSingleton::workerId()].sn = sn;
            vertPool_[ExecSingleton::workerId()].parity = parity;

            v_strip_53(&vertPool_[ExecSingleton::workerId()], wMin, wMax, winL, winH, winDest);
          });
      winL.incX_IN_PLACE(widthIncr);
      winH.incX_IN_PLACE(widthIncr);
      winDest.incX_IN_PLACE(widthIncr);
    }
  }
  return true;
}

/* <summary>                            */
/* Inverse wavelet transform in 2-D.    */
/* </summary>                           */
bool WaveletReverse::tile_53(void)
{
  if(numres_ == 1U)
    return true;

  // for resolution n, tileCompRes points to LL subband at res n-1
  auto bandLL = tilec_->resolutions_;
  auto tileBuffer = tilec_->getWindow();

  uint32_t num_threads = (uint32_t)ExecSingleton::num_threads();
  horizPool_ = std::make_unique<dwt_scratch<int32_t>[]>(num_threads);
  vertPool_ = std::make_unique<dwt_scratch<int32_t>[]>(num_threads);
  for(uint8_t res = 1; res < numres_; ++res)
  {
    horiz_.sn = bandLL->width();
    vert_.sn = bandLL->height();
    for(uint32_t i = 0; i < num_threads; ++i)
    {
      horizPool_[i].sn = bandLL->width();
      vertPool_[i].sn = bandLL->height();
    }
    ++bandLL;
    auto resWidth = bandLL->width();
    auto resHeight = bandLL->height();
    if(resWidth == 0 || resHeight == 0)
      continue;
    horiz_.dn = resWidth - horiz_.sn;
    horiz_.parity = bandLL->x0 & 1;
    vert_.dn = resHeight - vert_.sn;
    vert_.parity = bandLL->y0 & 1;
    for(uint32_t i = 0; i < num_threads; ++i)
    {
      horizPool_[i].dn = resWidth - horizPool_[i].sn;
      horizPool_[i].parity = bandLL->x0 & 1;
      horizPool_[i].allocatedMem = (int32_t*)WaveletReverse::horizPoolData_[i].get();
      horizPool_[i].mem = (int32_t*)WaveletReverse::horizPoolData_[i].get();

      vertPool_[i].dn = resHeight - vertPool_[i].sn;
      vertPool_[i].parity = bandLL->y0 & 1;
      vertPool_[i].allocatedMem = (int32_t*)WaveletReverse::vertPoolData_[i].get();
      vertPool_[i].mem = (int32_t*)WaveletReverse::vertPoolData_[i].get();
    }
    if(num_threads == 1)
      vertPool_[0].mem = horizPool_[0].mem;
    if(!h_53(res, tileBuffer, resHeight))
      return false;
    if(!v_53(res, tileBuffer, resWidth))
      return false;
  }

  return true;
}

bool WaveletReverse::decompress(void)
{
  if(!tileProcessor_->getTCP()->wholeTileDecompress_)
    return decompressPartial();

  if(qmfbid_ == 1)
    return tile_53();
  else
    return tile_97();
}

} // namespace grk
#endif
