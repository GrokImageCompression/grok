/*
 * mercury streaming full-image fast path.
 * See mercury_fastpath.h. Decodes eligible streams through mercury's
 * C API using grok's own part-1 block coder (mercury_grok_t1_decode
 * shim), filling multiTileComposite_'s planes directly.
 */

#include "mercury_fastpath.h"

#if defined(GRK_MERCURY_BUILD)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <vector>
#include <cstring>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
#include <functional>
#include <mutex>
#include <optional>
#include <string>

#include "TFSingleton.h"
#include "grk_fseek.h"
#include "geometry.h"
#include "grk_exceptions.h"
#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "Logger.h"
#include "buffer.h"
#include "GrkObjectWrapper.h"
#include "TileFutureManager.h"
#include "FlowComponent.h"
#include "IStream.h"
#include "MarkerCache.h"
#include "FetchCommon.h"
#include "TPFetchSeq.h"
#include "GrkImage.h"
#include "IDecompressor.h"
#include "MemStream.h"
#include "StreamGenerator.h"
#include "MarkerParser.h"
#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct ITileProcessor;
}
#include "CodeStream.h"
#include "PacketLengthCache.h"
#include "TLMMarker.h"
#include "ICoder.h"
#include "CoderPool.h"
#include "BitIO.h"
#include "TileProcessor.h"
#include "TileCache.h"
#include "TileCompletion.h"
#include "CodeStreamDecompress.h"

#include "mercury_capi.h"

/* grok's part-1 block coder behind mercury's BlockCoder contract —
 * compiled into libgrokj2k on this branch (t1/part1/mercury_t1_shim.cpp). */
extern "C" int32_t mercury_grok_t1_decode(const uint8_t*, int32_t, int32_t, int32_t, int32_t,
                                          int32_t, int32_t, int32_t, int32_t, const int32_t*,
                                          int32_t, int32_t*);

namespace grk
{

namespace
{
  template<typename T>
  static inline void writeRow(void* plane, size_t off, const T* src, uint64_t width)
  {
    memcpy((T*)plane + off, src, width * sizeof(T));
  }

  struct RowCtx
  {
    void** planes;
    uint32_t* strides;
  };

  template<typename T>
  void rowSink(void* c, uint32_t row, const T* const* comps, uint32_t numComps, uint64_t width)
  {
    auto ctx = (RowCtx*)c;
    for(uint32_t i = 0; i < numComps; i++)
      writeRow(ctx->planes[i], (size_t)row * ctx->strides[i], comps[i], width);
  }

  template<typename T>
  void compRowSink(void* c, uint32_t comp, uint32_t row, const T* samples, uint64_t width)
  {
    auto ctx = (RowCtx*)c;
    writeRow(ctx->planes[comp], (size_t)row * ctx->strides[comp], samples, width);
  }

  // Streaming band mode: rows accumulate in a rows_per_strip-high scratch
  // window that is flushed through ioBandCallback_ (yBegin/yEnd are
  // band-relative, matching the classic drain loop's contract), then the
  // window slides down the image. Peak memory is O(strip), not O(image).
  struct BandCtx
  {
    grk_io_band_callback cb;
    void* cbUserData;
    GrkImage* scratch;
    std::vector<void*> planes;
    std::vector<uint32_t> strides;
    uint32_t bandRows; // scratch window height
    uint32_t height; // full image height
    uint32_t filled = 0;
    uint32_t bandsFlushed = 0;
    bool cbFailed = false;
  };

  template<typename T>
  void bandSink(void* c, uint32_t row, const T* const* comps, uint32_t numComps, uint64_t width)
  {
    auto ctx = (BandCtx*)c;
    if(ctx->cbFailed)
      return;
    for(uint32_t i = 0; i < numComps; i++)
      writeRow(ctx->planes[i], (size_t)ctx->filled * ctx->strides[i], comps[i], width);
    ctx->filled++;
    if(ctx->filled == ctx->bandRows || row + 1 == ctx->height)
    {
      if(!ctx->cb(0, ctx->filled, ctx->scratch, ctx->cbUserData))
      {
        ctx->cbFailed = true;
        return;
      }
      ctx->bandsFlushed++;
      uint32_t remaining = ctx->height - (row + 1);
      for(uint16_t i = 0; i < ctx->scratch->numcomps; i++)
      {
        auto comp = ctx->scratch->comps + i;
        comp->y0 += ctx->filled;
        comp->h = std::min(ctx->bandRows, remaining);
      }
      ctx->filled = 0;
    }
  }

  // One band per tile row, which is what classic's tileCompletion_ hands the
  // consumer: yEnd counts canvas rows while each component carries its own y0
  // and height, something a rows_per_strip window cannot express.
  struct TileRowBand
  {
    uint32_t yEnd;
    std::vector<uint32_t> y0;
    std::vector<uint32_t> h;
  };

  struct TileRowBandCtx
  {
    grk_io_band_callback cb;
    void* cbUserData;
    GrkImage* scratch;
    std::vector<void*> planes;
    std::vector<uint32_t> strides;
    std::vector<TileRowBand> bands;
    size_t current = 0;
    std::vector<uint32_t> filled; // rows of the current band, per component
    uint32_t bandsFlushed = 0;
    bool cbFailed = false;
  };

  // Components interleave in no fixed order, so a band is ready only once each
  // has delivered its whole height. A tile row a decode window emptied carries
  // no rows and passes straight through.
  void flushCompleteBands(TileRowBandCtx* ctx)
  {
    while(ctx->current < ctx->bands.size())
    {
      const auto& band = ctx->bands[ctx->current];
      bool complete = true;
      for(size_t c = 0; c < ctx->filled.size(); c++)
        complete = complete && ctx->filled[c] == band.h[c];
      if(!complete)
        return;
      if(band.yEnd > 0)
      {
        for(uint16_t c = 0; c < ctx->scratch->numcomps; c++)
        {
          auto comp = ctx->scratch->comps + c;
          comp->y0 = band.y0[c];
          comp->h = band.h[c];
        }
        if(!ctx->cb(0, band.yEnd, ctx->scratch, ctx->cbUserData))
        {
          ctx->cbFailed = true;
          return;
        }
        ctx->bandsFlushed++;
      }
      ctx->current++;
      std::fill(ctx->filled.begin(), ctx->filled.end(), 0u);
    }
  }

  template<typename T>
  void tileRowBandRow(TileRowBandCtx* ctx, uint32_t comp, const T* samples, uint64_t width)
  {
    writeRow(ctx->planes[comp], (size_t)ctx->filled[comp] * ctx->strides[comp], samples, width);
    ctx->filled[comp]++;
  }

  template<typename T>
  void tileRowBandCompSink(void* c, uint32_t comp, uint32_t, const T* samples, uint64_t width)
  {
    auto ctx = (TileRowBandCtx*)c;
    if(ctx->cbFailed)
      return;
    flushCompleteBands(ctx);
    if(ctx->cbFailed)
      return;
    tileRowBandRow(ctx, comp, samples, width);
    flushCompleteBands(ctx);
  }

  template<typename T>
  void tileRowBandSink(void* c, uint32_t, const T* const* comps, uint32_t numComps, uint64_t width)
  {
    auto ctx = (TileRowBandCtx*)c;
    if(ctx->cbFailed)
      return;
    flushCompleteBands(ctx);
    if(ctx->cbFailed)
      return;
    for(uint32_t i = 0; i < numComps; i++)
      tileRowBandRow(ctx, i, comps[i], width);
    flushCompleteBands(ctx);
  }

  // read_at source over one contiguous span. read-only, so concurrent
  // read_at calls need no lock.
  struct MemReader
  {
    const uint8_t* base = nullptr;
    uint64_t size = 0;
  };
  int32_t memReadAt(void* ctx, uint8_t* buf, uint64_t off, uint64_t len)
  {
    auto* self = static_cast<MemReader*>(ctx);
    if(off > self->size || len > self->size - off)
      return 0;
    memcpy(buf, self->base + off, (size_t)len);
    return 1;
  }
} // namespace

#if defined(_WIN32)
namespace
{
  // Positioned reader over a Win32 handle for mercury's read_at callback. The
  // callback runs concurrently on many decode threads; the handle is opened with
  // FILE_FLAG_OVERLAPPED so each ReadFile carries its own offset and event and
  // runs independently, with no shared file cursor and no serialization.
  struct Win32PositionedReader
  {
    HANDLE handle = INVALID_HANDLE_VALUE;

    ~Win32PositionedReader()
    {
      if(handle != INVALID_HANDLE_VALUE)
        CloseHandle(handle);
    }

    bool open(const std::string& path)
    {
      handle = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
      return handle != INVALID_HANDLE_VALUE;
    }

    uint64_t size() const
    {
      LARGE_INTEGER s{};
      return GetFileSizeEx(handle, &s) ? (uint64_t)s.QuadPart : 0;
    }
  };

  // One reusable completion event per decode thread: overlapped waits must not
  // share an event, and a thread only ever has one read in flight at a time.
  struct OverlappedEvent
  {
    HANDLE handle = CreateEventW(nullptr, TRUE /* manual reset */, FALSE, nullptr);
    ~OverlappedEvent()
    {
      if(handle)
        CloseHandle(handle);
    }
  };

  // mercury read_at contract: nonzero on success, 0 on failure (see capi.rs).
  int32_t win32ReadAt(void* ctx, uint8_t* buf, uint64_t off, uint64_t len)
  {
    auto* self = static_cast<Win32PositionedReader*>(ctx);
    thread_local OverlappedEvent event;
    if(!event.handle)
      return 0;
    uint64_t done = 0;
    while(done < len)
    {
      uint64_t pos = off + done;
      OVERLAPPED ov{};
      ov.Offset = (DWORD)(pos & 0xFFFFFFFFu);
      ov.OffsetHigh = (DWORD)(pos >> 32);
      ov.hEvent = event.handle;
      ResetEvent(event.handle);
      DWORD chunk = (DWORD)std::min<uint64_t>(len - done, 0x40000000u);
      DWORD got = 0;
      if(!ReadFile(self->handle, buf + done, chunk, &got, &ov))
      {
        if(GetLastError() != ERROR_IO_PENDING ||
           !GetOverlappedResult(self->handle, &ov, &got, TRUE))
          return 0; // error or EOF before the buffer was filled
      }
      if(got == 0)
        return 0;
      done += (uint64_t)got;
    }
    return 1;
  }
} // namespace
#endif

#define MFP_BAIL(msg)                                      \
  do                                                       \
  {                                                        \
    if(std::getenv("GRK_MERCURY_DEBUG"))                   \
      fprintf(stderr, "mercury fastpath bail: %s\n", msg); \
    return false;                                          \
  } while(0)

// Whole-image output sample type, replicating grok's classic decision so the
// fast path's API-visible comp->data_type matches (test_8bit_mct_produces_int16
// and friends compare it). grk_get_data_type is the BIBO-headroom rule
// createDecompressTileComponentWindows feeds into the final image type — it
// handles 9/7 int16 at prec<=9, unlike activateScratch's reversible-only logic.
// A region decode takes the same rule as a whole decode, matching the classic
// partial path. Per grok's uniform-type rule, one under-headroom component
// forces every component to int32.
static grk_data_type mercuryOutType(const TileCodingParams* tcp, const GrkImage* img)
{
  // MCT applies (int16 headroom loses a bit) only for a 3+ component RCT/ICT
  // image whose first three components share dimensions — and then only to
  // components 0-2, exactly grok's needsMctDecompress(compno)/tcp_->mct_==1.
  bool mctImage = tcp->mct_ == 1 && img->numcomps >= 3 && img->componentsEqual(3, false);
  for(uint16_t c = 0; c < img->numcomps; c++)
  {
    bool isMctComp = mctImage && c <= 2;
    uint8_t qmfbid = tcp->tccps_[c].qmfbid_;
    if(grk_get_data_type(false, img->comps[c].prec, isMctComp, qmfbid) != GRK_INT_16)
      return GRK_INT_32;
  }
  return GRK_INT_16;
}

bool mercuryFastPath(CodeStreamDecompress& cs)
{
  const char* mercuryEnv = std::getenv("GRK_MERCURY");
  if(mercuryEnv && std::strcmp(mercuryEnv, "0") == 0)
    return false;
  auto* headerTcp = cs.defaultTcp_.get();
  for(uint16_t c = 0; c < headerTcp->numComps_; ++c)
    if(headerTcp->tccps_[c].usesPart2Transform())
      return false;

  const std::string& path = cs.inputFilePath_;

  // Eligibility: full-image, all-components decode. Reduce, a quality layer
  // limit, and palette/ICC/channel-definition post-processing are handled;
  // everything else stays on the classic pipeline.
  auto& dec = cs.cp_.codingParams_.dec_;
  if(dec.skipAllocateComposite_)
    MFP_BAIL("skipAllocate set");
  // The transcoder drives a T2-only decode to record packet lengths for
  // marker injection — it needs the classic parse, not pixels.
  if(cs.cp_.recordPacketLengths_)
    MFP_BAIL("packet-length recording (transcode) requested");
  // Async decode expects grk_decompress() to SCHEDULE work that later
  // grk_decompress_wait()/swath calls drain via the classic pipeline's
  // TileCompletion signalling. The fast path runs synchronously and never
  // arms that machinery, so a subsequent swath wait would block forever.
  if(cs.cp_.asynchronous_)
    MFP_BAIL("asynchronous decode requested");
  if(!cs.cp_.compsToDecompress_.empty())
    MFP_BAIL("component filter set");
  auto img = cs.multiTileComposite_.get();
  if(!img || !img->comps || img->numcomps == 0)
    MFP_BAIL("no composite image");
  // A decode window needs no bail: grok resolved it into region_ (canvas
  // coordinates, clipped) at read-header time, the composite already carries
  // the reduced window bounds, and mercury is handed the window below so it
  // skips the tiles, precincts, and synthesis rows outside it.
  // palette/ICC/channel-definition need no bail: the full-composite path runs
  // postProcess (same as classic's postMultiTile), and the band-callback path
  // hands raw strips to the consumer callback exactly as classic does, which is
  // where that post-processing happens. the A/B sweep confirms both are
  // bit-exact against classic.
  for(uint16_t c = 0; c < img->numcomps; c++)
    if(img->comps[c].data) // already allocated by someone else — bail
      MFP_BAIL("component data pre-allocated");

  // ── Hand grok's parsed main header to mercury ────────────────────────────
  // Mercury no longer parses the main header (SIZ/COD/QCD/QCC); grok already
  // has it. Every pointer in MercuryMainHeader is borrowed only for the
  // mercury_warp_loom_fd call, so the backing std::vectors below just need to
  // outlive that call (they live to the end of this function).
  auto* tcp = cs.defaultTcp_.get();
  if(!tcp || !tcp->tccps_)
    MFP_BAIL("no default tile coding params");
  const uint16_t nc = img->numcomps;
  const auto& t0 = tcp->tccps_[0];

  // Components of one size share a row, and mercury's joint row callback
  // carries the color transform; components of different sizes need the
  // per-component entries, which cannot.
  bool anySubsampled = false;
  bool uniformSubsampling = true;
  for(uint16_t c = 0; c < nc; c++)
  {
    anySubsampled = anySubsampled || img->comps[c].dx != 1 || img->comps[c].dy != 1;
    uniformSubsampling = uniformSubsampling && img->comps[c].dx == img->comps[0].dx &&
                         img->comps[c].dy == img->comps[0].dy;
  }
  // grok applies the MCT when components 0-2 match, so a later component of a
  // different size leaves no entry point that can both split and transform
  if(tcp->mct_ == 1 && nc >= 3 && img->componentsEqual(3, false))
    for(uint16_t c = 3; c < nc; c++)
      if(img->comps[c].dx != img->comps[0].dx || img->comps[c].dy != img->comps[0].dy)
        MFP_BAIL("MCT with a subsampled component");

  // Every component reduces by the same count, so the one with the fewest
  // resolutions binds.
  uint8_t fewestRes = t0.numresolutions_;
  for(uint16_t c = 1; c < nc; c++)
    fewestRes = std::min(fewestRes, tcp->tccps_[c].numresolutions_);
  // mercury's synthesis chain needs at least one decomposition level, so it
  // cannot produce resolution 0. classic already checked reduce < numresolutions.
  if(fewestRes < 2 || dec.reduce_ > fewestRes - 2)
    MFP_BAIL("reduce leaves no decomposition level");

  // mercury picks one sample path for the whole image, so every component has
  // to use the same wavelet
  for(uint16_t c = 1; c < nc; c++)
    if(tcp->tccps_[c].qmfbid_ != t0.qmfbid_)
      MFP_BAIL("mixed wavelet kernels across components");
  // mercury reads packet headers in the packet stream, and applies no ROI upshift
  if(cs.cp_.ppmMarkers_)
    MFP_BAIL("PPM present");
  for(uint16_t c = 0; c < nc; c++)
    if(tcp->tccps_[c].roishift_ != 0)
      MFP_BAIL("RGN present");

  // classic rejects this in validateQuantization at tile init, too late for the fast path
  if(tcp->mainQcdQntsty != CCP_QNTSTY_SIQNT)
    for(uint16_t c = 0; c < nc; c++)
    {
      const auto& t = tcp->tccps_[c];
      if(!t.fromQCC_ && !t.fromTileHeader_ && tcp->mainQcdNumStepSizes < t.numStepSizesNeeded())
        MFP_BAIL("main QCD has fewer step sizes than the sub-bands it covers");
    }

  std::vector<MercurySizComp> mhComps(nc);
  for(uint16_t c = 0; c < nc; c++)
  {
    mhComps[c].precision = img->comps[c].prec;
    mhComps[c].is_signed = img->comps[c].sgnd != 0;
    mhComps[c].xr_siz = img->comps[c].dx;
    mhComps[c].yr_siz = img->comps[c].dy;
  }

  const uint8_t numres = t0.numresolutions_;
  std::vector<MercuryPrecinct> mhPrec;
  if(t0.csty_ & CCP_CSTY_PRECINCT)
  {
    mhPrec.resize(numres);
    for(uint8_t r = 0; r < numres; r++)
    {
      mhPrec[r].width = 1u << t0.precWidthExp_[r];
      mhPrec[r].height = 1u << t0.precHeightExp_[r];
    }
  }

  // Quantization: QCD from component 0, plus a per-component override for the
  // rest. Mercury applies each component's own quant; identical values decode
  // identically whether they originated in a QCD or a QCC marker.
  //
  // A component's band count follows its own resolution count (3*levels + 1);
  // use it rather than numStepSizes_, which grok's readQcd leaves at 0 for the
  // components it propagates the QCD to (it copies the stepsizes_ array but
  // not the count).
  auto fillQuant = [](const auto& t, std::vector<uint8_t>& ranges, std::vector<float>& steps,
                      MercuryQuant& q) {
    const uint32_t nbands = (uint32_t)(3 * (t.numresolutions_ - 1) + 1);
    q.guard_bits = t.numgbits_;
    // readQcd expands a derived QCD into one step per band, so mercury sees it expounded
    q.style = t.qntsty_ == CCP_QNTSTY_SIQNT ? CCP_QNTSTY_SEQNT : t.qntsty_;
    ranges.clear();
    steps.clear();
    if(t.qntsty_ == CCP_QNTSTY_NOQNT)
    {
      ranges.resize(nbands);
      for(uint32_t b = 0; b < nbands; b++)
        ranges[b] = t.stepsizes_[b].expn;
    }
    else
    {
      steps.resize(nbands);
      for(uint32_t b = 0; b < nbands; b++)
        steps[b] =
            (1.0f + (float)t.stepsizes_[b].mant / 2048.0f) / (float)(1u << t.stepsizes_[b].expn);
    }
    q.ranges = ranges.empty() ? nullptr : ranges.data();
    q.num_ranges = (uint32_t)ranges.size();
    q.steps = steps.empty() ? nullptr : steps.data();
    q.num_steps = (uint32_t)steps.size();
  };

  MercuryMainHeader mhdr;
  memset(&mhdr, 0, sizeof mhdr);
  std::vector<uint8_t> qcdRanges;
  std::vector<float> qcdSteps;
  fillQuant(t0, qcdRanges, qcdSteps, mhdr.qcd);

  std::vector<MercuryQccOverride> mhQcc(nc > 1 ? (size_t)(nc - 1) : 0);
  std::vector<std::vector<uint8_t>> qccRanges(mhQcc.size());
  std::vector<std::vector<float>> qccSteps(mhQcc.size());
  for(uint16_t c = 1; c < nc; c++)
  {
    auto& ov = mhQcc[c - 1];
    ov.comp = c;
    fillQuant(tcp->tccps_[c], qccRanges[c - 1], qccSteps[c - 1], ov.quant);
  }

  // Coding style: the COD fields below come from component 0, and a COC gives
  // each other component its own decomposition count, block size, block style,
  // wavelet, and precincts.
  std::vector<MercuryCocOverride> mhCoc(tcp->sawCoc_ && nc > 1 ? (size_t)(nc - 1) : 0);
  std::vector<std::vector<MercuryPrecinct>> cocPrec(mhCoc.size());
  for(size_t i = 0; i < mhCoc.size(); i++)
  {
    const auto& t = tcp->tccps_[i + 1];
    auto& ov = mhCoc[i];
    ov.comp = (uint32_t)(i + 1);
    ov.num_levels = (uint8_t)(t.numresolutions_ - 1);
    ov.block_width = 1u << t.cblkw_expn_;
    ov.block_height = 1u << t.cblkh_expn_;
    ov.modes = t.cblkStyle_;
    ov.reversible = (t.qmfbid_ == 1);
    if(t.csty_ & CCP_CSTY_PRECINCT)
    {
      cocPrec[i].resize(t.numresolutions_);
      for(uint8_t r = 0; r < t.numresolutions_; r++)
      {
        cocPrec[i][r].width = 1u << t.precWidthExp_[r];
        cocPrec[i][r].height = 1u << t.precHeightExp_[r];
      }
    }
    ov.precincts = cocPrec[i].empty() ? nullptr : cocPrec[i].data();
    ov.num_precincts = (uint32_t)cocPrec[i].size();
  }

  // Main-header POC volume list; mercury clamps and rejects it the way
  // finalizePocs does, so hand it over raw.
  std::vector<MercuryProgressionVolume> mhPoc;
  if(tcp->hasPoc())
  {
    mhPoc.resize(tcp->getNumProgressions());
    for(size_t i = 0; i < mhPoc.size(); i++)
    {
      const auto& prog = tcp->progressionOrderChange_[i];
      auto& vol = mhPoc[i];
      vol.res_s = prog.res_s;
      vol.comp_s = prog.comp_s;
      vol.lay_e = prog.lay_e;
      vol.res_e = prog.res_e;
      vol.comp_e = prog.comp_e;
      vol.order = (uint8_t)prog.progression;
    }
  }

  // SIZ canvas from the header image: under a decode window the composite's
  // bounds are the window rect, not the image, and tile geometry needs the
  // real canvas.
  mhdr.x_siz = cs.headerImage_->x1;
  mhdr.y_siz = cs.headerImage_->y1;
  mhdr.x_o_siz = cs.headerImage_->x0;
  mhdr.y_o_siz = cs.headerImage_->y0;
  mhdr.xt_siz = cs.cp_.t_width_;
  mhdr.yt_siz = cs.cp_.t_height_;
  mhdr.xt_o_siz = cs.cp_.tx0_;
  mhdr.yt_o_siz = cs.cp_.ty0_;
  mhdr.comps = mhComps.data();
  mhdr.num_comps = nc;
  mhdr.order = (uint8_t)tcp->prg_;
  mhdr.num_layers = tcp->numLayers_;
  mhdr.use_ycc = (tcp->mct_ & 1) != 0;
  mhdr.num_levels = (uint8_t)(numres - 1);
  mhdr.block_width = 1u << t0.cblkw_expn_;
  mhdr.block_height = 1u << t0.cblkh_expn_;
  mhdr.modes = t0.cblkStyle_;
  mhdr.reversible = (t0.qmfbid_ == 1);
  mhdr.use_sop = (tcp->csty_ & CP_CSTY_SOP) != 0;
  mhdr.use_eph = (tcp->csty_ & CP_CSTY_EPH) != 0;
  mhdr.precincts = mhPrec.empty() ? nullptr : mhPrec.data();
  mhdr.num_precincts = (uint32_t)mhPrec.size();
  mhdr.qcc = mhQcc.empty() ? nullptr : mhQcc.data();
  mhdr.num_qcc = (uint32_t)mhQcc.size();
  mhdr.coc = mhCoc.empty() ? nullptr : mhCoc.data();
  mhdr.num_coc = (uint32_t)mhCoc.size();
  mhdr.poc = mhPoc.empty() ? nullptr : mhPoc.data();
  mhdr.num_poc = (uint32_t)mhPoc.size();
  // zero length: a raw code stream, which runs to the end of the file
  mhdr.codestream_off = cs.codestreamOffset_;
  mhdr.codestream_len = cs.codestreamLength_;
  mhdr.first_sot_off = cs.markerCache_->getTileStreamStart();

  // TLM tile-part table: with it, mercury reads only the tile-part headers of
  // the tiles it decodes instead of scanning the SOT chain. hasTLM() is false
  // when the app disabled TLM (GRK_RANDOM_ACCESS_TLM invalidates the marker).
  std::vector<MercuryTlmEntry> mhTlm;
  if(cs.cp_.hasTLM())
  {
    const auto& tileParts = cs.cp_.tlmMarkers_->getTileParts();
    for(uint32_t t = 0; t < tileParts.size(); t++)
    {
      if(!tileParts[t])
        continue;
      for(const auto& part : *tileParts[t])
        mhTlm.push_back({part->offset_, (uint32_t)part->length_, (uint16_t)t});
    }
  }
  mhdr.tlm = mhTlm.empty() ? nullptr : mhTlm.data();
  mhdr.num_tlm = (uint32_t)mhTlm.size();

  MercuryDecodeParams mparams;
  memset(&mparams, 0, sizeof mparams);
  mparams.reduce = dec.reduce_;
  // same contract as classic's T2 packet skip: 0 decodes every layer, and a
  // limit at or above the stream's layer count is a full decode
  mparams.max_layers = dec.layersToDecompress_;
  // classic ignores PLT when a PLM is present or the app disabled it
  mparams.disable_plt =
      cs.cp_.plmMarkers_ != nullptr || (dec.disableRandomAccessFlags_ & GRK_RANDOM_ACCESS_PLT) != 0;
  if(!cs.region_.empty())
  {
    mparams.win_x0 = cs.region_.x0;
    mparams.win_y0 = cs.region_.y0;
    mparams.win_x1 = cs.region_.x1;
    mparams.win_y1 = cs.region_.y1;
  }

  uint8_t err[256] = {0};
  MercuryPlan* plan = nullptr;
  // these back the read_at source and must outlive the decode below, so they
  // live at function scope. only one is used per call.
  MemReader memReader;
  std::vector<uint8_t> slurped;
#if defined(_WIN32)
  Win32PositionedReader win32reader;
#endif
  if(!path.empty())
  {
#if defined(_WIN32)
    // POSIX fds don't reach mercury's fd entry, drive it through the read_at
    // callback
    if(!win32reader.open(path))
      MFP_BAIL("open failed");
    plan = mercury_warp_loom(&mhdr, &mparams, win32ReadAt, &win32reader, win32reader.size(), err,
                             sizeof err);
#else
    int fd = open(path.c_str(), O_RDONLY);
    if(fd < 0)
      MFP_BAIL("open failed");
    plan = mercury_warp_loom_fd(&mhdr, &mparams, fd, err, sizeof err);
    close(fd);
#endif
  }
  else
  {
    // no file path: mercury needs random access, so serve buffer or callback
    // input from one contiguous span, and restore the stream cursor before
    // returning so a classic fallback resumes where header parsing left it
    IStream* stream = cs.getStream();
    if(!stream)
      MFP_BAIL("no input stream");
    // a failed media seek marks the stream at end, which would break the
    // classic fallback, so refuse unseekable streams before touching it
    if(!stream->hasSeek())
      MFP_BAIL("input stream not seekable");
    uint64_t savedPos = stream->tell();
    if(!stream->seek(0))
      MFP_BAIL("input stream seek to start failed");
    uint64_t len = stream->numBytesLeft();
    bool ok = len > 0;
    const char* streamFail = "input stream unreadable";
    if(ok && stream->supportsZeroCopy())
    {
      // read_at aliases the app's buffer at offset 0, no extra memory.
      memReader.base = stream->currPtr();
      ok = memReader.base != nullptr;
    }
    else if(ok)
    {
      // slurp once so read_at has lock-free random access. len is
      // app-declared and untrusted, so an oversized value falls back
      try
      {
        slurped.resize(len);
      }
      catch(const std::exception&)
      {
        ok = false;
        streamFail = "declared stream length too large";
      }
      if(ok)
      {
        ok = stream->read(slurped.data(), nullptr, len) == len;
        memReader.base = slurped.data();
      }
    }
    if(!stream->seek(savedPos))
      grklog.warn("mercury fast path: failed to restore stream position %llu",
                  (unsigned long long)savedPos);
    if(!ok)
      MFP_BAIL(streamFail);
    memReader.size = len;
    plan = mercury_warp_loom(&mhdr, &mparams, memReadAt, &memReader, len, err, sizeof err);
  }
  if(!plan)
  {
    grklog.info("mercury fast path: plan rejected (%s), using classic pipeline", err);
    char bailMsg[300];
    snprintf(bailMsg, sizeof bailMsg, "plan rejected: %s", (const char*)err);
    MFP_BAIL(bailMsg);
  }

  // The plan must describe the same image grok's headers do. The composite's
  // components already carry the reduced dimensions (SIZMarker reduces
  // headerImage_, which the composite is copied from), so this also confirms
  // mercury reduced the canvas the same way grok did.
  MercuryImageInfo info;
  if(mercury_loom_info(plan, &info) != MERCURY_OK || info.num_comps != img->numcomps)
  {
    mercury_unwarp_loom(plan);
    MFP_BAIL("plan image info disagrees with the headers");
  }
  for(uint16_t c = 0; c < img->numcomps; c++)
  {
    uint32_t prec = 0;
    int32_t sgnd = 0;
    uint32_t compWidth = 0;
    uint32_t compHeight = 0;
    if(mercury_loom_comp_info(plan, c, &prec, &sgnd, &compWidth, &compHeight) != MERCURY_OK ||
       prec != img->comps[c].prec || (sgnd != 0) != img->comps[c].sgnd ||
       img->comps[c].w != compWidth || img->comps[c].h != compHeight)
    {
      mercury_unwarp_loom(plan);
      MFP_BAIL("plan component info disagrees with the headers");
    }
  }

  const auto mercuryThreads = static_cast<uint32_t>(TFSingleton::num_threads());

  // Streaming band mode: the app registered an incremental band writer
  // (header already on disk), so decode into a sliding rows_per_strip
  // window instead of a full composite. After the first flushed band the
  // output is partially written, so a decode failure is terminal — no
  // classic fallback.
  if(cs.ioBandCallback_)
  {
    if(img->decompress_num_comps != img->numcomps)
    {
      mercury_unwarp_loom(plan);
      MFP_BAIL("decompress_num_comps != numcomps");
    }
    // Match grok's whole-image sample type (see composite branch below).
    grk_data_type outType = mercuryOutType(cs.defaultTcp_.get(), img);
    bool is16 = outType == GRK_INT_16;
    auto scratch = std::unique_ptr<GrkImage, RefCountedDeleter<GrkImage>>(
        new GrkImage(), RefCountedDeleter<GrkImage>());
    img->copyHeaderTo(scratch.get());
    // A subsampled component makes the components' heights differ, which a
    // rows_per_strip window cannot hold, so mirror classic: one band per tile
    // row, each component carrying its own y0 and height.
    if(anySubsampled)
    {
      uint8_t reduce = dec.reduce_;
      std::vector<TileRowBand> bands;
      for(uint32_t ty = 0; ty < cs.cp_.t_grid_height_; ty++)
      {
        uint64_t rowY0 = (uint64_t)cs.cp_.ty0_ + (uint64_t)ty * cs.cp_.t_height_;
        uint32_t y0 = (uint32_t)std::max<uint64_t>(rowY0, img->y0);
        uint32_t y1 = (uint32_t)std::min<uint64_t>(rowY0 + cs.cp_.t_height_, img->y1);
        if(y0 >= y1)
          continue;
        TileRowBand band;
        band.yEnd = ceildivpow2<uint32_t>(y1, reduce) - ceildivpow2<uint32_t>(y0, reduce);
        for(uint16_t c = 0; c < scratch->numcomps; c++)
        {
          uint32_t dy = scratch->comps[c].dy;
          uint32_t compY0 = ceildivpow2<uint32_t>(ceildiv<uint32_t>(y0, dy), reduce);
          uint32_t compY1 = ceildivpow2<uint32_t>(ceildiv<uint32_t>(y1, dy), reduce);
          band.y0.push_back(compY0);
          band.h.push_back(compY1 - compY0);
        }
        bands.push_back(std::move(band));
      }
      for(uint16_t c = 0; c < scratch->numcomps; c++)
      {
        uint32_t total = 0;
        uint32_t tallest = 0;
        for(const auto& band : bands)
        {
          total += band.h[c];
          tallest = std::max(tallest, band.h[c]);
        }
        // the bands must cover the component's plane exactly, or a row would
        // land in the wrong band
        if(total != img->comps[c].h)
        {
          mercury_unwarp_loom(plan);
          MFP_BAIL("tile rows do not cover the component plane");
        }
        auto comp = scratch->comps + c;
        comp->data_type = outType;
        // a non-tile-aligned YTOsiz can make an interior row the tallest
        comp->h = tallest;
        if(!GrkImage::allocData(comp))
        {
          mercury_unwarp_loom(plan);
          MFP_BAIL("scratch band buffer alloc failed");
        }
      }
      TileRowBandCtx ctx;
      ctx.cb = cs.ioBandCallback_;
      ctx.cbUserData = cs.ioBandUserData_;
      ctx.scratch = scratch.get();
      ctx.bands = std::move(bands);
      ctx.filled.assign(scratch->numcomps, 0);
      for(uint16_t c = 0; c < scratch->numcomps; c++)
      {
        ctx.planes.push_back(scratch->comps[c].data);
        ctx.strides.push_back(scratch->comps[c].stride);
      }
      int32_t rc;
      if(uniformSubsampling)
        rc = is16 ? mercury_weave_i16(plan, mercury_grok_t1_decode, tileRowBandSink<int16_t>, &ctx,
                                      mercuryThreads)
                  : mercury_weave(plan, mercury_grok_t1_decode, tileRowBandSink<int32_t>, &ctx,
                                  mercuryThreads);
      else
        rc = is16 ? mercury_weave_comps_i16(plan, mercury_grok_t1_decode,
                                            tileRowBandCompSink<int16_t>, &ctx, mercuryThreads)
                  : mercury_weave_comps(plan, mercury_grok_t1_decode, tileRowBandCompSink<int32_t>,
                                        &ctx, mercuryThreads);
      // trailing bands a decode window emptied carry no rows to trigger them
      if(rc == MERCURY_OK && !ctx.cbFailed)
        flushCompleteBands(&ctx);
      if(rc != MERCURY_OK || ctx.cbFailed || ctx.current != ctx.bands.size())
      {
        if(ctx.bandsFlushed == 0 && !ctx.cbFailed)
          MFP_BAIL("decode failed before first band; falling back");
        grklog.error("mercury fast path: streaming decode failed (rc=%d) after %u bands written",
                     rc, ctx.bandsFlushed);
        cs.success_ = false;
        return true;
      }
      grklog.info("mercury fast path: streamed %ux%u x%u in %u tile-row bands", info.width,
                  info.height, info.num_comps, (uint32_t)ctx.bands.size());
      cs.success_ = true;
      return true;
    }
    uint32_t bandRows =
        scratch->rows_per_strip ? std::min(scratch->rows_per_strip, info.height) : info.height;
    for(uint16_t c = 0; c < scratch->numcomps; c++)
    {
      auto comp = scratch->comps + c;
      comp->data_type = outType;
      comp->h = bandRows;
      // allocCompositeData no-ops on single-tile images; allocate directly
      if(!GrkImage::allocData(comp))
      {
        mercury_unwarp_loom(plan);
        MFP_BAIL("scratch band buffer alloc failed");
      }
    }
    BandCtx ctx;
    ctx.cb = cs.ioBandCallback_;
    ctx.cbUserData = cs.ioBandUserData_;
    ctx.scratch = scratch.get();
    ctx.bandRows = bandRows;
    ctx.height = info.height;
    for(uint16_t c = 0; c < scratch->numcomps; c++)
    {
      ctx.planes.push_back(scratch->comps[c].data);
      ctx.strides.push_back(scratch->comps[c].stride);
    }
    int32_t rc =
        is16 ? mercury_weave_i16(plan, mercury_grok_t1_decode, bandSink<int16_t>, &ctx,
                                 mercuryThreads)
             : mercury_weave(plan, mercury_grok_t1_decode, bandSink<int32_t>, &ctx, mercuryThreads);
    if(rc != MERCURY_OK || ctx.cbFailed)
    {
      if(ctx.bandsFlushed == 0 && !ctx.cbFailed)
        MFP_BAIL("decode failed before first band; falling back");
      grklog.error("mercury fast path: streaming decode failed (rc=%d) after %u bands written", rc,
                   ctx.bandsFlushed);
      cs.success_ = false;
      return true;
    }
    grklog.info("mercury fast path: streamed %ux%u x%u in %u-row bands", info.width, info.height,
                info.num_comps, bandRows);
    cs.success_ = true;
    return true;
  }

  // Match grok's whole-image sample type so the API-visible comp->data_type
  // (and thus writers / grk_image consumers) is identical to the classic path.
  grk_data_type outType = mercuryOutType(cs.defaultTcp_.get(), img);
  bool is16 = outType == GRK_INT_16;

  // Allocate planes (int16 or int32 per outType) and stream rows into them.
  std::vector<void*> planes(img->numcomps);
  std::vector<uint32_t> strides(img->numcomps);
  for(uint16_t c = 0; c < img->numcomps; c++)
  {
    auto comp = img->comps + c;
    comp->data_type = outType;
    if(!GrkImage::allocData(comp))
    {
      mercury_unwarp_loom(plan);
      return false;
    }
    planes[c] = comp->data;
    strides[c] = comp->stride;
  }
  RowCtx ctx{planes.data(), strides.data()};
  int32_t rc;
  if(uniformSubsampling)
    rc = is16 ? mercury_weave_i16(plan, mercury_grok_t1_decode, rowSink<int16_t>, &ctx,
                                  mercuryThreads)
              : mercury_weave(plan, mercury_grok_t1_decode, rowSink<int32_t>, &ctx, mercuryThreads);
  else
    rc = is16 ? mercury_weave_comps_i16(plan, mercury_grok_t1_decode, compRowSink<int16_t>, &ctx,
                                        mercuryThreads)
              : mercury_weave_comps(plan, mercury_grok_t1_decode, compRowSink<int32_t>, &ctx,
                                    mercuryThreads);
  // Classic runs postProcess on the composite after tile transfer
  // (postMultiTile) — precision/rescale, sycc/esycc->RGB, grey->RGB,
  // upsample, ICC. Mercury rows are the same final absolute samples the
  // composite holds at that point, so the same call applies here.
  if(rc != MERCURY_OK || !cs.postProcess(img))
  {
    if(rc != MERCURY_OK)
      grklog.warn("mercury fast path: decode failed (rc=%d) after plan accepted", rc);
    else
      grklog.warn("mercury fast path: postProcess failed, falling back to classic");
    for(uint16_t c = 0; c < img->numcomps; c++)
    {
      auto comp = img->comps + c;
      if(comp->owns_data && comp->data)
        grk_aligned_free(comp->data);
      comp->data = nullptr;
      comp->owns_data = false;
    }
    return false;
  }
  grklog.info("mercury fast path: decoded %ux%u x%u", info.width, info.height, info.num_comps);
  cs.success_ = true;
  return true;
}

} // namespace grk

#else // GRK_MERCURY_BUILD: fast path unavailable (no cargo at configure time)

namespace grk
{
bool mercuryFastPath(CodeStreamDecompress&)
{
  return false;
}
} // namespace grk

#endif // GRK_MERCURY_BUILD
