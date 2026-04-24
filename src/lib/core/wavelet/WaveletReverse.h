/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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

#include "CodecScheduler.h"
#include "WaveletCommon.h"
#include "WaveletPoolData.h"

namespace grk
{

template<typename ST>
struct dwt_scratch
{
  dwt_scratch(void) = default;

  dwt_scratch(const dwt_scratch& rhs)
      : allocatedMem(nullptr), lenBytes_(0), paddingBytes_(0), mem(nullptr), memL(nullptr),
        memH(nullptr), sn(rhs.sn), dn(rhs.dn), parity(rhs.parity), win_l(rhs.win_l),
        win_h(rhs.win_h), resno(rhs.resno), outputStart(rhs.outputStart),
        outputCount(rhs.outputCount)
  {}
  ~dwt_scratch(void)
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
    if(len > (SIZE_MAX / sizeof(ST)))
    {
      grklog.error("data size overflow");
      return false;
    }
    paddingBytes_ = grk_make_aligned_width<ST>((uint32_t)padding * 2 + 32) * sizeof(ST);
    lenBytes_ = len * sizeof(ST) + 2 * paddingBytes_;
    allocatedMem = (ST*)grk_aligned_malloc(lenBytes_);
    if(!allocatedMem)
    {
      grklog.error("Failed to allocate %u bytes", lenBytes_);
      return false;
    }
    ownsData_ = true;
    mem = allocatedMem + paddingBytes_ / sizeof(ST);

    return (allocatedMem != nullptr) ? true : false;
  }
  void clear(void)
  {
    if(!allocatedMem)
      return;
    memset(allocatedMem, 0, lenBytes_);
  }
  void release(void)
  {
    if(ownsData_)
      grk_aligned_free(allocatedMem);
    allocatedMem = nullptr;
    mem = nullptr;
    memL = nullptr;
    memH = nullptr;
  }
  ST* allocatedMem = nullptr;
  size_t lenBytes_ = 0;
  size_t paddingBytes_ = 0;
  ST* mem = nullptr;
  ST* memL = nullptr;
  ST* memH = nullptr;
  bool ownsData_ = false;
  uint32_t sn = 0; /* number of elements in low pass band */
  uint32_t dn = 0; /* number of elements in high pass band */
  uint32_t parity = 0; /* 0 = start on even coord, 1 = start on odd coord */
  Line32 win_l;
  Line32 win_h;
  uint8_t resno = 0;
  /* Cascade synthesis: V-DWT output range within the extended stripe.
   * Only rows [outputStart, outputStart+outputCount) are written. */
  uint32_t outputStart = 0;
  uint32_t outputCount = 0;
};

struct DcShiftParam
{
  int32_t shift = 0;
  int32_t min = 0;
  int32_t max = 0;
  bool enabled = false;
};

class WaveletReverse
{
public:
  WaveletReverse(CodecScheduler* scheduler, TileComponent* tilec, uint16_t compno, Rect32 window,
                 uint8_t numres, uint8_t qmfbid, uint32_t maxDim, bool wholeTileDecompress,
                 WaveletPoolData* poolData, DcShiftParam dcShift = {},
                 bool cascadeSynthesis = false);
  ~WaveletReverse(void);
  bool decompress(void);

  static void step_97(dwt_scratch<vec4f>* GRK_RESTRICT dwt);

private:
  WaveletPoolData* poolData_ = nullptr;
  CodecScheduler* scheduler_ = nullptr;
  TileComponent* tilec_ = nullptr;
  uint16_t compno_ = 0;
  Rect32 unreducedWindow_;
  uint8_t numres_ = 0;
  uint8_t qmfbid_ = 0;
  uint32_t maxDim_ = 0;
  bool wholeTileDecompress_ = true;
  DcShiftParam dcShift_;

  // 5/3 ////////////////////////////////////////////////////////////////////////////////////
  void load_h_p0_53(int32_t* scratch, const uint32_t width, const int32_t* bandL,
                    const int32_t* bandH, int32_t* dest);

  void load_h_p1_53(int32_t* scratch, const uint32_t width, const int32_t* bandL,
                    const int32_t* bandH, int32_t* dest);

  void h_53(uint8_t res, TileComponentWindow<int32_t>* scratch, uint32_t resHeight);

  void h_strip_53(const dwt_scratch<int32_t>* scratch, uint32_t hMin, uint32_t hMax,
                  Buffer2dSimple<int32_t> winL, Buffer2dSimple<int32_t> winH,
                  Buffer2dSimple<int32_t> winDest);

  void load_h_53(const dwt_scratch<int32_t>* scratch, int32_t* bandL, int32_t* bandH,
                 int32_t* dest);

  void v_p0_53(int32_t* scratch, const uint32_t height, int32_t* bandL, const uint32_t strideL,
               int32_t* bandH, const uint32_t strideH, int32_t* dest, const uint32_t strideDest);

  void v_p1_53(int32_t* scratch, const uint32_t height, int32_t* bandL, const uint32_t strideL,
               int32_t* bandH, const uint32_t strideH, int32_t* dest, const uint32_t strideDest);

  void v_53(const dwt_scratch<int32_t>* scratch, Buffer2dSimple<int32_t> winL,
            Buffer2dSimple<int32_t> winH, Buffer2dSimple<int32_t> winDest, uint32_t nb_cols,
            DcShiftParam dcShift);

  void v_strip_53(const dwt_scratch<int32_t>* scratch, uint32_t wMin, uint32_t wMax,
                  Buffer2dSimple<int32_t> winL, Buffer2dSimple<int32_t> winH,
                  Buffer2dSimple<int32_t> winDest, DcShiftParam dcShift);

  void v_53(uint8_t res, TileComponentWindow<int32_t>* buf, uint32_t resWidth);

  bool tile_53(void);

  dwt_scratch<int32_t> horiz_;
  dwt_scratch<int32_t> vert_;
  std::unique_ptr<dwt_scratch<int32_t>[]> horizPool_;
  std::unique_ptr<dwt_scratch<int32_t>[]> vertPool_;

  // 16-bit 5/3 //////////////////////////////////////////////////////////////////////////////
  void load_h_p0_16_53(int16_t* scratch, const uint32_t width, const int16_t* bandL,
                       const int16_t* bandH, int16_t* dest);

  void load_h_p1_16_53(int16_t* scratch, const uint32_t width, const int16_t* bandL,
                       const int16_t* bandH, int16_t* dest);

  void load_h_16_53(const dwt_scratch<int16_t>* scratch, int16_t* bandL, int16_t* bandH,
                    int16_t* dest);

  void h_strip_16_53(const dwt_scratch<int16_t>* scratch, uint32_t hMin, uint32_t hMax,
                     Buffer2dSimple<int16_t> winL, Buffer2dSimple<int16_t> winH,
                     Buffer2dSimple<int16_t> winDest);

  void h_16_53(uint8_t res, TileComponentWindow<int16_t>* scratch, uint32_t resHeight);

  void v_p0_16_53(int16_t* scratch, const uint32_t height, int16_t* bandL, const uint32_t strideL,
                  int16_t* bandH, const uint32_t strideH, int16_t* dest, const uint32_t strideDest);

  void v_p1_16_53(int16_t* scratch, const uint32_t height, int16_t* bandL, const uint32_t strideL,
                  int16_t* bandH, const uint32_t strideH, int16_t* dest, const uint32_t strideDest);

  void v_16_53(const dwt_scratch<int16_t>* scratch, Buffer2dSimple<int16_t> winL,
               Buffer2dSimple<int16_t> winH, Buffer2dSimple<int16_t> winDest, uint32_t nb_cols,
               DcShiftParam dcShift);

  void v_strip_16_53(const dwt_scratch<int16_t>* scratch, uint32_t wMin, uint32_t wMax,
                     Buffer2dSimple<int16_t> winL, Buffer2dSimple<int16_t> winH,
                     Buffer2dSimple<int16_t> winDest, DcShiftParam dcShift);

  void v_16_53(uint8_t res, TileComponentWindow<int16_t>* buf, uint32_t resWidth);

  bool tile_16_53(void);

  // 16-bit fixed-point 9/7 //////////////////////////////////////////////////////////////////
  void h_strip_16_97(const dwt_scratch<int16_t>* scratch, uint32_t hMin, uint32_t hMax,
                     Buffer2dSimple<int16_t> winL, Buffer2dSimple<int16_t> winH,
                     Buffer2dSimple<int16_t> winDest);

  void h_16_97(uint8_t res, TileComponentWindow<int16_t>* scratch, uint32_t resHeight);

  void v_16_97(const dwt_scratch<int16_t>* scratch, Buffer2dSimple<int16_t> winL,
               Buffer2dSimple<int16_t> winH, Buffer2dSimple<int16_t> winDest, uint32_t nb_cols,
               DcShiftParam dcShift);

  void v_strip_16_97(const dwt_scratch<int16_t>* scratch, uint32_t wMin, uint32_t wMax,
                     Buffer2dSimple<int16_t> winL, Buffer2dSimple<int16_t> winH,
                     Buffer2dSimple<int16_t> winDest, DcShiftParam dcShift);

  void v_16_97(uint8_t res, TileComponentWindow<int16_t>* buf, uint32_t resWidth);

  bool tile_16_97(void);

  dwt_scratch<int16_t> horiz16_;
  dwt_scratch<int16_t> vert16_;
  std::unique_ptr<dwt_scratch<int16_t>[]> horizPool16_;
  std::unique_ptr<dwt_scratch<int16_t>[]> vertPool16_;

  ////////////////////////////////////////////////////////////////////////////////////////////
  // 9/7

  void h_strip_97(dwt_scratch<vec4f>* GRK_RESTRICT horiz, const uint32_t resHeight,
                  Buffer2dSimple<float> winL, Buffer2dSimple<float> winH,
                  Buffer2dSimple<float> winDest);

  bool h_97(uint8_t res, uint32_t num_threads, size_t dataLength,
            dwt_scratch<vec4f>& GRK_RESTRICT horiz, const uint32_t resHeight,
            Buffer2dSimple<float> winL, Buffer2dSimple<float> winH, Buffer2dSimple<float> winDest);

  void v_strip_97(dwt_scratch<vec4f>* GRK_RESTRICT vert, const uint32_t resWidth,
                  const uint32_t resHeight, Buffer2dSimple<float> winL, Buffer2dSimple<float> winH,
                  Buffer2dSimple<float> winDest, DcShiftParam dcShift);

  bool v_97(uint8_t res, uint32_t num_threads, size_t dataLength,
            dwt_scratch<vec4f>& GRK_RESTRICT vert, const uint32_t resWidth,
            const uint32_t resHeight, Buffer2dSimple<float> winL, Buffer2dSimple<float> winH,
            Buffer2dSimple<float> winDest);

  // ======================== Cascade synthesis ========================
  //
  // Stripe-based combined H+V 9/7 inverse DWT. Instead of the traditional
  // two-phase approach (H-DWT entire image → sync → V-DWT entire image),
  // cascade synthesis divides the image into horizontal stripes of ~64
  // interleaved rows and processes each stripe through both H and V in
  // one pass. This keeps intermediate data hot in L2/L3 cache, avoiding
  // the DRAM round-trip that occurs when the full-resolution SPLIT buffers
  // (potentially tens of MB for 4K+ images) are cold by the time the
  // V-DWT reads them.
  //
  // Each stripe includes a halo of ±4 sub-band rows (8 interleaved rows)
  // to provide context for the 9/7 V-DWT lifting steps at stripe
  // boundaries. The halo introduces ~12% redundant H-DWT work, but this
  // is offset by the elimination of SPLIT buffer cache misses.
  //
  // Opt-in: set GRK_CASCADE_DWT=1 environment variable. Output is
  // bit-exact with the traditional path.
  //
  // Architecture:
  //   tile_97_cascade   - per-resolution loop (replaces tile_97)
  //   cascade_97        - scheduling: splits image into stripes, creates tasks
  //   cascade_strip_97  - per-stripe kernel: H-DWT + V-DWT + output
  //   hwy_v_cascade_stripe_97 - SIMD V-DWT with partial row output

  // Per-stripe cascade kernel. H-DWT for both sub-band pairs (LL+HL, LH+HH)
  // into task-local buffers, then V-DWT with partial output (central rows only,
  // halo rows are lifted but not written).
  void cascade_strip_97(dwt_scratch<vec4f>* GRK_RESTRICT hScratch,
                        dwt_scratch<vec4f>* GRK_RESTRICT vScratch,
                        const uint32_t resWidth, const uint32_t resHeight,
                        Buffer2dSimple<float> winLL, Buffer2dSimple<float> winHL,
                        Buffer2dSimple<float> winLH, Buffer2dSimple<float> winHH,
                        Buffer2dSimple<float> winDest, DcShiftParam dcShift);

  // Schedule cascade tasks for one resolution level. Divides resHeight into
  // 64-row interleaved stripes with 8-row halo. Each task processes one or
  // more stripes. Tasks are scheduled into waveletHoriz_ (waveletVert_ is
  // unused in cascade mode). Stripe tasks write to tempDest; a final copy-back
  // task (also in waveletHoriz_, with .precede() dependencies) copies results
  // to realDest. This avoids aliasing because sub-band inputs (LL, HL, LH, HH)
  // share memory with realDest in whole-tile mode.
  bool cascade_97(uint8_t res, uint32_t num_threads, size_t dataLength,
                  dwt_scratch<vec4f>& GRK_RESTRICT hScratch,
                  dwt_scratch<vec4f>& GRK_RESTRICT vScratch,
                  const uint32_t resWidth, const uint32_t resHeight,
                  Buffer2dSimple<float> winLL, Buffer2dSimple<float> winHL,
                  Buffer2dSimple<float> winLH, Buffer2dSimple<float> winHH,
                  Buffer2dSimple<float> tempDest, Buffer2dSimple<float> realDest,
                  std::shared_ptr<std::vector<float>> tempBufMem);

  // Entry point for cascade 9/7 inverse DWT (replaces tile_97 when enabled).
  bool tile_97_cascade(void);

  bool tile_97(void);

  dwt_scratch<vec4f> horiz97;
  dwt_scratch<vec4f> vert97;
  dwt_scratch<vec4f> cascade97_h; // H-DWT scratch for cascade synthesis
  dwt_scratch<vec4f> cascade97_v; // V-DWT scratch for cascade synthesis

  // When true, use cascade (stripe-based combined H+V) DWT instead of the
  // traditional two-phase approach. Controlled by GRK_CASCADE_DWT env var.
  bool cascadeSynthesis_ = false;
  ////////////////////////////////////////////////////////////////////////////////////////////////

  ///////////////////////////////////////////////////////////////////////////////////////////////
  // partial
  bool decompressPartial();

  template<typename T, typename S>
  struct PartialTaskInfo
  {
    PartialTaskInfo(S data, Buffer2dSimple<T> winLL, Buffer2dSimple<T> winHL,
                    Buffer2dSimple<T> winLH, Buffer2dSimple<T> winHH, Buffer2dSimple<T> winDest,
                    uint32_t indexMin, uint32_t indexMax)
        : data(data), winLL(winLL), winHL(winHL), winLH(winLH), winHH(winHH), winDest(winDest),
          indexMin_(indexMin), indexMax_(indexMax)
    {}
    PartialTaskInfo(S data, uint32_t indexMin, uint32_t indexMax)
        : data(data), indexMin_(indexMin), indexMax_(indexMax)
    {}
    ~PartialTaskInfo(void)
    {
      data.release();
    }
    S data;
    Buffer2dSimple<T> winLL;
    Buffer2dSimple<T> winHL;
    Buffer2dSimple<T> winLH;
    Buffer2dSimple<T> winHH;
    Buffer2dSimple<T> winDest;

    uint32_t indexMin_;
    uint32_t indexMax_;
  };

  template<typename ST, uint32_t FILTER_WIDTH, uint32_t VERT_PASS_WIDTH, typename D>
  bool partial_tile(ISparseCanvas<int32_t>* sa,
                    std::vector<PartialTaskInfo<ST, dwt_scratch<ST>>*>& tasks);

  // partial 5/3
  std::vector<PartialTaskInfo<int32_t, dwt_scratch<int32_t>>*> partialTasks53_;

  // partial 9/7
  std::vector<PartialTaskInfo<vec4f, dwt_scratch<vec4f>>*> partialTasks97_;
  ///////////////////////////////////////////////////////////////////////////////////////////////
};

} // namespace grk
