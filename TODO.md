# mercury migration

Goal: make mercury the default decode path, then retire classic decode.
Measured basis: mercury's streaming DWT is ~8x faster per core than the
classic two-phase kernels on every filter, precision, and size
(tests/grk_dwt_bench.cpp).

## phase 0: make mercury trustworthy (in progress)

- [x] A/B sweep test: decode the corpus classic vs GRK_MERCURY=1, bit-exact
      for reversible, small peak tolerance for irreversible
      (tests/mercury_ab_sweep.py)
- [ ] triage and fix the sweep failures (reversible mismatches and decode
      failures are bugs; irreversible divergence beyond a few LSBs needs
      a bail or a fix)
  - [x] reversible mismatches: both were classic bugs (first-band y0 clamp,
        tiff partial-strip carry), fixed and verified against openjpeg
  - [x] TNsot policy: classic keeps rejecting adobe-style TNsot undercounts
        (parallel tile-part parse relies on TNsot), mercury tolerates and
        decodes them correctly, sweep counts these as classic-rejects
  - [x] ESP truncated + issue432 were sweep-harness ENOSPC on tmpfs, not
        codec bugs: run the sweep with a disk-backed work dir
  - [ ] 3 remaining irreversible peak-tolerance fails: issue142.j2k,
        issue363-4740.jp2, issue391.jp2
- [ ] CI lane running the sweep
- [ ] fuzz coverage: mercury variant of grk_decompress_fuzzer (file-backed
      input), oss-fuzz needs a rust toolchain before it covers mercury
- [ ] TSAN soak of weft. Mixing instrumented C++ with uninstrumented rust
      floods false positives, so this likely needs rust built with
      -Zsanitizer=thread on nightly

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
