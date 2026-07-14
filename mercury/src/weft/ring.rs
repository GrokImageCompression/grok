//! SPSC ring of pre-allocated slots — the edge type of the weft graph.
//!
//! Connects exactly one producer node to one consumer node. Slots are
//! allocated once at graph-build time; the producer fills unpublished slots
//! in place and beats them across, the consumer reads a *window* of published
//! slots (vertical lifting needs several rows at once) and unwinds the
//! oldest when done. Nothing is copied at the boundary.
//!
//! Per-endpoint single-threaded access is what makes this safe, guaranteed by
//! the scheduler: at most one slice of a node runs at a time (see
//! [`super::node::NodeState`]), and split [`Producer`]/[`Consumer`] handles
//! give each side exactly one owner.
//!
//! The ring carries no notification logic: a node beats/unwinds then tugs the
//! peer via [`super::rt::Ctx::tug`]; capacity is the backpressure (the "pull")
//! — docs/weft-design.md §1.

use std::cell::UnsafeCell;
use std::sync::Arc;
use std::sync::atomic::{AtomicUsize, Ordering};

struct Shared<T> {
    slots: Box<[UnsafeCell<T>]>,
    /// Slots unwound by the consumer, monotonic count.
    head: AtomicUsize,
    /// Slots beaten across by the producer, monotonic count.
    tail: AtomicUsize,
}

// Safety: the producer only touches slots in [tail, head + capacity) and the
// consumer only slots in [head, tail); the atomic head/tail hand-off
// (Release store, Acquire load) orders the data written in a slot before the
// peer's access to it. Each handle is a single owner.
unsafe impl<T: Send> Sync for Shared<T> {}
unsafe impl<T: Send> Send for Shared<T> {}

/// Write end of a ring. Owned by exactly one node.
pub struct Producer<T> {
    shared: Arc<Shared<T>>,
}

/// Read end of a ring. Owned by exactly one node.
pub struct Consumer<T> {
    shared: Arc<Shared<T>>,
}

/// Spin a ring whose capacity and initial slot contents are `slots`.
///
/// The slots are pre-allocated buffers (e.g. subband lines), reused for the
/// ring's lifetime.
pub fn spin<T>(slots: Vec<T>) -> (Producer<T>, Consumer<T>) {
    assert!(!slots.is_empty(), "ring capacity must be at least 1");
    let shared = Arc::new(Shared {
        slots: slots.into_iter().map(UnsafeCell::new).collect(),
        head: AtomicUsize::new(0),
        tail: AtomicUsize::new(0),
    });
    (
        Producer {
            shared: Arc::clone(&shared),
        },
        Consumer { shared },
    )
}

impl<T> Producer<T> {
    pub fn span(&self) -> usize {
        self.shared.slots.len()
    }

    /// Number of slots the producer may currently fill.
    pub fn slack(&self) -> usize {
        // tail is owned by this side; head needs Acquire so slot reuse
        // happens-after the consumer's reads of the unwound slots.
        let tail = self.shared.tail.load(Ordering::Relaxed);
        let head = self.shared.head.load(Ordering::Acquire);
        self.span() - (tail - head)
    }

    /// Access the `i`-th unpublished slot (`i < slack()`), to fill in place.
    pub fn pick(&mut self, i: usize) -> &mut T {
        debug_assert!(i < self.slack(), "pick index beyond free space");
        let tail = self.shared.tail.load(Ordering::Relaxed);
        let idx = (tail + i) % self.span();
        // Safety: slot is unpublished (>= tail) and unreleased space
        // (< head + capacity), so the consumer cannot touch it; &mut self
        // prevents aliasing on this side.
        unsafe { &mut *self.shared.slots[idx].get() }
    }

    /// Beat the first `n` filled slots across to the consumer.
    pub fn beat(&mut self, n: usize) {
        debug_assert!(n <= self.slack(), "beating more slots than are free");
        let tail = self.shared.tail.load(Ordering::Relaxed);
        // Release: slot contents written above become visible to the
        // consumer's Acquire load of tail.
        self.shared.tail.store(tail + n, Ordering::Release);
    }
}

impl<T> Consumer<T> {
    pub fn span(&self) -> usize {
        self.shared.slots.len()
    }

    /// Number of beaten-across slots available to read.
    pub fn picks_ready(&self) -> usize {
        let head = self.shared.head.load(Ordering::Relaxed);
        let tail = self.shared.tail.load(Ordering::Acquire);
        tail - head
    }

    /// Peek the `i`-th available slot (`i < picks_ready()`). `i = 0` is the
    /// oldest; a lifting window peeks several before unwinding any.
    pub fn peek(&self, i: usize) -> &T {
        debug_assert!(i < self.picks_ready(), "peek index beyond available");
        let head = self.shared.head.load(Ordering::Relaxed);
        let idx = (head + i) % self.span();
        // Safety: slot is published and not yet released, so the producer
        // cannot touch it.
        unsafe { &*self.shared.slots[idx].get() }
    }

    /// Unwind the `n` oldest slots back to the producer for reuse.
    pub fn unwind(&mut self, n: usize) {
        debug_assert!(n <= self.picks_ready(), "unwinding more than available");
        let head = self.shared.head.load(Ordering::Relaxed);
        // Release: reads of the slot contents above happen-before the
        // producer's Acquire load of head when it reuses the slot.
        self.shared.head.store(head + n, Ordering::Release);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fill_drain_wraparound() {
        let (mut p, mut c) = spin(vec![0i64; 4]);
        assert_eq!(p.slack(), 4);
        assert_eq!(c.picks_ready(), 0);

        let mut next = 0i64;
        let mut expected = 0i64;
        // Push/pop in ragged batches so head/tail wrap several times.
        for round in 0..50 {
            let n = 1 + (round % 4).min(p.slack());
            for i in 0..n {
                *p.pick(i) = next + i as i64;
            }
            p.beat(n);
            next += n as i64;

            assert_eq!(c.picks_ready(), n);
            // Window peek before any unwind.
            for i in 0..n {
                assert_eq!(*c.peek(i), expected + i as i64);
            }
            c.unwind(n);
            expected += n as i64;
            assert_eq!(p.slack(), 4);
        }
    }

    #[test]
    fn window_peek_partial_release() {
        let (mut p, mut c) = spin(vec![0u32; 6]);
        for i in 0..5 {
            *p.pick(i) = i as u32;
        }
        p.beat(5);
        // Read a 3-slot window, unwind only the oldest (vlift pattern).
        assert_eq!((*c.peek(0), *c.peek(1), *c.peek(2)), (0, 1, 2));
        c.unwind(1);
        assert_eq!(p.slack(), 2);
        assert_eq!(*c.peek(0), 1);
    }

    #[test]
    fn two_thread_stress() {
        const N: u64 = 200_000;
        let (mut p, mut c) = spin(vec![0u64; 16]);
        let producer = std::thread::spawn(move || {
            let mut sent = 0u64;
            while sent < N {
                let n = p.slack().min((N - sent) as usize);
                if n == 0 {
                    std::hint::spin_loop();
                    continue;
                }
                for i in 0..n {
                    *p.pick(i) = sent + i as u64;
                }
                p.beat(n);
                sent += n as u64;
            }
        });
        let mut sum = 0u64;
        let mut got = 0u64;
        while got < N {
            let n = c.picks_ready();
            if n == 0 {
                std::hint::spin_loop();
                continue;
            }
            for i in 0..n {
                sum += *c.peek(i);
            }
            c.unwind(n);
            got += n as u64;
        }
        producer.join().unwrap();
        assert_eq!(sum, N * (N - 1) / 2);
    }
}
