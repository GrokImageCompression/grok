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
void mercury_hwy_hply_32_2tap_irrev(int32_t* src, int32_t* dst, int samples,
                                    MercuryLiftingStep* step);
void mercury_hwy_hply_32_5x3_weave_s0(int32_t* src, int32_t* dst, int samples,
                                      MercuryLiftingStep* step);
void mercury_hwy_hply_32_5x3_weave_s1(int32_t* src, int32_t* dst, int samples,
                                      MercuryLiftingStep* step);

#ifdef __cplusplus
} /* extern "C" */
#endif
