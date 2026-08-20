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
- mercury fast path decodes nondeterministically on many-tile streams
  (640x512 with 14x15 tiles, mismatch in 24 of 30 in-process decodes,
  always at x=0 on rows that are multiples of the tile height, the first
  column of each tile row). classic is stable on the same file
- encoder drops the DC level shift for a one-row finest-level tile
  (encode_53_v returns early on height <= 1 with even y0), so lossless
  encodes of those geometries write codestreams off by +128. encode_97_v
  has the same early return and also skips int-to-float, untested
- region decode returns wrong pixels for a short window ending on a tile
  boundary when the tile y0 is odd (WaveletReversePartial path), e.g.
  -d 0,43,61,45 on a 61x67 image with 14x15 tiles

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
