# mercury migration

Goal: make mercury the default decode path, then retire classic decode.
Measured basis: mercury's streaming DWT is ~8x faster per core than the
classic two-phase kernels on every filter, precision, and size
(tests/grk_dwt_bench.cpp).

Phase 0 (make mercury trustworthy) is done: A/B sweep green over both
corpora with a CI lane, fuzzing local + CIFuzz + oss-fuzz, TSAN soak clean.

## open bugs (not migration blockers)

- grk_decompress after grk_decompress_set_progression_state is safe but a
  no-op: decompressImpl filters dirty tiles out via isBestEffortDecompressed
  (set on every decoded tile, cleared only by the single-tile and lru
  paths), and under GRK_TILE_CACHE_NONE the tile data is freed anyway. the
  fix wants a real needs-re-decode signal separate from bestEffortDecompressed_
  and a raw composite with postProcess deferred to image handoff, so partial
  re-decode is expressible. per-tile re-decode through grk_decompress_tile
  works today
- p0_07.j2k async simulate-synchronous decode still deadlocks: the sync
  incremental band path was fixed (a row is not waited on until all its
  tiles are scheduled), but scheduleTileBatch's throttle has the same
  exposure for a swath covering a row whose last tile part sits past the
  window. needs the same not-fully-scheduled predicate on the async wait
- GrkImage::sycc444_to_rgb dispatches int32 to the SIMD hwy_sycc444_to_rgb_i32
  and everything else to the scalar sycc_to_rgb, and the two round
  differently (file2.jp2 decoded through int32 differs by 1 on 58 samples)
- the int16 decode eligibility rule is evaluated twice, in
  TileProcessor::createDecompressTileComponentWindows and again in
  CodeStreamDecompress::activateScratch for the multi-tile composite, and
  GrkImage::compositePlanar has no int32 to int16 branch, so a disagreement
  between the two is a heap overrun. decide once and carry the result
- eycc images are written to tif as PHOTOMETRIC_YCBCR implying rec.601
  coefficients (TIFFFormat.h writeHeader), so converting readers get wrong
  colors, and grok reads its own eycc tif back as sycc. byte round trips
  are unaffected. preferred fix: check whether eycc's inverse fits tiff's
  YCbCrCoefficients (529) plus ReferenceBlackWhite (532) model against
  grok's eycc transform code, write correct coefficients if so and teach
  the reader to distinguish them from 601, otherwise warn on write


## phase 1: close the eligibility gaps

Each is a MFP_BAIL in mercury_fastpath.cpp. Rough order (items 1 and 2,
stream input and resolution reduction, are done):

3. layer limits (plan-stage packet filtering)
4. palette / ICC / channel definition (apply grok's existing post-processing
   to mercury's output rows on the host side, do not port into mercury)
5. region decode (hardest; first cut: crop rows in the stripe flow, decode
   full width, crop columns at output. Alternatively classic keeps regions
   permanently)
6. adopt classic's packet-length shortcuts (PLT/TLM, PacketLengthCache) in
   the planner so huge-image progression parsing stays cheap. PLT pays off
   for packets the plan drops (reduce, layers, later regions), TLM replaces
   the sequential SOT scan. both are opt-in markers, test files need
   write_plt/write_tlm
7. precinct smaller than code-block is rejected at plan stage (effective
   block size clamping not wired)

## phase 2: performance completion

- int16 fixed-point 9/7 as a step-descriptor table in mercury
  (bench predicts 2-3 GP/s per core, halves line-buffer footprint)
- i16 output conversion path
- multithreaded end-to-end A/B: wall time and peak RSS across sizes

## phase 3: flip the default

- mercury on by default for eligible decodes, env var to force classic for
  one release cycle
- NR-DEC-text_GBR.jp2-29-decode expects rejection of a TNsot-undercount file:
  mercury tolerates these, so the expectation flips when mercury is default
- regenerate md5 refs; changelog the numerical change (lossy output shifts
  by ±1 in rounding neighborhoods, toward the exact transform)

## phase 4: retire classic decode

- delete WaveletReverse decompress paths, decompress task-graph scheduling,
  and the classic int16 pipeline (minus whatever classic keeps from phase 1)
- classic T1 stays: it is mercury's block coder through the shim
- encode is untouched throughout

## open policy decision

Builds without rust currently fall back to classic silently. After phase 4
that fallback is gone, so either rust becomes a hard build requirement for
decode or two decoders live forever. Current lean: rust required.
