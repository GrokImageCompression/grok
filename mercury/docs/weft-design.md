# Weft: a deadlock-free pull-based line decoder for MQ + DWT

**Weft** (the crosswise thread the shuttle carries through the warp) is the
dataflow engine mercury's decode graph runs on. Its two defining properties:

- **demand-driven laziness** with **O(support-window) memory** — nothing is
  ever buffered proportional to image height.
- **deadlock impossible by construction** — not representable in the execution model.


---

## 1. Core idea: bounded dataflow of non-blocking slices

- The pipeline is a small static graph of **nodes** (stage instances)
  connected by **bounded SPSC rings** of lines/stripes.
- A node's unit of execution is a **slice**: `shuttle()` makes as much progress
  as its inputs and output space allow, then **returns**. A slice never blocks,
  never waits, never runs foreign work. "I'm stuck" is expressed by returning,
  with the node's resume point held in its own state.
- **Events reschedule nodes.** When a producer publishes rows it notifies the
  consumer node; when a consumer frees ring slots it notifies the producer. The
  notification protocol (§3) guarantees no lost wakeups and **at most one slice
  of a given node runs at a time** — so node internals are single-threaded (no
  locks inside nodes) and every ring is genuinely SPSC.
- **Demand = ring credit.** A producer can only run when its output ring has
  space, so bounded rings *are* the pull: work is generated exactly as fast as
  downstream consumes. T2 packet parsing stays throttled by T1 stripe
  scheduling.
- **Only two things ever block a thread:** an idle worker parking on the idle
  list, and the caller parking on the sink ring. Neither holds any work.

Parallelism comes from fan-out inside T1 (bands are column-sliced into
independent nodes) and concurrency across nodes (all subbands, components, and
synthesis stages pipeline against each other).

This is a Kahn process network with statically sufficient buffer bounds — the
classic construction under which bounded-memory dataflow cannot deadlock.

---

## 2. Node taxonomy and graph shape

Per component with R resolution levels:

```
codestream ──► [T2 feeder] ──► SubbandDecode(HLₗ/LHₗ/HHₗ)  (one node per subband)
                                     │  ring: K stripes × stripe_height lines
                                     ▼
              Level nodes (one per resolution level, L0…L_top)
                                     │  ring: small (LL / sink window)
                                     ▼
              Merge (MCT / YCC→RGB / interleave)   [only if multi-component]
                                     │  ring: N output rows
                                     ▼
              Sink — host row callback
```

- **T2 feeder** — the streaming plan ([`decode::plan`](../src/decode/plan.rs))
  parses packet headers through a small sliding window and records each
  code-block's byte range + parameters; block bodies are `pread` on demand, so
  the file never enters memory.
- **SubbandDecode** ([`decode::stripe_decoder`](../src/decode/stripe_decoder.rs))
  — decodes one block-row per slice into ring slots via the host-supplied
  tier-1 coder (the `BlockCoder` seam), fused with dequantization. Full-res
  bands are column-sliced across several nodes for parallelism.
- **Level nodes** ([`dwt`](../src/dwt/)) — one per component per resolution
  level, each an incremental `Synthesis` engine. A level consumes leaf subband
  rows plus the LL rows of the level below, runs horizontal synthesis on each
  arriving row, runs every vertical lifting step that becomes enabled, and
  emits synthesized rows while the output ring has space. The vertical lifting
  window is the analytically minimal rolling set of rows.
- **Merge/Sink** — ordered by construction (single consumer, FIFO rings), so
  output rows arrive in order with no re-sequencing.

Multi-tile images decode one tile *row* at a time: all tiles of a tile row run
concurrently in a fresh graph; tile rows run in sequence, so peak memory is one
tile row's rings.

---

## 3. Deadlock-freedom argument

Waits-for edges exist only between ring-adjacent nodes: a producer waits for a
slot (freed by its consumer), a consumer waits for data (published by its
producer). Two facts close the argument:

1. **A ring cannot be simultaneously "full" for its producer and "empty" for
   its consumer.** Full ⇒ the consumer has input available; empty ⇒ the
   producer has space. So along any producer→…→sink chain, if node *i* is
   stalled on a full output ring, node *i+1* has data; by induction the stall
   chain terminates at the sink, which the caller drains unconditionally.
   Symmetrically, a stall chain toward the leaves terminates at the codestream,
   which is always readable. Therefore, while the image is incomplete, at least
   one node is runnable.
2. **Multi-input nodes need sufficient ring capacity.** A vertical-lifting node
   needs a *window* of rows before it can emit; if a ring were smaller than the
   window, both facts above could hold and progress still stall. So every ring
   is sized ≥ the maximum simultaneous requirement of its consumer, computed
   from the lifting schedule when the graph is built.

Runtime consequence: a cheap "all workers idle but image incomplete" assertion in debug builds nets ring-sizing bugs; it should be unreachable.

---

## 4. Node state machine (the whole scheduler contract)

```
node.state : AtomicU32     // bit0 SCHEDULED (queued or running), bit1 DIRTY

tug(node):                             // called on beat / slot-free
  loop:
    s = state.load()
    if s & SCHEDULED:
        if s & DIRTY: return           // someone will re-run it
        if CAS(s, s|DIRTY): return
    else:
        if CAS(s, SCHEDULED): spool(node); return

worker runs node:
  loop:
    state.fetch_and(!DIRTY)
    node.shuttle()                     // never blocks
    s = state.load()
    if s & DIRTY: continue             // event raced in; go again
    if CAS(s, s & !SCHEDULED): break   // clean exit; next tug re-spools
```

Properties: no lost wakeups (an event during `shuttle()` sets DIRTY and forces
a re-run), at most one slice per node in flight (SCHEDULED is exclusive), and
the whole protocol is ~30 lines — small enough to exhaustively model-check with
the `loom` crate.

Rings are SPSC head/tail atomics (Release on beat, Acquire on peek).
`beat(n)` → `tug(consumer)`; `unwind(n)` → `tug(producer)`.

---

## 5. Scheduling and cache locality

- **Tail-chaining.** When a slice publishes rows and its notify wins the
  SCHEDULED bit for the consumer, the finishing worker runs the consumer slice
  *directly* (depth-capped) instead of enqueueing — a stripe's samples are
  still in L2 when horizontal synthesis reads them.
- **Sticky affinity.** Each node prefers its last worker's run queue, keeping a
  level node's working set (a few rows per level) in one core's cache.
- **Slice granularity.** SubbandDecode schedules whole stripes; level nodes
  emit rows in batches (up to ring space) per slice, so a slice always does
  µs-scale work.

No priorities are needed: bounded rings give automatic backpressure ordering.

---

## 6. Memory model

The engine holds, at any instant: K stripes per active subband ring (K small,
e.g. 2), the minimal vertical-lifting window per level, one small sink/LL ring,
and in-flight compressed block buffers + packet metadata. Nothing scales with
image height.

Sample width is chosen per image: `I16` (reversible, coefficients fit 15 bits +
sign), `I32` (reversible high-precision), `F32` (irreversible 9/7). The DWT
lifting kernels are Highway SIMD (dispatched through
[`ffi_dwt`](../src/ffi_dwt.rs)); the tier-1 coder is supplied by the host codec
across the C API.

---

## 7. Risks

- **The synthesis control-flow is incremental, not demand-recursive.** Its
  resume state lives in per-step counters/queues; golden tests compare it
  row-for-row against a reference pull engine.
- **Ring-sizing sufficiency is load-bearing.** A too-small ring turns into a
  progress-free stall. Mitigated by computing sizes from the lifting schedule,
  the debug all-idle assertion, and K=1 stress runs.
- **Lost-wakeup class bugs** in tug/ring — mitigated by `loom` model
  checking.
- **Tail-chaining stack depth** — capped; beyond the cap, spool normally.
