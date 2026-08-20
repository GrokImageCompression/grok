//! Loom model of the SCHEDULED/DIRTY wakeup protocol.
//!
//! This drives the real [`NodeState`] (node.rs compiles onto loom's atomics
//! under `--cfg loom`), the pass loop copied from `rt::Inner::ply_node`, and a
//! stand-in for a ring: `published` carries ring.rs's tail ordering, a Release
//! store read back with Acquire. What is checked is that a producer's row can
//! never end up published with every pass over and nobody scheduled to look at
//! it, and that two passes of one node never overlap.

// std's Arc, not loom's: the refcount is not part of the protocol and every
// loom atomic in the model multiplies the interleavings to explore
use std::sync::Arc;

use loom::cell::UnsafeCell;
use loom::sync::atomic::{AtomicUsize, Ordering};

use super::node::NodeState;

struct ModeledNode {
    state: NodeState,
    /// Rows handed over by producers, as ring.rs's tail.
    published: AtomicUsize,
    /// Rows a pass has taken. Only the holder of SCHEDULED may touch it, so
    /// loom reports any two passes that overlap.
    consumed: UnsafeCell<usize>,
}

impl ModeledNode {
    fn warp() -> Self {
        ModeledNode {
            state: NodeState::warp(),
            published: AtomicUsize::new(0),
            consumed: UnsafeCell::new(0),
        }
    }
}

/// One row published to `node`, then the tug that rt.rs's `Ctx::tug` does.
/// The winner runs the node here, standing in for both the tail-chain and the
/// queue hand-off, which differ only in which thread runs the pass.
fn publish_and_tug(node: &ModeledNode) {
    node.published.fetch_add(1, Ordering::Release);
    if node.state.tug() {
        ply_node(node);
    }
}

fn ply_node(node: &ModeledNode) {
    loop {
        node.state.open_shed();
        let ready = node.published.load(Ordering::Acquire);
        node.consumed.with_mut(|slot| unsafe { *slot = ready });
        if node.state.try_tie_off() {
            break;
        }
    }
}

fn spawn_publisher(node: &Arc<ModeledNode>, rows: usize) -> loom::thread::JoinHandle<()> {
    let node = Arc::clone(node);
    loom::thread::spawn(move || {
        for _ in 0..rows {
            publish_and_tug(&node);
        }
    })
}

/// Three rows over two producers. Two rows from one thread is the shape the
/// lost wakeup needed: the second tug arrives while DIRTY is still set from
/// the first, which is the case tug() used to answer from a relaxed load
/// instead of the CAS.
#[test]
fn every_published_row_gets_a_pass() {
    loom::model(|| {
        let node = Arc::new(ModeledNode::warp());
        let first = spawn_publisher(&node, 2);
        let second = spawn_publisher(&node, 1);
        first.join().unwrap();
        second.join().unwrap();
        // no thread holds SCHEDULED once both have joined, so the last pass
        // to run must have seen every row
        node.consumed.with(|slot| {
            assert_eq!(
                unsafe { *slot },
                3,
                "row published with no pass left to see it"
            );
        });
    });
}

/// A row published from outside the pool (`Runtime::tug`) while a pass is
/// already running, with a second worker able to win the node next.
#[test]
fn tug_during_pass_forces_another_pass() {
    loom::model(|| {
        let node = Arc::new(ModeledNode::warp());
        assert!(node.state.tug(), "first tug wins the node");
        let runner = {
            let node = Arc::clone(&node);
            loom::thread::spawn(move || ply_node(&node))
        };
        let producer = spawn_publisher(&node, 1);
        runner.join().unwrap();
        producer.join().unwrap();
        node.consumed.with(|slot| {
            assert_eq!(
                unsafe { *slot },
                1,
                "row published with no pass left to see it"
            );
        });
    });
}
