# mercury migration

Goal: make mercury the default decode path, then retire classic decode.
Measured basis: mercury's streaming DWT is ~8x faster per core than the
classic two-phase kernels on every filter, precision, and size
(tests/grk_dwt_bench.cpp).

Phase 0 (make mercury trustworthy) is done: A/B sweep green over both
corpora with a CI lane, fuzzing local + CIFuzz + oss-fuzz, TSAN soak clean.

## open bugs (not migration blockers)

- repeated grk_decompress on one codec is a supported flow (sequential tile
  decode, see 759d7a71 slated tiles) and used to work. it regressed when the
  all-tiles-cached early return started re-transferring the drained scratch
  image, nulling the composite's buffers. the reject-second-call guard from
  6b0a99da is an interim stopgap: revert it, restore repeated decompress,
  and flip grk_double_decompress_test to assert the restored behavior
- decode of conformance p0_07.j2k to tif deadlocks every run, all workers
  parked on one futex. the same file decodes fine to pgx, so the hang is in
  the tiff output path
- zoo1.jp2 segfaults when decoded to tif
- with compare_images fixed (it was blind in both modes), 26 compare tests
  fail on real decode drift, in five groups. large errors: p0_03, p0_08,
  p0_09, p0_10, p0_15 miss the reference badly (p0_08 comp 1 MSE 125, peak
  390), and p1_05, p1_06 are wrong on nearly every sample (MSE ~1900, peak
  ~230), which reads like a transform or component bug. small drift: p0_04,
  p0_05, p0_06, p1_02, p1_03, p1_04 and the richter subsampled files are
  within a few code values of the reference but not bit exact, and p0_05,
  p1_03 marginally exceed the Table C.6/C.7 conformance limits
- j2k_multi_region_decompress is built but never registered as a ctest, and
  fails on five of six conformance inputs with null tile data in its
  asynchronous row-wait path
- CurlFetcher::configureHandle leaks a curl_slist per range request, on
  success and failure alike. track the list per handle (FetchResult,
  CURLOPT_PRIVATE) and free it at the curl_easy_cleanup sites
- memStreamCreate documents ownsBuffer=true as taking ownership but frees
  nothing on its three early exits. all call sites pass false today, so
  delete the two unused ownership parameters instead of fixing the paths
- CurlFetcher::listDirectory and getMetadata leak an easy handle when parse
  throws, and neither has a caller. delete them or move parse above
  curl_easy_init
- createMappedFileReadStream reads up to 22 bytes past the mapping when
  initial_offset lands within 22 bytes of file length. the guard needs to be
  initial_offset + GRK_JPEG_2000_NUM_IDENTIFIER_BYTES > len. the offset is
  api-supplied, not file content, so low severity
- eycc images are written to tif as PHOTOMETRIC_YCBCR with rec.601
  coefficients (TIFFFormat.h writeHeader), so converting readers get wrong
  colors. tif has no eycc photometric: warn on write or reject. byte round
  trips are unaffected. the ycc444 round-trip mismatch itself was not a
  bug, a raw j2k intermediate cannot carry colorspace and jp2 round trips
  byte-identically, coverage added in 7917296a


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
