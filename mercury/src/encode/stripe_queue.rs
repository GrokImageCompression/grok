use std::collections::VecDeque;
use std::sync::{Arc, Condvar, Mutex};

use super::band_coder::{CodedBand, StripeLayout, code_stripe};

pub(crate) struct StripeJob {
    pub samples: Vec<f32>,
    pub rows: usize,
    pub layout: StripeLayout,
    pub results: Arc<StripeResults>,
    pub index: usize,
}

impl StripeJob {
    pub fn code(self, block: &mut Vec<i32>) -> Vec<f32> {
        let mut coded = CodedBand::default();
        code_stripe(&self.layout, &self.samples, self.rows, block, &mut coded);
        self.results.store(self.index, coded);
        self.samples
    }
}

// one band's coded stripes, filled in any order
#[derive(Default)]
pub(crate) struct StripeResults(Mutex<Vec<Option<CodedBand>>>);

impl StripeResults {
    fn store(&self, index: usize, coded: CodedBand) {
        let mut slots = self.0.lock().unwrap();
        if slots.len() <= index {
            slots.resize_with(index + 1, || None);
        }
        slots[index] = Some(coded);
    }

    pub fn take(&self) -> Vec<CodedBand> {
        std::mem::take(&mut *self.0.lock().unwrap())
            .into_iter()
            .map(|slot| slot.expect("every stripe is coded"))
            .collect()
    }
}

#[derive(Default)]
struct QueueState {
    jobs: VecDeque<StripeJob>,
    spare: Vec<Vec<f32>>,
    closed: bool,
}

pub(crate) struct StripeQueue {
    state: Mutex<QueueState>,
    job_ready: Condvar,
    capacity: usize,
}

impl StripeQueue {
    pub fn warp(capacity: usize) -> Self {
        Self {
            state: Mutex::new(QueueState::default()),
            job_ready: Condvar::new(),
            capacity,
        }
    }

    pub fn try_push(&self, job: StripeJob) -> Result<(), StripeJob> {
        let mut state = self.state.lock().unwrap();
        if state.jobs.len() >= self.capacity {
            return Err(job);
        }
        state.jobs.push_back(job);
        self.job_ready.notify_one();
        Ok(())
    }

    fn pop(&self) -> Option<StripeJob> {
        let mut state = self.state.lock().unwrap();
        loop {
            if let Some(job) = state.jobs.pop_front() {
                return Some(job);
            }
            if state.closed {
                return None;
            }
            state = self.job_ready.wait(state).unwrap();
        }
    }

    pub fn spare_buffer(&self, len: usize) -> Vec<f32> {
        let mut buffer = self.state.lock().unwrap().spare.pop().unwrap_or_default();
        buffer.resize(len, 0.0);
        buffer
    }

    pub fn recycle(&self, buffer: Vec<f32>) {
        self.state.lock().unwrap().spare.push(buffer);
    }

    fn close(&self) {
        self.state.lock().unwrap().closed = true;
        self.job_ready.notify_all();
    }
}

// without this a panicking feed leaves the workers waiting forever
struct CloseOnDrop<'a>(&'a StripeQueue);

impl Drop for CloseOnDrop<'_> {
    fn drop(&mut self) {
        self.0.close();
    }
}

pub(crate) fn run_stripe_workers<R>(
    queue: &StripeQueue,
    workers: usize,
    feed: impl FnOnce() -> R,
) -> R {
    std::thread::scope(|scope| {
        for _ in 0..workers {
            scope.spawn(|| {
                let mut block = Vec::new();
                while let Some(job) = queue.pop() {
                    queue.recycle(job.code(&mut block));
                }
            });
        }
        let _close = CloseOnDrop(queue);
        feed()
    })
}
