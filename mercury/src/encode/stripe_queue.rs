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

// without this a panicking feed or a failed spawn leaves the workers waiting forever
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
        let _close = CloseOnDrop(queue);
        for _ in 0..workers {
            scope.spawn(|| {
                let mut block = Vec::new();
                while let Some(job) = queue.pop() {
                    queue.recycle(job.code(&mut block));
                }
            });
        }
        feed()
    })
}

#[cfg(all(test, target_os = "linux"))]
mod tests {
    use super::*;
    use std::process::{Command, Stdio};
    use std::time::{Duration, Instant};

    const CHILD_ENV: &str = "MERCURY_STRIPE_SPAWN_FAILURE_CHILD";
    const WORKER_STACK_KB: usize = 256 << 10;
    const ADDRESS_SPACE_KB: usize = 1536 << 10;
    const CHILD_TIMEOUT: Duration = Duration::from_secs(30);
    const WORKERS: usize = 8;

    fn address_space_kb() -> usize {
        let status = std::fs::read_to_string("/proc/self/status").unwrap();
        let line = status
            .lines()
            .find(|line| line.starts_with("VmSize:"))
            .unwrap();
        line.split_whitespace().nth(1).unwrap().parse().unwrap()
    }

    // the child's address space limit lets a few workers spawn before one fails
    #[test]
    fn failed_worker_spawn_closes_the_queue() {
        if std::env::var_os(CHILD_ENV).is_some() {
            assert!(
                address_space_kb() + 2 * WORKER_STACK_KB <= ADDRESS_SPACE_KB,
                "no room for a worker to spawn before the failure"
            );
            let queue = StripeQueue::warp(1);
            let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                run_stripe_workers(&queue, WORKERS, || ())
            }));
            assert!(result.is_err(), "every worker spawned, no failure to test");
            return;
        }
        let script = format!(
            "ulimit -v {ADDRESS_SPACE_KB} && exec \"$0\" failed_worker_spawn_closes_the_queue --test-threads=1"
        );
        let mut child = Command::new("sh")
            .arg("-c")
            .arg(script)
            .arg(std::env::current_exe().unwrap())
            .env(CHILD_ENV, "1")
            .env("RUST_MIN_STACK", (WORKER_STACK_KB << 10).to_string())
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()
            .unwrap();
        let started = Instant::now();
        loop {
            if let Some(status) = child.try_wait().unwrap() {
                assert!(status.success(), "child exited with {status}");
                return;
            }
            if started.elapsed() > CHILD_TIMEOUT {
                child.kill().unwrap();
                panic!("workers hung after a failed spawn");
            }
            std::thread::sleep(Duration::from_millis(50));
        }
    }
}
