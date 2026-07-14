# Mercury

Pure-Rust JPEG 2000 Part 1 **streaming decode engine**, built on **weft**, a
deadlock-free pull-based dataflow engine.

Mercury is a decode engine embedded in
[Grok](https://github.com/GrokImageCompression/grok) (`libgrokj2k`) as a
full-resolution fast path. It decodes an eligible JP2/J2K codestream in a
single streaming pass with constant, O(support-window) buffering while making deadlocks impossible by construction. The full design rationale is
in [docs/weft-design.md](docs/weft-design.md).

The host codec (Grok) supplies two things across the C API
([include/mercury_capi.h](include/mercury_capi.h)):

- **tier-1 block decoding** — Grok plugs its own Part-1/HTJ2K block coder into
  the `mercury_weave` fn-pointer seam, so mercury ships no T1 of its own;
- **the DWT kernels** — Grok compiles the vendored Highway SIMD lifting
  kernels in [native/](native/) against its own Highway tree and links them,
  so this crate emits no C++ (`build.rs` is a no-op).

Everything else — codestream/packet parsing (T2), the streaming plan, DWT
orchestration, and the weft scheduler — is Rust. The only public surface is
the `extern "C"` API; every internal module is `pub(crate)`.

## Architecture

### The weft engine (`src/weft/`)

The decode pipeline is a small static graph of **nodes** connected by
**bounded SPSC rings**, driven by a minimal worker pool. Three primitives,
nothing else:

| Primitive | Module | Role |
|-----------|--------|------|
| `ring` | `weft/ring.rs` | SPSC slot ring — pre-allocated slots, zero-copy hand-off, capacity **is** the backpressure ("pull") |
| `Node` + `NodeState` | `weft/node.rs` | Non-blocking slices + the 2-bit SCHEDULED/DIRTY wakeup word (no lost wakeups, at most one slice of a node runs at a time) |
| `Runtime` | `weft/rt.rs` | Shared run queue, parked workers, and tail-chaining (a finishing producer runs its consumer directly, keeping rows in cache) |

A node's `shuttle()` makes as much progress as its input rings and output
space allow, then **returns** — it never blocks, never waits, never runs
foreign work. "I'm stuck" lives in the node's own state, not on a thread's
stack. Bounded rings are the demand: a producer can only run when its output
ring has space, so packets are parsed and codeblocks decoded exactly as fast
as downstream consumes. The no-blocking invariant plus sufficiency-sized
rings make deadlock unrepresentable.

Because at most one slice of a node runs at a time, node internals are
single-threaded (no locks inside nodes) and every ring is genuinely SPSC.

### The decode graph (`src/decode/graph.rs`)

```
[SubbandDecode c0 r0/LL]  ──ring──► [Level 0]
[SubbandDecode c0 r1/HL..HH] ─ring─►    │ LL ring
                                     [Level 1] ··· [Level top c0] ─┐
[SubbandDecode c1 ...]  ─ ─ ─ ─ ─ ─►  ···  ···  [Level top c1] ───┼─► [Merge]
```

- **Plan** ([decode/plan.rs](src/decode/plan.rs)) — one streaming pass over
  the codestream's packet headers builds, per tile and subband, a table of
  every code-block's compressed bytes (absolute file offset + length) and
  decode parameters. Headers are parsed through a small sliding window;
  block bodies are seeked over, never read. Handles all five progression orders, multi-layer, multi-tile, +multi-tile-part, multi-component, SOP/EPH. Unsupported features reject at plan time rather than mis-decode, and the host falls back to its own pipeline.
- **SubbandDecode nodes** (T1) — read block bytes on demand via `pread` and
  decode one block-row (up to 64 subband rows) per pass directly into ring
  slots, calling the **host-supplied** tier-1 coder
  ([decode/stripe_decoder.rs](src/decode/stripe_decoder.rs) `BlockCoder`),
  fused with dequantization. Full-resolution bands are column-sliced across
  several nodes for parallelism.
- **Level nodes** (DWT) — one per component per resolution level, each
  owning an incremental `Synthesis` engine ([src/dwt/](src/dwt/)). A
  level consumes leaf subband rows from the T1 rings plus LL rows from
  the level below through a small ring, and emits synthesized rows to the
  level above (the top level emits full-width tile rows). Vertical lifting
  keeps only the analytically minimal rolling window of rows; the SIMD inner
  loops dispatch through [ffi_dwt.rs](src/ffi_dwt.rs) to the Highway kernels.
- **Merge sink** — joins components (and tile columns), applies the inverse
  RCT/ICT when signalled, and hands finished image rows to the host via the
  row callback.

Multi-tile images decode one tile *row* at a time: all tiles of a tile row
run concurrently, tile rows run in sequence with a fresh graph each, so peak
memory is one tile row's rings.

Three sample paths are chosen per image: `I16` (reversible, coefficients fit
15 bits + sign), `I32` (reversible high-precision), and `F32` (irreversible
9/7, normalized floats with FMA inverse ICT).

### Module reference

| Module | Description |
|--------|-------------|
| [capi.rs](src/capi.rs) | The `extern "C"` embedding API (plan/decode handles, read_at + host-T1 + row-callback FFI surfaces; header in [include/mercury_capi.h](include/mercury_capi.h)) |
| [codec/](src/codec/) | Codestream/JP2 parsing: markers, main header, packet headers (tag trees, bit reader), tile geometry, JP2 codestream location |
| [decode/](src/decode/) | The T2 plan, the weft decode graph, and the code-block stripe decoder (driving the host's T1 through the `BlockCoder` seam) |
| [dwt/](src/dwt/) | Incremental inverse-DWT: per-level `Synthesis` engines, level geometry builder, horizontal/vertical lifting |
| [weft/](src/weft/) | The dataflow runtime: rings, nodes, worker pool |
| [ffi_dwt.rs](src/ffi_dwt.rs) | `extern "C"` bindings to the Highway SIMD lifting kernels |
| [native/](native/) | The vendored C++ DWT kernels + bridge, compiled by the host against its own Highway (see [native/README.md](native/README.md)) |

## The C API

Grok drives the engine through [include/mercury_capi.h](include/mercury_capi.h):

1. `mercury_warp_loom_fd(fd, …)` / `mercury_warp_loom(read_at, …)` — parse the
   codestream headers and build the streaming plan. Returns `NULL` (with a
   reason string) if the stream is ineligible, so the host falls back to its
   own pipeline.
2. `mercury_plan_info` / `mercury_plan_comp_info` — query image geometry so
   the host can allocate output planes.
3. `mercury_weave(plan, t1_fn, row_fn, …)` — decode, calling `t1_fn` per
   code-block and `row_fn` for each finished full-width row (in order).
   A null `t1_fn` is rejected — the host must supply tier-1.

[examples/capi_smoke.c](examples/capi_smoke.c) exercises all three surfaces.

## Building

The crate builds only as the Grok-embedded static library — it produces no C++
and relies on the host to compile the kernels and supply T1:

```bash
cargo build --lib --release --no-default-features --features extern-kernels
```

This yields `target/release/libmercury.a` (0 kernel symbols; they resolve at
Grok's final link). Grok's CMake compiles [native/](native/) against its own
Highway and links this archive.

## Performance

Gate image: ESP_028011_2055_RED.JP2 (HiRISE, 28260×52834, 10-bit, single
tile, 9 levels, ~3 GB decoded):

- Decode gate (design target: ≤ 10 s wall, ≤ 47 MB RSS): met.
- Through Grok's CLI with the streaming row callback, the fast path decodes
  ESP in ~10.9 s at ~63 MB peak RSS, versus ~14.4 s / multi-GB for the
  classic whole-image pipeline.
- Fast-path output is byte-identical to the classic pipeline on the
  reversible corpus (5/3, all precisions), across multi-tile, multi-component
  (RCT), and subsampling-free images.
- Irreversible 9/7 matches OpenJPEG within final rounding (≤ 1 LSB, i.e.
  spec-correct); different valid 9/7 inverse implementations disagree at the
  ±1 LSB level.

## Validation

This crate has no runnable standalone test harness — it links into Grok, so
end-to-end decode is validated through **Grok's own test suite** (run with
`GRK_MERCURY=1`) and A/B'd against Grok's classic pipeline and OpenJPEG. The
small in-crate `#[cfg(test)]` unit tests cover header/packet parsing and tag
trees.

`WEFT_DEBUG=1` enables stall-gate and synthesis-ladder diagnostics on wedged
graphs. `GRK_MERCURY_DEBUG=1` (host side) logs why a stream fell back to the
classic pipeline.

## Scope

- Full-resolution, full-image decode only. No `-reduce` / decode-window /
  region decode (the host handles those on its classic path).
- Rejected at plan time (returned as errors, never mis-decoded, so the host
  falls back): COC/POC markers, derived quantization, component subsampling,
  precincts smaller than code-blocks, HT/BYPASS/RESTART code-block modes.
- Decode-only; the host owns compression, color management, and output.
