# Mercury encode plan

A streaming JPEG 2000 Part 1 compressor in mercury, built on weft like the
decoder. The client feeds strips, mercury encodes them as they arrive, and the
codestream is byte-identical to classic grok for the same parameters. The main
target is DCI 2K and 4K.

## Decisions

- Speed and memory both matter. Input arrives strip by strip, never as a whole image.
- New public grok API: the client pushes strips. The call blocks while mercury's source ring is full.
- MQ (Part 1) only. No HT.
- Output is byte-identical to classic. Any difference is a bug.
- T1 is mercury's own copy of grok's pass code (MQ unchanged), so the code around MQ can be tuned. Every block is checked against grok's `compress_cblk`.
- Classic is not changed. All new codec work is in mercury.
- PCRD is in version 1. With rate control every packet of the tile is held until the tile ends. Without it, packets are written as soon as the next one in progression order is ready.
- All five progression orders and POC.
- When mercury rejects an image, the strip API collects the strips into a `grk_image` and runs classic.

## Output memory

Mercury writes packets in classic's order and seeks back to patch Psot, TLM
and the jp2c box length, like classic does. The output must be seekable.

What gets held:

| Parameters | Held |
|---|---|
| PCRD | every packet of the tile |
| PLT | every packet of the tile, the lengths precede SOD |
| no rate control, PCRL, precincts covering a strip at every resolution | about one strip of packets |
| no rate control, LRCP with whole-resolution precincts | everything, the first packet needs the whole image |

## DCI parameters

From `Profile::setCinemaParams` and `Profile::init4kPoc`:

- 3 components, 12-bit unsigned, ICT, 9/7
- one tile, 32x32 code-blocks, 256x256 precincts, CPRL
- one layer, one tile-part per component
- 4K: a POC that splits off the top resolution, 2 guard bits
- PCRD under `max_cs_size` and the per-component `max_comp_size`, enforced in both `TileProcessor_RateControl.cpp` and `T2Compress.cpp`
- classic runs the XYZ transform and the cut to 12 bits on the whole image before compressing. Both work per sample, so mercury runs them per strip with classic's arithmetic.

A DCI frame is small and PCRD holds it whole, so for DCI the gain is throughput.

## Classic baseline

Ryzen 9 6900HX, 8 cores and 16 threads, `benchmark/dci_encode.py` on 24
frames:

| frames | 1 thread | 16 threads | peak RSS |
|---|---|---|---|
| 2K, 2048x857 | 1.5 fps | 11.5 fps | 105 MB |
| 4K, 4096x2160 | 1.1 fps | 4.1 fps | 259 MB |

Single-thread time by stage, from perf:

| stage | 2K | 4K |
|---|---|---|
| T1 MQ passes | 82% | 44% |
| forward DWT | 4% | 19% |
| quantization in `Coder::preCompress` | 5% | 14% |
| page faults | 6% | 14% |
| T2 and rate control | 1% | 4% |

- Quantization in the 9/7 path is scalar, one `grk_lrintf` per sample. A SIMD version with the same rounding can be bit-exact.
- The page faults come from first-touching whole-image buffers. Strip input avoids them.
- At 16 threads a 4K frame takes 300 ms. The TIFF read and the split into component planes take the first 60 ms on one thread. The DWT averages about 4 busy threads, T1 about 10. The `pcrdBisectFeasible` lambda costs about 110 ms of CPU at 16 threads against about 15 ms at one.
- Frames in batch mode run one after another. Strip input lets the next frame's read and DWT overlap the current frame's T1 and rate control.
- T1 is the same code in mercury. The gain has to come from removing the serial phases, quantization and page faults, and from overlapping frames.

## Phases

0. **Benchmark.** DCI 2K and 4K compression benchmark, classic profiled per stage. T1 stays grok's code, so the profile sets how much pipelining, cache locality and frame concurrency can gain.
1. **Forward DWT.** Done. `dwt::analysis::LevelAnalysis` takes rows top to bottom and runs each lifting step on a row once both neighbour rows have reached the previous step, so it holds about 7 rows. `feed` lets the caller write a row straight into the engine, and `emit` deinterleaves with SIMD and lifts horizontally straight into the caller's band rows, which can be the next level's row, so no row is copied between levels. Classic's kernels lift whole columns and can't stream, so the 9/7 step is a row kernel in `native/transform/hwy_dwt_analysis.cpp` with classic's constants and expression, which compiles to the same FMAs. 5/3 is integer and exact in any implementation, so it is plain Rust. `grk_mercury_forward_dwt_test` compares every coefficient bit for bit against classic's kernels (`grk_classic_forward_dwt_tile` in `src/lib/core/mercury_dwt_oracle.cpp`) on 975 geometries per mode, DCI 2K and 4K included. Single-threaded 4K 9/7, 6 levels, one component: 10 ms against classic's 55 ms.
2. **T1 encode shim.** C wrapper over `BlockCoder::compress_cblk`: quantized row-major samples in, bytes and per-pass length, rate and distortion out. Rust side is a `BlockEncoder` fn pointer.
3. **Encode graph.**
   - source node: XYZ, precision cut, DC shift, RCT/ICT per strip
   - one analysis node per level per component
   - encode nodes per subband, column-sliced: collect a block row, quantize, call T1
   - store node for the tile's code-block records
   - multiple tiles run one tile row at a time
4. **T2 and PCRD.** DCI 2K and 4K done, single thread: `encode::dci::encode_dci_frame` through `mercury_encode_dci`. `grk_mercury_dci_encode_test` compares whole codestreams with classic's library, slope hint and tolerance included. Other parameter sets, SOP, EPH and PLT are open.
   - packet bit writer with the 0xFF stuffing rule, tag tree encoder
   - markers: SOC, SIZ, COD, COC, QCD, QCC, POC, SOT, SOD, EOC, TLM, PLT
   - convex hull and bisect ported from `RateControl.cpp` and `TileProcessor_RateControl.cpp`, same iteration order and double arithmetic, both caps
   - all progression orders, POC, tile-parts split by component, SOP, EPH

   Test: byte-identical with classic on DCI 2K and 4K, then other parameter sets.
5. **Strip API in grok.** Push entry point, eligibility checks, and the classic fallback.
6. **Incremental output** for encodes without rate control.

## Rejected in version 1

HT, ROI, custom MCT, MCT with subsampled components, and BYPASS, RESTART and the other code-block mode switches.
