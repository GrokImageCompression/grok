/*
 * LOCAL-ONLY (never pushed): extern "C" adapter exposing grok's part-1 T1
 * block decoder to mercury's bench_t1 A/B harness, matching mercury's
 * stripe_decoder::BlockCoder contract (see mercury src/decode/stripe_decoder.rs):
 * output is row-major sign-magnitude i32, bit 31 = sign, magnitude
 * left-aligned so the reversible sample is mag >> (31 - k_max_prime).
 *
 * grok's BlockCoder decodes two's-complement values at x2 scale (the
 * implicit T1_NMSEDEC fractional bit; its ShiftFilter divides by 2), with
 * the block's MSB plane at bit numbps. Conversion is therefore
 * mag = |v| << (30 - k_max_prime), which also carries the half-bit
 * reconstruction into the fractional positions mercury's f32 path reads.
 */

#include <cstdint>
#include <cstring>
#include <algorithm>

#include "simd.h"
#include "t1_common.h"
#include "BlockCoder.h"
#include "CodeblockDecompress.h"

extern "C" __attribute__((visibility("default"))) int32_t mercury_grok_t1_decode(
    const uint8_t* coded_data, int32_t coded_length, int32_t num_passes, int32_t missing_msbs,
    int32_t k_max_prime, int32_t orientation, int32_t modes, int32_t num_cols, int32_t num_rows,
    const int32_t* segment_lengths, int32_t num_segments, int32_t* out)
{
  using namespace grk;
  using namespace grk::t1;

  // Per-thread coder reuse = CoderPool::getCoder(worker) semantics.
  thread_local BlockCoder* coder = nullptr;
  if(!coder)
    coder = new BlockCoder(false /*isCompressor*/, 64, 64, 0 /*cacheStrategy*/);

  int32_t numbps = k_max_prime - missing_msbs;
  if(numbps <= 0 || num_cols <= 0 || num_rows <= 0)
    return 0;

  static const bool dbg = getenv("MERCURY_SHIM_DEBUG") != nullptr;
  CodeblockDecompress cblk(1 /*numLayers*/);
  cblk.setRect(Rect32_16(0, 0, (uint16_t)num_cols, (uint16_t)num_rows));
  if(!cblk.getImpl()->directInit((uint8_t)modes, (uint8_t)numbps, num_passes,
                                 const_cast<uint8_t*>(coded_data), coded_length, segment_lengths,
                                 num_segments))
  {
    if(dbg)
      fprintf(stderr,
              "shim: directInit failed: numbps=%d passes=%d segs=%d len=%d modes=%#x\n",
              numbps, num_passes, num_segments, coded_length, modes);
    return 0;
  }

  coder->setFinalLayer(true);
  if(!coder->decompress_cblk(&cblk, (uint8_t)orientation, (uint32_t)modes))
  {
    if(dbg)
      fprintf(stderr,
              "shim: decompress_cblk failed: numbps=%d passes=%d segs=%d len=%d modes=%#x\n",
              numbps, num_passes, num_segments, coded_length, modes);
    return 0;
  }

  const int32_t* src = coder->getUncompressedData();
  const int32_t shift = 30 - k_max_prime;
  const size_t n = (size_t)num_cols * (size_t)num_rows;
  if(shift >= 0)
  {
    for(size_t i = 0; i < n; ++i)
    {
      int32_t v = src[i];
      uint32_t mag = (uint32_t)(v < 0 ? -v : v) << shift;
      out[i] = (int32_t)((v < 0 ? 0x80000000u : 0u) | mag);
    }
  }
  else
  {
    for(size_t i = 0; i < n; ++i)
    {
      int32_t v = src[i];
      uint32_t mag = (uint32_t)(v < 0 ? -v : v) >> -shift;
      out[i] = (int32_t)((v < 0 ? 0x80000000u : 0u) | mag);
    }
  }
  return 1;
}
