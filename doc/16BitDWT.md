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

1. **Precision + headroom ≤ 16 bits**, per `WaveletInt16Rules.h`:
   - Reversible 5/3, encode and decode: `prec + 3 ≤ 16` up to five levels,
     `prec + 4 ≤ 16` above, plus one bit for RCT chroma
   - Decode, irreversible 9/7: `prec + 6 ≤ 16`
   - Compress, irreversible 9/7: never

A region (partial) decode qualifies on the same terms as a whole-tile decode,
under either filter, and the mercury fast path applies the same rule, so both
pipelines return the same sample type. All components of a decode share one
sample type, so one ineligible component forces every component to int32.

The decode decision uses the main header coding style. A tile-part COD or COC
that needs more headroom than that decision allows fails the decode with an
error. Mercury hands any int16 plan whose tiles change the level count, colour
transform or wavelet to the classic pipeline, which applies the check.

### Data Flow

```
Tier-1 decode (int32)
  → NarrowShiftFilter (int32 → int16, in PostDecodeFilters.h)
  → 16-bit DWT synthesis (WaveletReverse.cpp / WaveletReverse97_16.cpp), or
    16-bit partial synthesis over an int16 SparseCanvas (WaveletReversePartial.cpp,
    9/7 lifting via hwy_step_16_97)
  → Q-format downshift + DC shift (fused into the final store or the partial
    final read, or via mct.cpp for MCT)
  → MCT inverse RCT (mct.cpp DecompressRev16 / DecompressDcShiftRev16)
  → composite to output image (GrkImage.h compositePlanar with int16→int32 widening)
```

### Key Files

| File | Role |
|------|------|
| `TileProcessor.cpp` | 16-bit eligibility decision |
| `WaveletReverse.cpp` | All DWT kernels (int32 and int16, scalar and HWY SIMD) |
| `WaveletReversePartial.cpp` | Region-decode synthesis over a sparse canvas: 5/3 int32 and int16, 9/7 float and int16 |
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

For **16-bit synthesis**, a lossless decode retraces the forward transform, so
the int16 buffers must hold every forward intermediate value. The largest
magnitude at 12 bits with RCT is 32564, in chroma, over images built from the
sign of each 5/3 analysis response at five levels. The same search at 13 bits
gives 65132. At six levels the 12 bit RCT chroma maximum is about 33050, which
exceeds int16. This holds only because the update and predict steps below average
without overflow in an int16 lane.

For **16-bit analysis**, the worst case 5/3 analysis gain is 7.95 up to five
levels and 8.07 at six, so six or more levels need one more bit.

### Why the Rules Are What They Are

An int16 lane holds one sign bit and 15 magnitude bits. A p-bit unsigned
sample becomes signed after the DC shift, with magnitude at most 2^(p-1), so it
already uses p of the 16 bits. Every bit of growth in the transform has to fit
in the remaining 16 - p bits.

**Growth comes from the filter gain.** A wavelet coefficient is a weighted sum
of input samples. Its largest possible magnitude is the largest sample
magnitude times the sum of the absolute weights, which is the BIBO gain of that
subband's filter. The worst image for one coefficient sets each sample to plus
or minus full scale, matching the sign of its weight. That looks like a fine
checkerboard shaped by the filter. Natural images never come close, but a
lossless codec has to be exact for every input, so the worst case decides.

**The level count matters because gains compound.** A subband at level k is
k - 1 low pass filters followed by one high pass, in both directions. The 2D
gain is the row gain times the column gain. Computing it for the 5/3 filter
gives 4.0 at one level, 7.95 at five levels and 8.07 at six, and it converges
to about 8.2. Up to five levels the gain is below 8, so 3 extra bits are enough.
At six or more it exceeds 8, so 4 bits are needed.

**RCT chroma needs one more bit.** The RCT chroma components are differences
such as B - G. The difference of two p-bit samples spans twice the range of
either, so chroma enters the wavelet with p + 1 bits. Luma is a weighted average
of R, G and B, so its range stays within one sample.

**Sums inside the lifting steps do not add a bit.** Each lifting step adds two
neighbours and halves the result. The sum alone can need one bit more than its
inputs. The SIMD kernels use averaging instructions that never form the full sum
(see below), so only the values actually stored, the coefficients and the
partly lifted samples, have to fit.

**One 5/3 rule for encode and decode.** For a lossless stream made from an
in-range image, every value the inverse transform produces is a value the
forward transform produced, because lifting is undone step by step. So the
decoder needs exactly the headroom the encoder needs, and lossless decode is
exact under the same rule. The rule uses the exact gain bits: `prec + 3` up to
five levels, `prec + 4` above, plus one for RCT chroma. RCT luma needs no extra
bit. On encode the luma limit never decides anything, because the three RCT
components share one sample type and chroma has the tighter limit.

**Lossy 5/3 decode keeps no extra margin.** A stream decoded from fewer quality
layers carries truncated coefficients, and the reconstruction can ring past the
sample range before the final clamp. In rare cases that can wrap an int16 lane.
The rule does not reserve a bit for it, so lossy decodes keep the int16 speed.

**Tiles can change the inputs.** The decision is made once per decode, from
the main header, before tile headers are read. A tile-part header can raise the
level count or turn on RCT for one tile. Each tile is checked against the
decision, and a tile that needs more headroom fails the decode instead of
producing wrong samples.

**9/7.** The decoder lifts in fixed point with 3 fraction bits to keep sub-LSB
precision, on top of a synthesis gain of 2^2.79, so it needs `prec + 6`. The
encoder always uses float. Forward fixed-point lifting rounds at every step and
would change the coefficients compared with the float path.

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

### Solution 3: The Partial Path

The partial (region-decode) vertical pass in `WaveletReversePartial.cpp` writes
the same two identities against raw SSE2 intrinsics, in
`update_avg_epi16_53()` and `predict_avg_epi16_53()`, so its SIMD rows and its
scalar rows produce the same samples.

### Why Scalar Code Doesn't Need This

In non-SIMD (scalar) C++ code, the standard integer promotion rules
automatically widen `int16_t` operands to `int` (at least 32 bits) before any
arithmetic operation. The intermediate sum `a + b` is computed in 32-bit
precision, so no overflow occurs. Only the SIMD kernels need the overflow-safe
averaging functions.
