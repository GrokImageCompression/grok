# Mercury encode plan

A streaming JPEG 2000 Part 1 compressor in mercury, built on weft like the
decoder. The client feeds strips, mercury encodes them as they arrive, and the
codestream is byte-identical to classic grok for the same parameters. The
targets are DCI 2K and 4K, then large untiled lossy images.

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

Classic writes the main header, EOC and TLM. Mercury writes the tile-parts in
classic's order, and classic seeks back to patch TLM and the jp2c box length.
The output must be seekable.

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

## Untiled lossy target

s2 is a 10980x10980 8-bit RGB TIFF encoded with `-I -r 24`: one tile, 64x64
code-blocks, LRCP, one layer, 6 resolutions, no precincts, 1 guard bit, ICT.
At 16 threads classic takes 780 ms and peaks at 1045 MB, of which the int16
component planes are 724 MB. The TIFF unpack, the ICT, the 9/7 and
quantization each pass over the whole image. The vertical 9/7 and the ICT
cost 5x and 8x more cycles at 16 threads than at one, so memory bandwidth
limits them. Strip input removes the whole-image buffers and the separate
passes together.

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
4. **T2 and PCRD.** Done, single tile: `encode::tile::encode_tile` through `mercury_encode_tile`. `grk_mercury_dci_encode_test` compares whole DCI codestreams with classic's library, slope hint and tolerance included. SOP, EPH, PLT and more than one layer are open.
   - packet bit writer with the 0xFF stuffing rule, tag tree encoder
   - markers: SOC, SIZ, COD, COC, QCD, QCC, POC, SOT, SOD, EOC, TLM, PLT
   - convex hull and bisect ported from `RateControl.cpp` and `TileProcessor_RateControl.cpp`, same iteration order and double arithmetic, both caps
   - all progression orders, POC, tile-parts split by component, SOP, EPH

   Test: byte-identical with classic on DCI 2K and 4K, then other parameter sets.
5. **Parameters over the C ABI.** Done. Grok passes the same `MercuryMainHeader` the decoder takes (`fillMercuryCodingHeader`), plus classic's layer byte budget, the component cap and the tile-part split. Classic writes the main header, so mercury has no marker writer, and band steps come from the header's QCD and QCC. `codingMatchesMercury` and `parametersMatchMercury` in `mercury_compress.cpp` list what mercury takes: one tile, one layer, PCRD, 9/7, code-block style 0, int32 unsigned planes of one precision, any progression and POC, precincts, and tile-parts split by component under CPRL. `grk_mercury_tile_encode_test` compares eight parameter sets with classic and checks three that go to classic.
6. **int16 lossy path.** Done. Int16 planes up to 10 bit take classic's int16 ICT, 9/7 and quantization bit for bit: `AnalysisSample for i16`, the int16 kernels in `native/transform/hwy_dwt_analysis.cpp`, `hwy_ict.cpp` and `hwy_quantize.cpp`, and grok's level factors and band scales passed in `MercuryEncodeParams`. `grk_mercury_forward_dwt_test` compares 8 and 10 bit int16 against classic's `dwt97_16`, and `grk_mercury_tile_encode_test` compares whole codestreams.
7. **PCRD memory.** Done. For s2 PCRD holds 29.5 MB of coded bytes, 1.5 MB of pass records and 1.1 MB of block records, against 724 MB of int16 planes, so strip input removes nearly all of the peak.
8. **Threading on one large image.** Done. s2 at 16 threads on 8 cores with 2 hardware threads each, best of 5: 660 ms before, 501 ms now, reference encoder 462 ms, from `compare_reference.sh`. Byte-identical throughout.
   - `TIFFFormat::pushStrips` reads the next strip on a second thread while the current strip is pushed: 660 to 600 ms. The feeding thread was busy for the whole run, reading and deinterleaving the TIFF (about 200 ms), the ICT (76 ms) and the 9/7 for every component (225 ms).
   - `TilePacketWriter::write_packets` writes each precinct's packets on its own thread, biggest precincts first, for rate control's trial writes and the final write: 600 to 585 ms. s2 has one precinct per resolution, so the three finest precincts limit it.
   - One 9/7 thread per component, with the ICT on the pushing thread, gives no gain after the read thread, because the 15 stripe workers are then 87 to 91 percent busy during the feed. T1 is 80 percent of all CPU: cleanup pass 44, significance pass 20, refinement pass 10, and quantization another 9.
   - `TileEncoder::finish` writes the SOT header, each packet header and slices of the code-block bytes straight to grok's stream through `mercury_encoder_finish`'s write callback, so the coded bytes are copied once, and `collect_stripes` reserves each band's total before appending: 585 to 540 ms.
   - `native/t1enc` takes classic's packed MQ contexts and classic's unrolled cleanup pass: 540 to 510 ms, user cycles 25.9 G to 23.1 G. Mercury's cleanup pass now uses 9.2 G cycles on s2 against classic's 11.1 G, and the significance and refinement passes are within 2 percent of classic. Renormalizing with a leading-zero count instead of one bit per step saves only 1 percent of cycles, since most renormalizations shift one bit.
   - `Impl_quantize_16` calls its per-vector lambda out of line under gcc -O2, with the running maximum on the stack. An `HWY_INLINE` helper takes it from 1.85 G to 1.48 G cycles.
   - Packing each stripe column's four bits per bit plane before the passes costs 2.6 G cycles and saves 0.4 G in the passes, and a branch-free MQ symbol encode removes only 7 percent of the 377 M branch misses. The misses sit on the per-sample decisions to code a sample and on `if(v)`, which pick whether an MQ symbol is coded at all. Visiting the significance pass's candidates through a 64-bit mask of 16 columns removes 32 M misses but takes the pass from 6.65 G to 7.85 G cycles, because building the mask costs more than the zero-flag column test it replaces.
   - Left after the workers stop, about 38 ms, 27 ms of it on one thread in pieces of 2 to 3 ms: `collect_stripes`, the rate control bisection, spawning the packet threads, the stream writes, and freeing the coded bytes. `makeForward97Int16Plan` takes about 8 ms at the start.
9. **Strip API in grok.** Done. `grk_compress_init_strips` takes a header image, `grk_compress_push_strip` takes any number of rows, and `grk_compress` finishes. Widening to int32, the ICC transform, classic's XYZ and the cinema cut to 12 bits run on each strip, and the GPU plugin is skipped. The lcms transform is built once at init by `GrkImage::createIccTransform`. Mercury's `TileEncoder` keeps its stripe workers across pushes, and a whole-image encode is one push. When mercury rejects the parameters, classic collects the strips into the image. `grk_compress` reads uncompressed and compressed TIFF strips in order, unmapped, on the calling thread. PNM, PGX, RAW, JPEG, PNG and BMP are read 64 rows at a time, the next rows while the current ones are coded. Adam7 PNG and RLE BMP decode the whole image before the first strip, because libpng finishes Adam7 rows only on the last pass and RLE rows have no fixed file offset. Multi-component RAW from stdin uses the whole-image read, because the file stores one component after another and stdin can't seek. Bretagne2 from PNM at 16 threads: 54 ms and 29 MB peak, against 77 ms and 56 MB with the whole image read first. s2 at 16 threads with `-I -r 24`, against the whole image read first: PNG 1219 ms and 119 MB against 1551 ms and 1047 MB, BMP 561 ms and 125 MB against 897 ms and 1045 MB, RAW 547 ms and 127 MB against 686 ms and 806 MB. Lossless Adam7 PNG is 4.5 percent slower, 2395 ms against 2290 ms on s2. `grk_mercury_tile_encode_test` and `grk_mercury_dci_encode_test` compare strip encodes with classic byte for byte. s2 at 16 threads: 659 ms and 154 MB peak, against 657 ms and 1044 MB with whole planes. The time is unchanged because one thread now reads the TIFF and feeds the ICT and 9/7.
10. **Tiled images**, one tile row at a time. Done. Strips are cut into tile slices, the tiles of one tile row are coded together on the shared stripe workers, and a finished tile keeps only its coded stripes. Rate control is classic's image-wide PCRD (`CodeStreamCompress::allocateImageWideRate`): one slope threshold for every tile, measured by simulating every tile's packets, then tile-parts in tile order. Per-tile rate control (fixed quality, component caps) stays on classic, as do tile grids and images whose origin is not 0,0. Slices:
   - Done. Multiple tiles with image-wide PCRD, compared with classic byte for byte, partial edge tiles and strips that cross tile rows included
   - Done. PLT: classic writes all of a tile's packet lengths in its first tile-part header, in segments of at most 65531 bytes. `simulateLayersAt` counts those bytes in every image-wide rate measurement, and classic's per-tile PCRD leaves them out. Classic's cinema 4K PLT listed lengths component by component while its tile-parts go progression by progression, and now follows the stream
   - Done. Tiled s2 from `compare_reference.sh` (687 tiles, 16x16 blocks, 128 precincts, PLT) at 16 threads: 865 ms and 322 MB peak, against 981 ms and 334 MB for the reference encoder and 1561 ms for classic. The tiles of a tile row are fed to their ICT and 9/7 in parallel, since one pushing thread doing every tile took 820 ms with the stripe workers 60% busy. Collecting each tile's coded stripes and building its packet writer also run in parallel, which removed 240 ms on one thread after the last stripe. Freeing the precinct coders in parallel was slower than on one thread
   - Done. Untiled s2 with 16x16 blocks and 128 precincts, about 30000 precincts: 1002 ms against 1477 ms before and 830 ms for the reference encoder. The precinct coders are built in parallel and sorted by block count once, `feasible_passes` runs band by band in parallel, and `run_tasks` hands out tasks with an atomic counter in place of one locked queue. Rate simulations then wrote packet headers into `PrecinctPackets` that the main thread assembled and freed, about 20 ms per simulation, and freeing about 660000 precinct coder allocations took 60 ms on one thread at the end. `TilePacketWriter::packet_lengths` writes only the lengths, in packet order, and each `PrecinctCoder` keeps its tag tree nodes and block states in two vectors, with level offsets computed on the walk to the root: 1081 to 807 ms and 324 to 233 MB peak, against 829 ms for the reference encoder on the same machine. Tiled s2 from `compare_reference.sh` went from 869 to 820 ms.
11. **Incremental output** for encodes without rate control.

## Rejected in version 1

HT, ROI, custom MCT, MCT with subsampled components, and BYPASS, RESTART and the other code-block mode switches.
