//! The weft runtime: graph builder, worker pool, and tail-chaining.
//!
//! Deliberately minimal. Since no weft job blocks (see [`super::node`]), the
//! pool needs no heddle/shed sophistication: a shared run queue, a plain
//! outstanding-pass counter, and parked workers suffice. The *hot* path — a
//! producer finishing a slice and its consumer running next — bypasses the
//! queue via tail-chaining, keeping the just-written rows in the finishing
//! core's cache (docs/weft-design.md §5).
//!
//! Per-worker stealable deques and sticky node affinity could slot in behind
//! the same `Ctx::tug` seam if ever needed; the decode graph reached its
//! perf gate without.

use std::cell::UnsafeCell;
use std::collections::VecDeque;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::{Arc, Condvar, Mutex};
use std::thread::JoinHandle;

use super::node::{Node, NodeState};

/// Index of a node in the graph. Assigned by [`Builder::mount`] in call order,
/// so a graph can be wired with ids known up front.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct NodeId(u32);

impl NodeId {
    /// Id that a future `Builder::mount` will return, so peers can be wired
    /// before the nodes are constructed.
    pub fn heddle(i: u32) -> Self {
        NodeId(i)
    }

}

/// How deep tail-chaining may recurse before falling back to the queue.
/// Bounds worker stack growth; depth 2 covers the hot producer→consumer hop
/// plus one more (e.g. T1 stripe publish → synthesis → sink).
const CHAIN_DEPTH_CAP: u8 = 2;

pub struct Builder {
    nodes: Vec<Box<dyn Node>>,
}

impl Default for Builder {
    fn default() -> Self {
        Self::warp()
    }
}

impl Builder {
    pub fn warp() -> Self {
        Builder { nodes: Vec::new() }
    }

    /// Add a node; ids are assigned sequentially from 0.
    pub fn mount(&mut self, node: Box<dyn Node>) -> NodeId {
        self.nodes.push(node);
        NodeId(self.nodes.len() as u32 - 1)
    }

    /// Spawn `workers` (≥ 1) threads and return the running runtime.
    /// Nothing executes until a node is notified.
    pub fn dress(self, workers: usize) -> Runtime {
        assert!(workers >= 1, "weft runtime needs at least one worker");
        let inner = Arc::new(Inner {
            slots: self
                .nodes
                .into_iter()
                .map(|n| Slot {
                    state: NodeState::warp(),
                    node: UnsafeCell::new(n),
                })
                .collect(),
            queue: Mutex::new(VecDeque::new()),
            work_cv: Condvar::new(),
            done_cv: Condvar::new(),
            outstanding: AtomicUsize::new(0),
            shutdown: AtomicBool::new(false),
        });
        let threads = (0..workers)
            .map(|i| {
                let inner = Arc::clone(&inner);
                std::thread::Builder::new()
                    .name(format!("weft-{i}"))
                    .spawn(move || inner.treadle_loop())
                    .expect("spawn weft worker")
            })
            .collect();
        Runtime { inner, threads }
    }
}

struct Slot {
    state: NodeState,
    node: UnsafeCell<Box<dyn Node>>,
}

// Safety: `node` is only accessed by the thread that currently holds the
// node's SCHEDULED bit, which NodeState grants to exactly one owner at a
// time; `Node: Send` lets that owner move between passes across threads.
unsafe impl Sync for Slot {}

struct Inner {
    slots: Box<[Slot]>,
    queue: Mutex<VecDeque<NodeId>>,
    work_cv: Condvar,
    done_cv: Condvar,
    /// Nodes currently scheduled (queued, chained, or running). Zero means
    /// the graph is quiescent.
    outstanding: AtomicUsize,
    shutdown: AtomicBool,
}

/// Slice context: how a node notifies its ring peers.
pub struct Ctx<'a> {
    rt: &'a Inner,
    /// Tail-chain candidate claimed during this pass.
    chain: Option<NodeId>,
    depth: u8,
}

impl Ctx<'_> {
    /// Wake `id`, which may now be able to progress (rows published to it or
    /// slots freed for it). Cheap and idempotent; safe to call after any
    /// publish/release.
    pub fn tug(&mut self, id: NodeId) {
        if self.rt.slots[id.0 as usize].state.tug() {
            self.rt.outstanding.fetch_add(1, Ordering::AcqRel);
            if self.depth < CHAIN_DEPTH_CAP && self.chain.is_none() {
                // Run it on this worker right after the current pass, while
                // the rows just handed over are still cache-hot.
                self.chain = Some(id);
            } else {
                self.rt.spool(id);
            }
        }
    }
}

impl Inner {
    fn spool(&self, id: NodeId) {
        let mut q = self.queue.lock().unwrap();
        q.push_back(id);
        drop(q);
        self.work_cv.notify_one();
    }

    fn treadle_loop(&self) {
        loop {
            let id = {
                let mut q = self.queue.lock().unwrap();
                loop {
                    if self.shutdown.load(Ordering::Relaxed) {
                        return;
                    }
                    if let Some(id) = q.pop_front() {
                        break id;
                    }
                    q = self.work_cv.wait(q).unwrap();
                }
            };
            // A panicking node would leave `outstanding` forever nonzero and
            // hang await_stillness(); fail loudly instead.
            if std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                self.ply_node(id, 0)
            }))
            .is_err()
            {
                eprintln!("weft: node {} panicked; aborting", id.0);
                std::process::abort();
            }
        }
    }

    /// Run passes of `id` until it finishes cleanly, executing at most one
    /// tail-chained successor per pass. Decrements `outstanding` on exit.
    fn ply_node(&self, id: NodeId, depth: u8) {
        let slot = &self.slots[id.0 as usize];
        loop {
            slot.state.open_shed();
            let mut ctx = Ctx {
                rt: self,
                chain: None,
                depth,
            };
            // Safety: we hold this node's SCHEDULED bit (see Slot).
            let node = unsafe { &mut *slot.node.get() };
            node.shuttle(&mut ctx);
            let chained = ctx.chain.take();
            if let Some(next) = chained {
                self.ply_node(next, depth + 1);
            }
            if slot.state.try_tie_off() {
                break;
            }
        }
        if self.outstanding.fetch_sub(1, Ordering::AcqRel) == 1 {
            let _guard = self.queue.lock().unwrap();
            self.done_cv.notify_all();
        }
    }
}

pub struct Runtime {
    inner: Arc<Inner>,
    threads: Vec<JoinHandle<()>>,
}

impl Runtime {
    /// External tug (e.g. the initial kick of source nodes, or the sink
    /// consumer returning slots from outside the pool).
    pub fn tug(&self, id: NodeId) {
        if self.inner.slots[id.0 as usize].state.tug() {
            self.inner.outstanding.fetch_add(1, Ordering::AcqRel);
            self.inner.spool(id);
        }
    }

    /// Block the calling (non-worker) thread until the graph is quiescent:
    /// no node scheduled, queued, or running. With correctly sized rings,
    /// the work kicked off so far is then complete.
    pub fn await_stillness(&self) {
        let mut q = self.inner.queue.lock().unwrap();
        while self.inner.outstanding.load(Ordering::Acquire) != 0 {
            q = self.inner.done_cv.wait(q).unwrap();
        }
    }
}

impl Drop for Runtime {
    fn drop(&mut self) {
        self.inner.shutdown.store(true, Ordering::Relaxed);
        {
            let _guard = self.inner.queue.lock().unwrap();
            self.inner.work_cv.notify_all();
        }
        for t in self.threads.drain(..) {
            let _ = t.join();
        }
    }
}
