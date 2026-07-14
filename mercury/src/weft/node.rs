//! The weft node contract: non-blocking slices plus the SCHEDULED/DIRTY
//! wakeup word.
//!
//! [`Node::shuttle`] progresses as far as input rings and output-ring space
//! allow, then returns. It never blocks, waits, or runs foreign work; "I'm
//! stuck" is a return, with the resume point held in node state. This is what
//! makes the engine deadlock-free by construction (docs/weft-design.md §3) —
//! hence no shed/wwid/working-wait machinery.
//!
//! [`NodeState`] is the entire wakeup protocol. Guarantees:
//! - **no lost wakeup** — a tug during a pass sets DIRTY, forcing another
//!   pass before the runner exits;
//! - **exclusive execution** — SCHEDULED is won by exactly one notifier, so
//!   at most one slice of a node runs at a time. Node internals thus need no
//!   synchronization, and every ring is genuinely SPSC.
//!
//! Mirrored with loom atomics and exhaustively model-checked in
//! tests/weft_loom.rs; keep the two in sync.

use std::sync::atomic::{AtomicU32, Ordering};

use super::rt::Ctx;

/// A stage instance in the weft graph.
///
/// Implementations own their ring endpoints and peer [`NodeId`]s (assigned at
/// graph build), and call [`Ctx::tug`] after publishing to or releasing
/// from a ring.
///
/// [`NodeId`]: super::rt::NodeId
pub trait Node: Send {
    /// Run one slice: consume available input, produce into free output
    /// space, tug peers, return when no further progress is possible.
    /// Must not block.
    fn shuttle(&mut self, ctx: &mut Ctx<'_>);
}

const SCHEDULED: u32 = 1;
const DIRTY: u32 = 2;

/// Per-node wakeup word. See module docs for the protocol.
pub struct NodeState(AtomicU32);

impl Default for NodeState {
    fn default() -> Self {
        Self::warp()
    }
}

impl NodeState {
    pub fn warp() -> Self {
        NodeState(AtomicU32::new(0))
    }

    /// Signal that the node may be able to progress (rows published to it,
    /// or slots freed for it).
    ///
    /// Returns `true` iff the caller won the SCHEDULED bit and must get the
    /// node run (spool or tail-chain). Returns `false` if already scheduled
    /// — the running or queued pass will observe DIRTY and re-run.
    pub fn tug(&self) -> bool {
        let mut s = self.0.load(Ordering::Relaxed);
        loop {
            if s & SCHEDULED != 0 {
                if s & DIRTY != 0 {
                    return false; // already scheduled and already dirty
                }
                match self.0.compare_exchange_weak(
                    s,
                    s | DIRTY,
                    Ordering::AcqRel,
                    Ordering::Relaxed,
                ) {
                    Ok(_) => return false,
                    Err(cur) => s = cur,
                }
            } else {
                debug_assert_eq!(s & DIRTY, 0, "DIRTY without SCHEDULED");
                match self.0.compare_exchange_weak(
                    s,
                    SCHEDULED,
                    Ordering::AcqRel,
                    Ordering::Relaxed,
                ) {
                    Ok(_) => return true,
                    Err(cur) => s = cur,
                }
            }
        }
    }

    /// Called by the runner immediately before each `shuttle` pass.
    pub fn open_shed(&self) {
        let prev = self.0.fetch_and(!DIRTY, Ordering::AcqRel);
        debug_assert!(prev & SCHEDULED != 0, "pass on unscheduled node");
    }

    /// Called by the runner after a pass. Returns `true` if SCHEDULED was
    /// cleared and the runner may exit; `false` if a tug raced in during
    /// the pass (DIRTY) — run another pass.
    pub fn try_tie_off(&self) -> bool {
        let mut s = self.0.load(Ordering::Relaxed);
        loop {
            debug_assert!(s & SCHEDULED != 0, "finish on unscheduled node");
            if s & DIRTY != 0 {
                return false;
            }
            match self.0.compare_exchange_weak(
                s,
                s & !SCHEDULED,
                Ordering::AcqRel,
                Ordering::Relaxed,
            ) {
                Ok(_) => return true,
                Err(cur) => s = cur,
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn notify_wins_only_when_idle() {
        let st = NodeState::warp();
        assert!(st.tug(), "first tug wins scheduling");
        assert!(!st.tug(), "second tug only sets DIRTY");
        assert!(!st.tug(), "third tug is a no-op");
    }

    #[test]
    fn dirty_forces_rerun() {
        let st = NodeState::warp();
        assert!(st.tug());
        st.open_shed();
        assert!(!st.tug(), "tug during pass sets DIRTY");
        assert!(!st.try_tie_off(), "DIRTY blocks finishing");
        st.open_shed(); // second pass clears DIRTY
        assert!(st.try_tie_off(), "clean pass finishes");
        assert!(st.tug(), "after finish, tug wins again");
    }
}
