# mercury migration

Goal: make mercury the default decode path, then retire classic decode.
Measured basis: mercury's streaming DWT is ~8x faster per core than the
classic two-phase kernels on every filter, precision, and size
(tests/grk_dwt_bench.cpp).

Phase 0 (make mercury trustworthy) is done: A/B sweep green over both
corpora with a CI lane, fuzzing local + CIFuzz + oss-fuzz, TSAN soak clean.

## classic output bugs found by the sweep (not migration blockers)

- incremental band writers emit a header-only tif when a tile row never
  completes (issue363-4740, issue391), and exit 0 with most tiles missing
- subsampled ycbcr tif output cannot be read back by grok's own tif reader
  or compare_images (issue142, issue432)
- createMappedFileReadStream fd/leak class: fixed, but the audit was per-file
  only. other stream creators may have the same unowned-handle pattern
- a second grk_decompress call on the same codec returns true but hands back
  an image whose comps[0].data is null
- region decode returns wrong pixels for tiled images on a fixed set of
  source rows regardless of window position (61x67 with 14x15 tiles: rows
  28, 39, 40, 43, 44, 55, 56, 58, 59). single-tile windows and whole-tile
  windows are exact, so suspect the band-window derivation for tiles with
  non-zero origin: getPaddedBandWindow (ResWindow.h, has a standing todo on
  its padding factor) feeding WaveletReversePartial. repro:
  grk_compress -t 14,15 then grk_decompress -d 0,43,61,45
- encode_97_v returns early on height <= 1, skipping DC shift AND the
  int-to-float conversion, so 9/7 encodes of one-row finest-level tiles are
  badly wrong (worst error ~14768 at height 1) and heights 2-4 are also
  broken (~7300) by something beyond that function. a one-row-only fix
  mirroring WaveletReverse97's trivial case only halves the height-1 error.
  needs the forward single-sample rule derived for grok's 9/7 normalization
  at every degenerate level, plus a lossy-output review
- weft's wakeup protocol has no loom model (node.rs claimed one existed in
  tests/weft_loom.rs, it does not). the tug() lost-wakeup was exactly the
  class a model catches. write the model, mirror node.rs atomics, wire it
  into cargo test

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
