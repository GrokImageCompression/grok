/* mercury_dwt_bridge.cpp — extern "C" bridge from the Mercury C API to the
 * Highway SIMD DWT kernels (mercury_native::hwy_*).
 *
 * MercuryLiftingStep is layout-compatible with the kernels' merc_lifting_step,
 * so we reinterpret_cast between them.
 */

#include "mercury_dwt.h"
#include "transform/dwt_kernels.h"

using namespace mercury_native;

static_assert(sizeof(MercuryLiftingStep) == sizeof(merc_lifting_step),
              "MercuryLiftingStep must be layout-compatible with "
              "merc_lifting_step");

static inline merc_lifting_step* cast_step(MercuryLiftingStep* s)
{
  return reinterpret_cast<merc_lifting_step*>(s);
}

extern "C" {

void mercury_hwy_splice_16(int16_t* src1, int16_t* src2, int16_t* dst, int pairs)
{
  hwy_interleave_16(src1, src2, dst, pairs);
}

void mercury_hwy_splice_32(int32_t* src1, int32_t* src2, int32_t* dst, int pairs)
{
  hwy_interleave_32(src1, src2, dst, pairs);
}

void mercury_hwy_vply_16_5x3_weave_s0(int16_t** src, int16_t* dst_in, int16_t* dst_out, int samples)
{
  hwy_vlift_16_5x3_synth_s0(src, dst_in, dst_out, samples);
}

void mercury_hwy_vply_16_5x3_weave_s1(int16_t** src, int16_t* dst_in, int16_t* dst_out, int samples)
{
  hwy_vlift_16_5x3_synth_s1(src, dst_in, dst_out, samples);
}

void mercury_hwy_vply_32_2tap_irrev(int32_t** src, int32_t* dst_in, int32_t* dst_out, int samples,
                                    MercuryLiftingStep* step)
{
  hwy_vlift_32_2tap_irrev(src, dst_in, dst_out, samples, cast_step(step));
}

void mercury_hwy_vply_32_5x3_weave_s0(int32_t** src, int32_t* dst_in, int32_t* dst_out, int samples,
                                      MercuryLiftingStep* step)
{
  hwy_vlift_32_5x3_synth_s0(src, dst_in, dst_out, samples, cast_step(step));
}

void mercury_hwy_vply_32_5x3_weave_s1(int32_t** src, int32_t* dst_in, int32_t* dst_out, int samples,
                                      MercuryLiftingStep* step)
{
  hwy_vlift_32_5x3_synth_s1(src, dst_in, dst_out, samples, cast_step(step));
}

void mercury_hwy_hply_16_5x3_weave_s0(int16_t* src, int16_t* dst, int samples)
{
  hwy_hlift_16_5x3_synth_s0(src, dst, samples);
}

void mercury_hwy_hply_16_5x3_weave_s1(int16_t* src, int16_t* dst, int samples)
{
  hwy_hlift_16_5x3_synth_s1(src, dst, samples);
}

void mercury_hwy_hply_32_2tap_irrev(int32_t* src, int32_t* dst, int samples,
                                    MercuryLiftingStep* step)
{
  hwy_hlift_32_2tap_irrev(src, dst, samples, cast_step(step));
}

void mercury_hwy_hply_32_5x3_weave_s0(int32_t* src, int32_t* dst, int samples,
                                      MercuryLiftingStep* step)
{
  hwy_hlift_32_5x3_synth_s0(src, dst, samples, cast_step(step));
}

void mercury_hwy_hply_32_5x3_weave_s1(int32_t* src, int32_t* dst, int samples,
                                      MercuryLiftingStep* step)
{
  hwy_hlift_32_5x3_synth_s1(src, dst, samples, cast_step(step));
}

} /* extern "C" */
