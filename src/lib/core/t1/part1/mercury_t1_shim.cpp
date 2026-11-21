/*
 * extern "C" adapter exposing grok's part-1 T1 block decoder to mercury,
 * matching mercury's stripe_decoder::BlockCoder contract (see mercury
 * src/decode/stripe_decoder.rs).
 *
 * grok's BlockCoder decodes two's-complement values at x2 scale (the
 * implicit T1_NMSEDEC fractional bit, classic's ShiftFilter divides by 2),
 * which is the contract's format, so the coder's buffer is returned as is.
 */

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <memory>

#include "simd.h"
#include "t1_common.h"
#include "BlockCoder.h"
#include "CodeblockDecompress.h"

extern "C" const int32_t* mercury_grok_t1_decode(const uint8_t* coded_data, int32_t coded_length,
                                                 int32_t num_passes, int32_t missing_msbs,
                                                 int32_t k_max_prime, int32_t orientation,
                                                 int32_t modes, int32_t num_cols, int32_t num_rows,
                                                 const int32_t* segment_lengths,
                                                 int32_t num_segments)
{
  using namespace grk;
  using namespace grk::t1;

  // Per-thread coder reuse = CoderPool::getCoder(worker) semantics.
  // owning pointer so the coder and its buffers go away when the worker thread does
  thread_local std::unique_ptr<BlockCoder> coder;
  if(!coder)
    coder = std::make_unique<BlockCoder>(false /*isCompressor*/, 64, 64, 0 /*cacheStrategy*/);

  int32_t numbps = k_max_prime - missing_msbs;
  if(numbps <= 0 || num_cols <= 0 || num_rows <= 0)
    return nullptr;

  static const bool dbg = getenv("MERCURY_SHIM_DEBUG") != nullptr;
  CodeblockDecompress cblk(1 /*numLayers*/);
  cblk.setRect(Rect32_16(0, 0, (uint16_t)num_cols, (uint16_t)num_rows));
  if(!cblk.getImpl()->directInit((uint8_t)modes, (uint8_t)numbps, num_passes,
                                 const_cast<uint8_t*>(coded_data), coded_length, segment_lengths,
                                 num_segments))
  {
    if(dbg)
      fprintf(stderr, "shim: directInit failed: numbps=%d passes=%d segs=%d len=%d modes=%#x\n",
              numbps, num_passes, num_segments, coded_length, modes);
    return nullptr;
  }

  coder->setFinalLayer(true);
  if(!coder->decompress_cblk(&cblk, (uint8_t)orientation, (uint32_t)modes))
  {
    if(dbg)
      fprintf(stderr,
              "shim: decompress_cblk failed: numbps=%d passes=%d segs=%d len=%d modes=%#x\n",
              numbps, num_passes, num_segments, coded_length, modes);
    return nullptr;
  }

  return coder->getUncompressedData();
}
