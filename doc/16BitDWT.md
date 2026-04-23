# 16-Bit DWT Path for 5/3 Reversible Wavelet

## Overview

The 16-bit DWT path performs the 5/3 reversible (lossless) inverse discrete
wavelet transform entirely using `int16_t` arithmetic instead of `int32_t`.
This halves memory usage and bandwidth for wavelet coefficient buffers,
improving cache utilization and throughput for eligible images.

## Architecture

### Decision Point

The 16-bit path is selected at runtime in `TileProcessor::decompressInit()`
(TileProcessor.cpp) when all of the following hold:

1. **Reversible wavelet** (`qmfbid == 1`)
2. **Whole-tile decoding** (no region-of-interest partial decode)
3. **Precision + headroom ≤ 16 bits**:
   - MCT components (inverse RCT): `prec + 5 ≤ 16` → max precision 11 bits
   - Non-MCT components (DC shift only): `prec + 4 ≤ 16` → max precision 12 bits

### Data Flow

```
Tier-1 decode (int32)
  → NarrowShiftFilter (int32 → int16, in PostDecodeFilters.h)
  → 16-bit DWT synthesis (WaveletReverse.cpp)
  → DC shift (fused into final store, or via mct.cpp for MCT)
  → MCT inverse RCT (mct.cpp DecompressRev16 / DecompressDcShiftRev16)
  → composite to output image (GrkImage.h compositePlanar with int16→int32 widening)
```

### Key Files

| File | Role |
|------|------|
| `TileProcessor.cpp` | 16-bit eligibility decision |
| `WaveletReverse.cpp` | All DWT kernels (int32 and int16, scalar and HWY SIMD) |
| `WaveletReverse.h` | 16-bit entry point declarations |
| `PostDecodeFilters.h` | `NarrowShiftFilter` — int32→int16 narrowing after T1 decode |
| `mct.cpp` | `DecompressRev16` / `DecompressDcShiftRev16` — int16 MCT+DC shift |
| `GrkImage.h` | `compositePlanar()` — int16→int32 widening for multi-tile compositing |
| `GrkImage.cpp` | `transferDataFrom_T()` — int16 buffer transfer |
| `buffer.h` | Buffer type with `data_type` field for int16/int32 dispatch |

## BIBO Gain Analysis

The Bounded-Input-Bounded-Output (BIBO) gain determines the worst-case output
range expansion through the inverse DWT filter bank. This analysis determines
how many extra bits of headroom are needed beyond the image precision.

### 5/3 Lifting Steps (Synthesis)

The 5/3 reversible inverse DWT (synthesis) is performed as two lifting steps:

1. **Undo update (even samples)**: `s[n] -= floor((d[n-1] + d[n] + 2) / 4)`
2. **Undo predict (odd samples)**: `d[n] += floor((s[n] + s[n+1]) / 2)`

where `s[]` are even (low-pass) samples and `d[]` are odd (high-pass) samples.

### First-Principles BIBO Gain Derivation

The BIBO gain is the worst-case ratio of output magnitude to input magnitude,
computed by tracking the maximum possible amplification through each lifting
step.

**Single-level analysis:**

Consider subband coefficients bounded by some maximum magnitude M.

Step 1 (undo update): Each even sample `s[n]` is modified by subtracting
`floor((d[n-1] + d[n] + 2) / 4)`. The subtracted quantity has magnitude
at most `floor((M + M + 2) / 4) ≈ M/2`. So the even sample after this step
has magnitude at most `M + M/2 = 3M/2`.

Step 2 (undo predict): Each odd sample `d[n]` is modified by adding
`floor((s[n] + s[n+1]) / 2)`. The added quantity has magnitude at most
`floor((3M/2 + 3M/2) / 2) = 3M/2`. So the odd sample has magnitude
at most `M + 3M/2 = 5M/2`.

The worst-case 1D gain from a single synthesis level is therefore:
- Even (low) output: 3/2 = 1.5
- Odd (high) output: 5/2 = 2.0 (but this is the absolute worst case; the
  typical bound is tighter since `d[n]` and the update correction are correlated)

These match the first entries of the OJPH `bibo_gains` tables in
`QuantizerOJPH.cpp`:
```
gain_5x3_l[1] = 1.500   (low-pass output, 1 level)
gain_5x3_h[0] = 2.000   (high-pass output, 0 additional levels)
```

**Multi-level recursion:**

For L decomposition levels, the LL subband is further decomposed. The gain
builds recursively but converges because each additional level only affects the
lowest-frequency subband through a filter with gain < 2, applied to an
increasingly small fraction of the signal energy.

The per-subband, per-direction gains from the OJPH `bibo_gains` class:
```
gain_5x3_l[]: 1.000, 1.500, 1.625, 1.688, 1.696, 1.707, 1.712, ... → 1.716
gain_5x3_h[]: 2.000, 2.500, 2.750, 2.805, 2.820, 2.841, 2.856, ... → 2.867
```

The 2D worst-case gain to any subband is the product of horizontal and vertical
gains. The overall BIBO gain (maximum across all subbands and both dimensions)
converges:
- For ≤5 decomposition levels: overall gain < 2^3 (= 8)
- For >5 levels: gain slightly exceeds 2^3, converging to ~2^3.04

**Why it converges**: The update step has a transfer function that attenuates
by 1/4 (the `>> 2` shift), and each additional decomposition level compounds
this through an increasingly refined low-pass subband. The geometric series
converges, yielding a finite asymptotic gain of approximately `2.867^2 ≈ 8.22`
in 2D, which is `~2^3.04`.

### Lifting Step Intermediate Safety

The update step computes `floor((a + b + 2) / 4)` where `a` and `b` are
odd-indexed (high-pass) samples. When implemented using an **averaging
operation** (as in `update_avg_16_53()`), the intermediate value never exceeds
the magnitude of the inputs — the hardware averaging instruction uses a wider
internal accumulator, so there is no additional overflow risk beyond the BIBO
gain itself.

The predict step computes `d + floor((s_prev + s_next) / 2)` where `s_prev`
and `s_next` are already-updated even samples. The intermediate sum
`s_prev + s_next` (before the `>> 1` shift) has BIBO gain that can be shown
to always remain below 2^3 (since each `s` value has gain at most ~1.716,
and the sum is immediately halved).

### Headroom Calculation

For **16-bit synthesis**:

- **Non-MCT** (`rct_comp ≤ 1`): **4 bits** of headroom.
  This covers ~3 bits for the DWT BIBO gain (< 2^3 for typical ≤5 levels)
  plus ~0.5–1 bit safety margin for quantization errors (reversibly processed
  content can be quantized by code-block truncation).
  Max precision: `prec + 4 ≤ 16` → prec ≤ 12.
  For 12-bit: output range 2^12 × 2^3 ≈ 2^15 fits in int16_t.

- **MCT chrominance** (`rct_comp ≥ 2`): **5 bits** of headroom.
  The inverse RCT (Reversible Colour Transform) has additional BIBO gain
  of 1.5× for luminance and 2.0× for chrominance (Db/Dr) channels,
  since `Cr = B - G` and `Cb = R - G` can double the range, and
  `Y = G + floor((Cb + Cr) / 4)` adds up to 1.5× for luminance.
  Max precision: `prec + 5 ≤ 16` → prec ≤ 11.

Note: Our implementation conservatively uses 5 bits for ALL MCT components
(including luminance), simplifying the logic while remaining safe.

## Overflow-Safe SIMD Averaging

### The Problem

The 5/3 DWT update and predict steps compute averages of two sample values:

- **Update step**: `even -= floor((odd_prev + odd_next + 2) / 4)`
- **Predict step**: `odd += floor((even_prev + even_next) / 2)`

In scalar C++ code, `int16_t` operands are implicitly promoted to `int` before
arithmetic — no overflow is possible. However, in **SIMD code (HWY/Highway)**,
arithmetic stays within the vector lane width (int16). The intermediate sum
`a + b` of two int16 values can overflow the int16 range.

### Solution 1: Update Step — `update_avg_16_53()`

Computes `floor((a + b + 2) / 4)` without overflow using unsigned hardware
averaging.

**Identity chain**:
```
floor((a + b + 2) / 4) = (floor((a + b) / 2) + 1) >> 1
```

**Algorithm**:
1. Convert signed → unsigned: `a_u = a XOR 0x8000`
2. Bias b: `b_biased = b_unsigned + 0x7FFF` (equivalent to `b_unsigned - 1` mod 2^16)
3. Hardware unsigned average: `AverageRound(a_u, b_biased)` =
   `floor((a_u + b_biased + 1) / 2)` = `floor((a_u + b_u) / 2)`
   (the +1 from AverageRound cancels the -1 from the bias)
4. Unbias: `step = avg - 0x7FFF` → yields `floor((a+b)/2) + 1` in signed domain
5. Final shift: `step >> 1` → `floor((a + b + 2) / 4)`

The hardware `AverageRound` instruction (`_mm256_avg_epu16` on x86) uses a
17-bit intermediate accumulator internally, so the sum never overflows.

### Solution 2: Predict Step — `predict_avg_16_53()`

Computes `floor((a + b) / 2)` without overflow using bit decomposition.

**Identity**:
```
(a + b) >> 1 = (a >> 1) + (b >> 1) + ((a & b) & 1)
```

Each term is individually safe:
- `a >> 1` and `b >> 1` are half the original range
- `(a & b) & 1` is 0 or 1 (the "carry" from the lost LSBs)

### Why Scalar Code Doesn't Need This

In non-SIMD (scalar) C++ code, the standard integer promotion rules
automatically widen `int16_t` operands to `int` (at least 32 bits) before any
arithmetic operation. The intermediate sum `a + b` is computed in 32-bit
precision, so no overflow occurs. Only the SIMD kernels need the overflow-safe
averaging functions.
