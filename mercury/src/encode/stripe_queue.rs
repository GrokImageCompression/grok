use std::any::Any;
use std::cell::RefCell;
use std::collections::VecDeque;
use std::ffi::c_void;
use std::panic::{AssertUnwindSafe, catch_unwind, resume_unwind};
use std::sync::{Arc, Condvar, Mutex};

use crate::weft::rt::HostExecutor;

use super::band_coder::{CodedBand, QuantizedSample, StripeLayout, code_stripe};

pub(crate) struct StripeJob<T> {
    pub samples: Vec<T>,
    pub rows: usize,
    pub layout: StripeLayout,
    pub results: Arc<StripeResults>,
    pub index: usize,
}

impl<T: QuantizedSample> StripeJob<T> {
    pub fn code(self, block: &mut Vec<i32>) -> Vec<T> {
        let mut coded = CodedBand::default();
        code_stripe(&self.layout, &self.samples, self.rows, block, &mut coded);
        self.results.store(self.index, coded);
        self.samples
    }
}

#[derive(Default)]
struct StoredStripes {
    slots: Vec<Option<CodedBand>>,
    stored: usize,
    panic: Option<Box<dyn Any + Send>>,
}

// one band's coded stripes, filled in any order
#[derive(Default)]
pub(crate) struct StripeResults {
    stripes: Mutex<StoredStripes>,
    changed: Condvar,
}

impl StripeResults {
    fn store(&self, index: usize, coded: CodedBand) {
        let mut stripes = self.stripes.lock().unwrap();
        if stripes.slots.len() <= index {
            stripes.slots.resize_with(index + 1, || None);
        }
        stripes.slots[index] = Some(coded);
        stripes.stored += 1;
        self.changed.notify_all();
    }

    fn abandon(&self, panic: Box<dyn Any + Send>) {
        self.stripes.lock().unwrap().panic.get_or_insert(panic);
        self.changed.notify_all();
    }

    fn all_stored(&self, stripes_sent: usize) -> bool {
        self.stripes.lock().unwrap().stored >= stripes_sent
    }

    pub fn take(&self, stripes_sent: usize, queue: &dyn QueuedStripes) -> Vec<CodedBand> {
        // host workers can all be blocked here, with this band's stripes still queued behind them
        while !self.all_stored(stripes_sent) && queue.code_next() {}
        let mut stripes = self.stripes.lock().unwrap();
        while stripes.stored < stripes_sent {
            if let Some(panic) = stripes.panic.take() {
                drop(stripes);
                resume_unwind(panic);
            }
            stripes = self.changed.wait(stripes).unwrap();
        }
        std::mem::take(&mut stripes.slots)
            .into_iter()
            .map(|slot| slot.expect("every stripe is coded"))
            .collect()
    }
}

pub(crate) trait QueuedStripes: Send + Sync {
    fn code_next(&self) -> bool;
}

thread_local! {
    static BLOCK_SCRATCH: RefCell<Vec<i32>> = const { RefCell::new(Vec::new()) };
}

struct QueueState<T> {
    jobs: VecDeque<StripeJob<T>>,
    spare: Vec<Vec<T>>,
}

pub(crate) struct StripeQueue<T> {
    state: Mutex<QueueState<T>>,
    host: HostExecutor,
    capacity: usize,
}

extern "C" fn code_submitted_stripe<T: QuantizedSample>(pass: *mut c_void) {
    // Safety: try_push hands every pass an Arc from Arc::into_raw and the host runs each pass once
    let queue = unsafe { Arc::from_raw(pass.cast::<StripeQueue<T>>().cast_const()) };
    queue.code_next();
}

impl<T: QuantizedSample> StripeQueue<T> {
    pub fn warp(capacity: usize, host: HostExecutor) -> Self {
        Self {
            state: Mutex::new(QueueState {
                jobs: VecDeque::new(),
                spare: Vec::new(),
            }),
            host,
            capacity,
        }
    }

    pub fn try_push(self: &Arc<Self>, job: StripeJob<T>) -> Result<(), StripeJob<T>> {
        {
            let mut state = self.state.lock().unwrap();
            if state.jobs.len() >= self.capacity {
                return Err(job);
            }
            state.jobs.push_back(job);
        }
        let pass = Arc::into_raw(self.clone()).cast_mut().cast();
        (self.host.submit)(self.host.ctx, code_submitted_stripe::<T>, pass);
        Ok(())
    }

    pub fn spare_buffer(&self, len: usize) -> Vec<T> {
        let mut buffer = self.state.lock().unwrap().spare.pop().unwrap_or_default();
        buffer.resize(len, T::default());
        buffer
    }

    pub fn recycle(&self, buffer: Vec<T>) {
        self.state.lock().unwrap().spare.push(buffer);
    }
}

impl<T: QuantizedSample> QueuedStripes for StripeQueue<T> {
    fn code_next(&self) -> bool {
        let Some(job) = self.state.lock().unwrap().jobs.pop_front() else {
            return false;
        };
        let results = job.results.clone();
        // host passes are extern "C", where an unwinding panic aborts the process
        let coded = BLOCK_SCRATCH
            .with_borrow_mut(|block| catch_unwind(AssertUnwindSafe(|| job.code(block))));
        match coded {
            Ok(samples) => self.recycle(samples),
            Err(panic) => results.abandon(panic),
        }
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::encode::band_coder::BandT1Params;
    use crate::weft::rt::HostRunFn;
    use std::sync::mpsc::channel;
    use std::time::Duration;

    const WIDTH: usize = 64;
    const ROWS: usize = 4;
    const STRIPES: usize = 6;

    unsafe extern "C" fn no_passes(
        _samples: *const i32,
        _num_cols: i32,
        _num_rows: i32,
        _max_magnitude: u32,
        _orientation: i32,
        _compno: i32,
        _level: i32,
        _stepsize: f64,
        _mct: i32,
        _reversible: i32,
        _coded_data: *mut *const u8,
        _pass_rates: *mut u16,
        _pass_distortions: *mut f64,
        _pass_terminated: *mut u8,
        _numbps: *mut i32,
    ) -> i32 {
        0
    }

    struct HeldPasses(Mutex<Vec<(HostRunFn, usize)>>);

    extern "C" fn hold(ctx: *mut c_void, run: HostRunFn, pass: *mut c_void) {
        let held = unsafe { &*ctx.cast::<HeldPasses>() };
        held.0.lock().unwrap().push((run, pass as usize));
    }

    fn layout() -> StripeLayout {
        StripeLayout {
            width: WIDTH,
            x0: 0,
            block_width: WIDTH,
            inverse_step: 1.0,
            t1_params: BandT1Params {
                orientation: 0,
                compno: 0,
                level: 0,
                stepsize: 1.0,
                mct: false,
                reversible: false,
            },
            t1: no_passes,
        }
    }

    #[test]
    fn take_codes_stripes_the_host_never_started() {
        let held = Box::leak(Box::new(HeldPasses(Mutex::new(Vec::new()))));
        let host = HostExecutor {
            submit: hold,
            ctx: (held as *const HeldPasses).cast_mut().cast(),
        };
        let queue = Arc::new(StripeQueue::<f32>::warp(STRIPES, host));
        let results = Arc::new(StripeResults::default());
        for index in 0..STRIPES {
            let job = StripeJob {
                samples: vec![0.0; WIDTH * ROWS],
                rows: ROWS,
                layout: layout(),
                results: results.clone(),
                index,
            };
            assert!(queue.try_push(job).is_ok());
        }
        let (sender, receiver) = channel();
        let taker = queue.clone();
        std::thread::spawn(move || {
            sender
                .send(results.take(STRIPES, taker.as_ref()).len())
                .unwrap();
        });
        let taken = receiver.recv_timeout(Duration::from_secs(30));
        assert_eq!(taken, Ok(STRIPES), "take waited on stripes nobody codes");
        for (run, pass) in std::mem::take(&mut *held.0.lock().unwrap()) {
            run(pass as *mut c_void);
        }
    }
}
