#pragma once
#include <cstdint>

// C++ internals of the vendored Highway SIMD DWT kernels. All of mercury's
// native C++ lives in the single `mercury_native` namespace.
namespace mercury_native
{

// One DWT lifting step. Layout-compatible with MercuryLiftingStep
// (native/mercury_dwt.h) and Rust's ffi_dwt::MercuryLiftingStep: Rust builds
// it, the C bridge reinterpret_casts to it, and the Highway kernels read its
// data fields.
struct merc_lifting_step
{
  uint8_t step_idx;
  uint8_t support_length;
  uint8_t downshift;
  uint8_t extend;
  int16_t support_min;
  int16_t rounding_offset;
  float* coeffs;
  int* icoeffs;
  bool reversible;
  uint8_t kernel_id;
};

// Highway-dispatched DWT lifting/interleave kernels (multi-target, runtime-
// selected). Defined in hwy_dwt.cpp; the C bridge in mercury_dwt_bridge.cpp
// forwards the mercury_* FFI surface to these. Only the kernels the decoder
// dispatches to are declared.

extern void hwy_interleave_16(int16_t*, int16_t*, int16_t*, int);
extern void hwy_interleave_32(int32_t*, int32_t*, int32_t*, int);
extern void hwy_vlift_16_5x3_synth_s0(int16_t**, int16_t*, int16_t*, int);
extern void hwy_vlift_16_5x3_synth_s1(int16_t**, int16_t*, int16_t*, int);
extern void hwy_vlift_32_5x3_synth_s0(int32_t**, int32_t*, int32_t*, int, merc_lifting_step*);
extern void hwy_vlift_32_5x3_synth_s1(int32_t**, int32_t*, int32_t*, int, merc_lifting_step*);
extern void hwy_vlift_32_2tap_irrev(int32_t**, int32_t*, int32_t*, int, merc_lifting_step*);
extern void hwy_hlift_16_5x3_synth_s0(int16_t*, int16_t*, int);
extern void hwy_hlift_16_5x3_synth_s1(int16_t*, int16_t*, int);
extern void hwy_hlift_32_5x3_synth_s0(int32_t*, int32_t*, int, merc_lifting_step*);
extern void hwy_hlift_32_5x3_synth_s1(int32_t*, int32_t*, int, merc_lifting_step*);
extern void hwy_hlift_32_2tap_irrev(int32_t*, int32_t*, int, merc_lifting_step*);

} // namespace mercury_native
