/*
 * LOCAL-ONLY (never pushed): mercury streaming full-image fast path.
 * See mercury_fastpath.h. Decodes eligible streams through mercury's
 * C API using grok's own part-1 block coder (mercury_grok_t1_decode
 * shim), filling multiTileComposite_'s planes directly.
 */

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
#include <unistd.h>

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
#include "SchedulerExcalibur.h"
#include "TileProcessor.h"
#include "TileCache.h"
#include "TileCompletion.h"
#include "CodeStreamDecompress.h"

#include "lanes.h"

#include "mercury_fastpath.h"
#include "mercury_capi.h"

/* grok's part-1 block coder behind mercury's BlockCoder contract —
 * compiled into libgrokj2k on this branch (t1/part1/mercury_t1_shim.cpp). */
extern "C" int32_t mercury_grok_t1_decode(const uint8_t*, int32_t, int32_t, int32_t, int32_t,
                                          int32_t, int32_t, int32_t, int32_t, const int32_t*,
                                          int32_t, int32_t*);

namespace grk
{

static std::mutex g_mercuryFileMutex;
static std::string g_mercuryFile;

void mercurySetInputFile(const char* path)
{
  std::lock_guard<std::mutex> lock(g_mercuryFileMutex);
  g_mercuryFile = path ? path : "";
}

namespace
{
// Mercury always emits full-width absolute samples as int32. When the output
// image type is INT_16 (grk_get_data_type says the component is int16-eligible
// and every component agrees — see mercuryOutType) the sink narrows each sample
// to int16 on the way into the plane; the values are known to fit (mercury's
// f32 9/7 and exact 5/3 emit the same absolute samples grok's int16 path does).
static inline void writeRow(void* plane, bool is16, size_t off, const int32_t* src, uint64_t width)
{
  if(is16)
  {
    auto dst = (int16_t*)plane + off;
    for(uint64_t x = 0; x < width; x++)
      dst[x] = (int16_t)src[x];
  }
  else
  {
    memcpy((int32_t*)plane + off, src, width * sizeof(int32_t));
  }
}

struct RowCtx
{
  void** planes;
  uint32_t* strides;
  bool is16;
};

void rowSink(void* c, uint32_t row, const int32_t* const* comps, uint32_t numComps, uint64_t width)
{
  auto ctx = (RowCtx*)c;
  for(uint32_t i = 0; i < numComps; i++)
    writeRow(ctx->planes[i], ctx->is16, (size_t)row * ctx->strides[i], comps[i], width);
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
  bool is16;
  uint32_t bandRows; // scratch window height
  uint32_t height; // full image height
  uint32_t filled = 0;
  uint32_t bandsFlushed = 0;
  bool cbFailed = false;
};

void bandSink(void* c, uint32_t row, const int32_t* const* comps, uint32_t numComps, uint64_t width)
{
  auto ctx = (BandCtx*)c;
  if(ctx->cbFailed)
    return;
  for(uint32_t i = 0; i < numComps; i++)
    writeRow(ctx->planes[i], ctx->is16, (size_t)ctx->filled * ctx->strides[i], comps[i], width);
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

#define MFP_BAIL(msg)                                                    \
  do                                                                     \
  {                                                                      \
    if(std::getenv("GRK_MERCURY_DEBUG"))                                 \
      fprintf(stderr, "mercury fastpath bail: %s\n", msg);              \
    return false;                                                        \
  } while(0)

// Whole-image output sample type, replicating grok's classic decision so the
// fast path's API-visible comp->data_type matches (test_8bit_mct_produces_int16
// and friends compare it). grk_get_data_type is the BIBO-headroom rule
// createDecompressTileComponentWindows feeds into the final image type — it
// handles 9/7 int16 at prec<=9, unlike activateScratch's reversible-only logic.
// The 16-bit path is whole-tile only, which the fast path always is (it bails on
// any decode window/region). Per grok's uniform-type rule, one under-headroom
// component forces every component to int32.
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

// Allocate an output plane with the SAME row stride the classic pipeline
// produces. Classic hands the tile-window buffer straight to the image
// (transferWindowData), and that buffer rounds its width up to grk::NumLanes()
// elements (buffer.h alignedBufferWidth), independent of sample type.
// GrkImage::allocData instead uses a per-type 64-byte alignment — 32 elements
// for int16 — so for a narrow int16 image the two strides diverge (w=16:
// classic 16, allocData 32). A consumer that walks comp->data assuming rows are
// alignedBufferWidth-strided (grok's own GrkJP2MetadataTest, and any flat
// reader) then reads into the padding. Match classic for int16; int32 keeps
// allocData (it already agrees with classic on every tested width, and the
// image writers honour stride regardless of these bytes).
static bool allocFastPathPlane(grk_image_comp* comp)
{
  if(comp->data_type != GRK_INT_16)
    return GrkImage::allocData(comp);
  uint32_t lanes = grok::NumLanes();
  uint32_t stride = (uint32_t)((((uint64_t)comp->w + lanes - 1) / lanes) * lanes);
  void* data = grk_aligned_malloc((uint64_t)stride * comp->h * sizeof(int16_t));
  if(!data)
    return false;
  comp->data = data;
  comp->stride = stride;
  comp->owns_data = true;
  return true;
}

bool mercuryFastPath(CodeStreamDecompress& cs)
{
  if(!std::getenv("GRK_MERCURY"))
    return false;

  std::string path;
  {
    std::lock_guard<std::mutex> lock(g_mercuryFileMutex);
    path = g_mercuryFile;
  }
  if(path.empty())
    MFP_BAIL("no input file recorded");

  // Eligibility: full-resolution, full-image, all-layers, all-components
  // decode of a stream with no palette/ICC/channel-definition
  // post-processing. Everything else stays on the classic pipeline.
  auto& dec = cs.cp_.codingParams_.dec_;
  if(dec.reduce_ != 0 || dec.layersToDecompress_ != 0 || dec.skipAllocateComposite_)
    MFP_BAIL("reduce/layers/skipAllocate set");
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
  if(cs.cp_.dw_x1 > cs.cp_.dw_x0 || cs.cp_.dw_y1 > cs.cp_.dw_y0)
    MFP_BAIL("decode window set");
  if(!cs.cp_.compsToDecompress_.empty())
    MFP_BAIL("component filter set");
  auto img = cs.multiTileComposite_.get();
  if(!img || !img->comps || img->numcomps == 0)
    MFP_BAIL("no composite image");
  if(img->meta &&
     (img->meta->color.palette || img->meta->color.icc_profile_buf ||
      img->meta->color.channel_definition))
    MFP_BAIL("palette/ICC/cdef present");
  for(uint16_t c = 0; c < img->numcomps; c++)
  {
    if(img->comps[c].dx != 1 || img->comps[c].dy != 1)
      MFP_BAIL("subsampled component");
    if(img->comps[c].data) // already allocated by someone else — bail
      MFP_BAIL("component data pre-allocated");
  }

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

  // COC (per-component coding style) and POC are not streamable by mercury —
  // it derives a single packet schedule from one progression and one coding
  // style. (These were mercury's own rejections before; now grok owns them.)
  // Bail on COC *presence*, not just on differing values: a redundant COC
  // (identical to the COD) can still land on a mercury decode path the classic
  // pipeline handles but the fast path does not, so match the old contract and
  // fall back whenever any COC was read.
  if(tcp->hasPoc())
    MFP_BAIL("POC present");
  if(tcp->sawCoc_)
    MFP_BAIL("COC present");

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
  // Band count is uniform (3*num_levels + 1); use it rather than a component's
  // numStepSizes_, which grok's readQcd leaves at 0 for the components it
  // propagates the QCD to (it copies the stepsizes_ array but not the count).
  const uint32_t nbands = (uint32_t)(3 * (numres - 1) + 1);
  auto fillQuant = [nbands](const auto& t, std::vector<uint8_t>& ranges,
                            std::vector<float>& steps, MercuryQuant& q) {
    q.guard_bits = t.numgbits_;
    q.style = t.qntsty_; // 0 = reversible, 1 = derived, 2 = expounded
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
        steps[b] = (1.0f + (float)t.stepsizes_[b].mant / 2048.0f) /
                   (float)(1u << t.stepsizes_[b].expn);
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

  mhdr.x_siz = img->x1;
  mhdr.y_siz = img->y1;
  mhdr.x_o_siz = img->x0;
  mhdr.y_o_siz = img->y0;
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
  mhdr.codestream_off = 0; // informational; mercury drives off first_sot_off
  mhdr.first_sot_off = cs.markerCache_->getTileStreamStart();

  uint8_t err[256] = {0};
#if defined(_WIN32)
  // POSIX fds don't reach mercury's fd entry; drive it through the read_at
  // callback. The reader must outlive the decode below, so it lives here.
  Win32PositionedReader reader;
  if(!reader.open(path))
    MFP_BAIL("open failed");
  MercuryPlan* plan =
      mercury_warp_loom(&mhdr, win32ReadAt, &reader, reader.size(), err, sizeof err);
#else
  int fd = open(path.c_str(), O_RDONLY);
  if(fd < 0)
    MFP_BAIL("open failed");
  MercuryPlan* plan = mercury_warp_loom_fd(&mhdr, fd, err, sizeof err);
  close(fd);
#endif
  if(!plan)
  {
    grklog.info("mercury fast path: plan rejected (%s), using classic pipeline", err);
    return false;
  }

  // The plan must describe the same image grok's headers do.
  MercuryImageInfo info;
  if(mercury_loom_info(plan, &info) != MERCURY_OK || info.num_comps != img->numcomps)
  {
    mercury_unwarp_loom(plan);
    return false;
  }
  for(uint16_t c = 0; c < img->numcomps; c++)
  {
    uint32_t prec = 0;
    int32_t sgnd = 0;
    if(mercury_loom_comp_info(plan, c, &prec, &sgnd) != MERCURY_OK ||
       prec != img->comps[c].prec || (sgnd != 0) != img->comps[c].sgnd ||
       img->comps[c].w != info.width || img->comps[c].h != info.height)
    {
      mercury_unwarp_loom(plan);
      return false;
    }
  }

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
    uint32_t bandRows = scratch->rows_per_strip ? std::min(scratch->rows_per_strip, info.height)
                                                : info.height;
    for(uint16_t c = 0; c < scratch->numcomps; c++)
    {
      auto comp = scratch->comps + c;
      comp->data_type = outType;
      comp->h = bandRows;
      // allocCompositeData no-ops on single-tile images; allocate directly
      // (classic stride for int16 — see allocFastPathPlane)
      if(!allocFastPathPlane(comp))
      {
        mercury_unwarp_loom(plan);
        MFP_BAIL("scratch band buffer alloc failed");
      }
    }
    BandCtx ctx;
    ctx.cb = cs.ioBandCallback_;
    ctx.cbUserData = cs.ioBandUserData_;
    ctx.scratch = scratch.get();
    ctx.is16 = is16;
    ctx.bandRows = bandRows;
    ctx.height = info.height;
    for(uint16_t c = 0; c < scratch->numcomps; c++)
    {
      ctx.planes.push_back(scratch->comps[c].data);
      ctx.strides.push_back(scratch->comps[c].stride);
    }
    int32_t rc = mercury_weave(plan, mercury_grok_t1_decode, bandSink, &ctx, 0);
    if(rc != MERCURY_OK || ctx.cbFailed)
    {
      if(ctx.bandsFlushed == 0 && !ctx.cbFailed)
        MFP_BAIL("decode failed before first band; falling back");
      grklog.error("mercury fast path: streaming decode failed (rc=%d) after %u bands written",
                   rc, ctx.bandsFlushed);
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
    if(!allocFastPathPlane(comp))
    {
      mercury_unwarp_loom(plan);
      return false;
    }
    planes[c] = comp->data;
    strides[c] = comp->stride;
  }
  RowCtx ctx{planes.data(), strides.data(), is16};
  int32_t rc = mercury_weave(plan, mercury_grok_t1_decode, rowSink, &ctx, 0);
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
