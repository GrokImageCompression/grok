/* mercury_dwt.h — C API for Highway SIMD DWT kernels.
 *
 * extern "C" surface for the Highway-dispatched W5X3/9-7 lifting and phase
 * interleave kernels the decoder dispatches to, callable from Rust via FFI.
 *
 * MercuryLiftingStep is layout-compatible with the C++ bridge's
 * merc_lifting_step: Rust builds it and passes a pointer, the bridge casts it
 * directly.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Lifting step — C-compatible layout matching the bridge's step struct       */
/* ========================================================================== */

typedef struct
{
  uint8_t downshift;
  uint8_t extend;
  float* coeffs;
  int* icoeffs;
  bool reversible;
  uint8_t kernelId;
} MercuryLiftingStep;

/* i32 lane count of the dispatched kernels' vectors. Layout granularity must
 * come from this so it matches the kernels on every target. */
int mercury_hwy_lanes_32(void);

void mercury_hwy_splice_16(int16_t* src1, int16_t* src2, int16_t* dst, int pairs);

void mercury_hwy_splice_32(int32_t* src1, int32_t* src2, int32_t* dst, int pairs);

void mercury_hwy_vply_16_5x3_weave_s0(int16_t** src, int16_t* dstIn, int16_t* dstOut, int samples);
void mercury_hwy_vply_16_5x3_weave_s1(int16_t** src, int16_t* dstIn, int16_t* dstOut, int samples);

void mercury_hwy_vply_32_2tap_irrev(int32_t** src, int32_t* dstIn, int32_t* dstOut, int samples,
                                    MercuryLiftingStep* step);
void mercury_hwy_vply_32_5x3_weave_s0(int32_t** src, int32_t* dstIn, int32_t* dstOut, int samples,
                                      MercuryLiftingStep* step);
void mercury_hwy_vply_32_5x3_weave_s1(int32_t** src, int32_t* dstIn, int32_t* dstOut, int samples,
                                      MercuryLiftingStep* step);

void mercury_hwy_hply_16_5x3_weave_s0(int16_t* src, int16_t* dst, int samples);
void mercury_hwy_hply_16_5x3_weave_s1(int16_t* src, int16_t* dst, int samples);

/* int16 fixed-point 9/7 (Q13 samples), one kernel per synthesis step. */
void mercury_hwy_vply_16_97_weave_s0(int16_t** src, int16_t* dstIn, int16_t* dstOut, int samples);
void mercury_hwy_vply_16_97_weave_s1(int16_t** src, int16_t* dstIn, int16_t* dstOut, int samples);
void mercury_hwy_vply_16_97_weave_s2(int16_t** src, int16_t* dstIn, int16_t* dstOut, int samples);
void mercury_hwy_vply_16_97_weave_s3(int16_t** src, int16_t* dstIn, int16_t* dstOut, int samples);
void mercury_hwy_hply_16_97_weave_s0(int16_t* src, int16_t* dst, int samples);
void mercury_hwy_hply_16_97_weave_s1(int16_t* src, int16_t* dst, int samples);
void mercury_hwy_hply_16_97_weave_s2(int16_t* src, int16_t* dst, int samples);
void mercury_hwy_hply_16_97_weave_s3(int16_t* src, int16_t* dst, int samples);
void mercury_hwy_hply_32_2tap_irrev(int32_t* src, int32_t* dst, int samples,
                                    MercuryLiftingStep* step);
void mercury_hwy_hply_32_5x3_weave_s0(int32_t* src, int32_t* dst, int samples,
                                      MercuryLiftingStep* step);
void mercury_hwy_hply_32_5x3_weave_s1(int32_t* src, int32_t* dst, int samples,
                                      MercuryLiftingStep* step);

/* forward 9/7 lifting, one step over one row */
void mercury_hwy_analysis_lift_97(int step, float* target, const float* left, const float* right,
                                  int samples);

/* split `pairs` sample pairs into even and odd samples */
void mercury_hwy_analysis_deinterleave_32(const uint32_t* row, uint32_t* even, uint32_t* odd,
                                          int pairs);

/* grok's irreversible quantization into sign-magnitude with six fraction bits */
uint32_t mercury_hwy_quantize_97(const float* stripe, int stride, int columns, int rows,
                                 double quant, int32_t* block);

/* grok's part-1 block coder for code-block style 0; returns the pass count */
int32_t mercury_t1_encode_block(const int32_t* samples, int32_t num_cols, int32_t num_rows,
                                uint32_t max_magnitude, int32_t orientation, int32_t compno,
                                int32_t level, double stepsize, int32_t mct,
                                const uint8_t** coded_data, uint16_t* pass_rates,
                                double* pass_distortions, uint8_t* pass_terminated,
                                int32_t* numbps);

/* grok's forward irreversible colour transform over one row, `dc_shift` added first */
void mercury_hwy_forward_ict(const int32_t* red, const int32_t* green, const int32_t* blue,
                             int32_t dc_shift, float* luma, float* chroma_blue, float* chroma_red,
                             int samples);

/* grok's Quantizer::generate step exponent and mantissa for one irreversible band */
void mercury_t1_band_step(uint8_t numresolutions, uint8_t max_bit_depth, uint8_t bandno,
                          double stepScale, uint8_t* exponent, uint16_t* mantissa);

/* grok's RateControl::convexHull over one block's passes */
void mercury_rate_convex_hull(const uint16_t* lens, const double* distortions, uint16_t* slopes,
                              uint32_t num_passes);

#ifdef __cplusplus
} /* extern "C" */
#endif
