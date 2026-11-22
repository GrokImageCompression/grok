//! SPSC ring of pre-allocated slots — the edge type of the weft graph.
//!
//! Connects exactly one producer node to one consumer node. Slots are
//! allocated once, at graph-build time or, for a block-row ring, when its
//! first block row is written; the producer fills unpublished slots
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
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::{Arc, Mutex, OnceLock};

struct Shared<T> {
    slots: OnceLock<Box<[UnsafeCell<T>]>>,
    span: usize,
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

impl<T> Shared<T> {
    fn slots(&self) -> &[UnsafeCell<T>] {
        self.slots
            .get()
            .expect("ring slots are made before the first row is published")
    }
}

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
        span: slots.len(),
        slots: OnceLock::from(slots.into_iter().map(UnsafeCell::new).collect::<Box<[_]>>()),
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
        self.shared.span
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
        unsafe { &mut *self.shared.slots()[idx].get() }
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
        self.shared.span
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
        unsafe { &*self.shared.slots()[idx].get() }
    }

    pub fn peek_mut(&mut self, i: usize) -> &mut T {
        debug_assert!(i < self.picks_ready(), "peek index beyond available");
        let head = self.shared.head.load(Ordering::Relaxed);
        let idx = (head + i) % self.span();
        // Safety: as in peek, and &mut self prevents aliasing on this side.
        unsafe { &mut *self.shared.slots()[idx].get() }
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

pub type MakeSlots<T> = Box<dyn Fn() -> Vec<T> + Send + Sync>;

pub struct BlockRowProducer<T> {
    shared: Arc<Shared<T>>,
    make_slots: MakeSlots<T>,
    block_row_starts: Box<[usize]>,
    finished: Box<[AtomicBool]>,
    next_unpublished: Mutex<usize>,
}

pub fn spin_block_rows<T>(
    span: usize,
    rows_per_block_row: &[u32],
    make_slots: MakeSlots<T>,
) -> (Arc<BlockRowProducer<T>>, Consumer<T>) {
    assert!(span > 0, "ring capacity must be at least 1");
    let shared = Arc::new(Shared {
        slots: OnceLock::new(),
        span,
        head: AtomicUsize::new(0),
        tail: AtomicUsize::new(0),
    });
    let mut block_row_starts = Vec::with_capacity(rows_per_block_row.len() + 1);
    let mut start = 0usize;
    block_row_starts.push(start);
    for &rows in rows_per_block_row {
        assert!(rows as usize <= span, "a block row fits in its ring");
        start += rows as usize;
        block_row_starts.push(start);
    }
    let producer = BlockRowProducer {
        shared: Arc::clone(&shared),
        make_slots,
        block_row_starts: block_row_starts.into_boxed_slice(),
        finished: rows_per_block_row
            .iter()
            .map(|_| AtomicBool::new(false))
            .collect(),
        next_unpublished: Mutex::new(0),
    };
    (Arc::new(producer), Consumer { shared })
}

impl<T> BlockRowProducer<T> {
    pub fn block_row_fits(&self, block_row: usize) -> bool {
        let head = self.shared.head.load(Ordering::Acquire);
        self.block_row_starts[block_row + 1] <= head + self.shared.span
    }

    pub fn block_row_slot(&self, block_row: usize, row: usize) -> *mut T {
        let index = self.block_row_starts[block_row] + row;
        debug_assert!(index < self.block_row_starts[block_row + 1]);
        let slots = self.shared.slots.get_or_init(|| {
            let slots = (self.make_slots)();
            assert_eq!(slots.len(), self.shared.span, "made slots fill the ring");
            slots.into_iter().map(UnsafeCell::new).collect()
        });
        slots[index % self.shared.span].get()
    }

    pub fn finish_block_row(&self, block_row: usize) {
        self.finished[block_row].store(true, Ordering::Release);
        let mut next = self.next_unpublished.lock().unwrap();
        while *next < self.finished.len() && self.finished[*next].load(Ordering::Acquire) {
            *next += 1;
        }
        self.shared
            .tail
            .store(self.block_row_starts[*next], Ordering::Release);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn block_rows_finished_out_of_order_publish_in_order() {
        let (producer, mut consumer) = spin_block_rows(4, &[2, 2, 1], Box::new(|| vec![0u32; 4]));
        assert!(producer.block_row_fits(0) && producer.block_row_fits(1));
        assert!(!producer.block_row_fits(2));
        for (block_row, row) in [(1, 0), (1, 1), (0, 0), (0, 1)] {
            unsafe { *producer.block_row_slot(block_row, row) = (block_row * 10 + row) as u32 };
        }
        producer.finish_block_row(1);
        assert_eq!(consumer.picks_ready(), 0);
        producer.finish_block_row(0);
        assert_eq!(consumer.picks_ready(), 4);
        let rows: Vec<u32> = (0..4).map(|i| *consumer.peek(i)).collect();
        assert_eq!(rows, [0, 1, 10, 11]);
        consumer.unwind(1);
        assert!(producer.block_row_fits(2));
    }

    #[test]
    fn block_row_slots_are_made_once_on_first_write() {
        let made = Arc::new(AtomicUsize::new(0));
        let counter = Arc::clone(&made);
        let (producer, consumer) = spin_block_rows(
            2,
            &[1, 1],
            Box::new(move || {
                counter.fetch_add(1, Ordering::Relaxed);
                vec![0u32; 2]
            }),
        );
        assert!(producer.block_row_fits(1));
        assert_eq!(made.load(Ordering::Relaxed), 0);
        unsafe { *producer.block_row_slot(1, 0) = 7 };
        unsafe { *producer.block_row_slot(0, 0) = 3 };
        assert_eq!(made.load(Ordering::Relaxed), 1);
        producer.finish_block_row(0);
        producer.finish_block_row(1);
        assert_eq!((*consumer.peek(0), *consumer.peek(1)), (3, 7));
    }

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
