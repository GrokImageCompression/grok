# mercury migration

Goal: make mercury the default decode path, then retire classic decode.
Measured basis: mercury's streaming DWT is ~8x faster per core than the
classic two-phase kernels on every filter, precision, and size
(tests/grk_dwt_bench.cpp).

## phase 0: make mercury trustworthy (done except oss-fuzz rust toolchain)

- [x] A/B sweep test: decode the corpus classic vs GRK_MERCURY=1, bit-exact
      for reversible, small peak tolerance for irreversible
      (tests/mercury_ab_sweep.py)
- [x] triage and fix the sweep failures: green, 0 FAIL over both corpora
  - [x] reversible mismatches: both were classic bugs (first-band y0 clamp,
        tiff partial-strip carry), fixed and verified against openjpeg
  - [x] TNsot policy: classic keeps rejecting adobe-style TNsot undercounts
        (parallel tile-part parse relies on TNsot), mercury tolerates and
        decodes them correctly, sweep counts these as classic-rejects
  - [x] ESP truncated + issue432 were sweep-harness ENOSPC on tmpfs, not
        codec bugs: run the sweep with a disk-backed work dir
  - [x] remaining irreversible fails were not divergences: outputs are
        byte-identical, compare_images cannot load some classic tifs, sweep
        now byte-compares first and reports unloadable outputs separately.
        sweep is green: 0 FAIL over both corpora
- [x] CI lane running the sweep (.github/workflows/mercury_sweep.yml, asserts
      mercury is in the build so a rust-less runner cannot pass vacuously)
- [x] fuzz coverage: local mercury fuzzer + CIFuzz. findings all fixed:
      t1 shim per-thread coder leak, level_builder panic on corrupt band
      geometry (now a decode error), mapped-stream and fd leaks on early
      init failures, and classic's eager canvas allocation (a 111-byte SIZ
      could commit 26 GB, allocation now waits for decoded tile data)
  - [ ] oss-fuzz proper still fuzzes classic only: grok's oss-fuzz Dockerfile
        (google/oss-fuzz repo) needs rust, and build_google_oss_fuzzers.sh
        needs the mercury archive on the link line
- [x] TSAN soak of weft. premise confirmed: uninstrumented rust floods false
      positives (std's futex mutex/condvar edges are invisible to tsan), so
      rust needs nightly -Zsanitizer=thread with -Zbuild-std. recipe in the
      soak notes. 3672 instrumented decodes across thread counts and output
      paths: zero races
  - [x] the soak's concurrent harness found mercury decodes crossing files
        (input path was a process global, last writer won). fixed: path is
        per codec via IDecompressor::setInputFilePath

## classic output bugs found by the sweep (not migration blockers)

- incremental band writers emit a header-only tif when a tile row never
  completes (issue363-4740, issue391), and exit 0 with most tiles missing
- subsampled ycbcr tif output cannot be read back by grok's own tif reader
  or compare_images (issue142, issue432)
- createMappedFileReadStream fd/leak class: fixed, but the audit was per-file
  only. other stream creators may have the same unowned-handle pattern

## phase 1: close the eligibility gaps

Each is a MFP_BAIL in mercury_fastpath.cpp. Rough order:

1. buffer/callback stream input (add a memory-backed ReadAt)
2. resolution reduction (stop the level chain early)
3. layer limits (plan-stage packet filtering)
4. palette / ICC / channel definition (apply grok's existing post-processing
   to mercury's output rows on the host side, do not port into mercury)
5. region decode (hardest; first cut: crop rows in the stripe flow, decode
   full width, crop columns at output. Alternatively classic keeps regions
   permanently)

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
