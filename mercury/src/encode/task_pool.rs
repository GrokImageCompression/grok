use std::any::Any;
use std::ffi::c_void;
use std::panic::{AssertUnwindSafe, catch_unwind, resume_unwind};
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::{Arc, Condvar, Mutex};

use crate::weft::rt::HostExecutor;

struct JobPointer(*const (dyn Fn() + Sync + 'static));

// Safety: the pointee is Sync, and TaskPool::share waits for every helper to leave it before it goes out of scope
unsafe impl Send for JobPointer {}

#[derive(Default)]
struct JobState {
    job: Option<JobPointer>,
    generation: u64,
    helpers_running: usize,
    panic: Option<Box<dyn Any + Send>>,
}

#[derive(Default)]
struct Shared {
    state: Mutex<JobState>,
    helpers_left: Condvar,
}

struct HelperPass {
    shared: Arc<Shared>,
    generation: u64,
}

pub(crate) struct TaskPool {
    shared: Arc<Shared>,
    host: HostExecutor,
    threads: usize,
    job_active: AtomicBool,
}

extern "C" fn run_helper(pass: *mut c_void) {
    // Safety: TaskPool::share boxes every pass it submits and the host runs each one once
    let pass = unsafe { Box::from_raw(pass.cast::<HelperPass>()) };
    let job = {
        let mut state = pass.shared.state.lock().unwrap();
        // hosts can start a helper after its job is finished, or during a later one
        if state.generation != pass.generation {
            return;
        }
        let Some(job) = &state.job else {
            return;
        };
        let job = job.0;
        state.helpers_running += 1;
        job
    };
    // Safety: TaskPool::share keeps the job alive until helpers_running is back to zero
    let result = catch_unwind(AssertUnwindSafe(|| unsafe { (*job)() }));
    let mut state = pass.shared.state.lock().unwrap();
    if let Err(panic) = result {
        state.panic.get_or_insert(panic);
    }
    state.helpers_running -= 1;
    if state.helpers_running == 0 {
        pass.shared.helpers_left.notify_all();
    }
}

impl TaskPool {
    pub fn warp(host: HostExecutor, threads: usize) -> Self {
        Self {
            shared: Arc::new(Shared::default()),
            host,
            threads,
            job_active: AtomicBool::new(false),
        }
    }

    pub fn run<T: Send, R: Send>(&self, tasks: Vec<T>, work: impl Fn(T) -> R + Sync) -> Vec<R> {
        let count = tasks.len();
        let slots: Vec<Mutex<Option<T>>> = tasks
            .into_iter()
            .map(|task| Mutex::new(Some(task)))
            .collect();
        let results: Vec<Mutex<Option<R>>> = std::iter::repeat_with(|| Mutex::new(None))
            .take(count)
            .collect();
        let next = AtomicUsize::new(0);
        let claim_tasks = || {
            loop {
                let index = next.fetch_add(1, Ordering::Relaxed);
                if index >= count {
                    return;
                }
                let task = slots[index]
                    .lock()
                    .unwrap()
                    .take()
                    .expect("each task is taken once");
                *results[index].lock().unwrap() = Some(work(task));
            }
        };
        let helpers = self.threads.min(count).saturating_sub(1);
        // helpers inside a job never pick up a second one
        if helpers > 0 && !self.job_active.swap(true, Ordering::Acquire) {
            self.share(&claim_tasks, helpers);
            self.job_active.store(false, Ordering::Release);
        } else {
            claim_tasks();
        }
        results
            .into_iter()
            .map(|result| result.into_inner().unwrap().expect("every task ran"))
            .collect()
    }

    fn share(&self, job: &(dyn Fn() + Sync), helpers: usize) {
        // Safety: the job outlives every helper use of it, this function waits for helpers_running to reach zero
        let pointer: *const (dyn Fn() + Sync + 'static) = unsafe { std::mem::transmute(job) };
        let generation = {
            let mut state = self.shared.state.lock().unwrap();
            state.job = Some(JobPointer(pointer));
            state.generation += 1;
            state.generation
        };
        for _ in 0..helpers {
            let pass = Box::new(HelperPass {
                shared: self.shared.clone(),
                generation,
            });
            (self.host.submit)(self.host.ctx, run_helper, Box::into_raw(pass).cast());
        }
        let caller_result = catch_unwind(AssertUnwindSafe(job));
        let helper_panic = {
            let mut state = self.shared.state.lock().unwrap();
            state.job = None;
            while state.helpers_running > 0 {
                state = self.shared.helpers_left.wait(state).unwrap();
            }
            state.panic.take()
        };
        if let Err(panic) = caller_result {
            resume_unwind(panic);
        }
        if let Some(panic) = helper_panic {
            resume_unwind(panic);
        }
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use crate::weft::rt::HostRunFn;
    use std::collections::HashSet;
    use std::sync::mpsc::{Receiver, Sender, channel};
    use std::thread::{JoinHandle, ThreadId};

    struct Pass(HostRunFn, usize);

    pub(crate) struct TestHost {
        sender: Option<Sender<Pass>>,
        threads: Vec<JoinHandle<()>>,
        thread_ids: Vec<ThreadId>,
    }

    extern "C" fn submit_to_test_host(ctx: *mut c_void, run: HostRunFn, pass: *mut c_void) {
        let sender = unsafe { &*ctx.cast::<Sender<Pass>>() };
        sender.send(Pass(run, pass as usize)).unwrap();
    }

    impl TestHost {
        pub fn spawn(threads: usize) -> Self {
            let (sender, receiver) = channel::<Pass>();
            let receiver = Arc::new(Mutex::new(receiver));
            let handles: Vec<JoinHandle<()>> = (0..threads)
                .map(|_| {
                    let receiver: Arc<Mutex<Receiver<Pass>>> = receiver.clone();
                    std::thread::spawn(move || {
                        loop {
                            let next = receiver.lock().unwrap().recv();
                            let Ok(Pass(run, pass)) = next else { return };
                            run(pass as *mut c_void);
                        }
                    })
                })
                .collect();
            let thread_ids = handles.iter().map(|handle| handle.thread().id()).collect();
            Self {
                sender: Some(sender),
                threads: handles,
                thread_ids,
            }
        }

        pub fn executor(&self) -> HostExecutor {
            HostExecutor {
                submit: submit_to_test_host,
                ctx: (self.sender.as_ref().unwrap() as *const Sender<Pass>)
                    .cast_mut()
                    .cast(),
            }
        }
    }

    impl Drop for TestHost {
        fn drop(&mut self) {
            self.sender.take();
            for thread in self.threads.drain(..) {
                thread.join().unwrap();
            }
        }
    }

    #[test]
    fn results_keep_task_order() {
        let host = TestHost::spawn(3);
        let pool = TaskPool::warp(host.executor(), 4);
        let squares = pool.run((0..100u64).collect(), |n| n * n);
        assert_eq!(squares, (0..100u64).map(|n| n * n).collect::<Vec<_>>());
    }

    #[test]
    fn tasks_run_only_on_the_caller_and_host_threads() {
        let host = TestHost::spawn(3);
        let pool = TaskPool::warp(host.executor(), 4);
        let seen = Mutex::new(HashSet::<ThreadId>::new());
        for _ in 0..50 {
            pool.run((0..64).collect(), |_: i32| {
                seen.lock().unwrap().insert(std::thread::current().id());
                std::thread::yield_now();
            });
        }
        let mut allowed: HashSet<ThreadId> = host.thread_ids.iter().copied().collect();
        allowed.insert(std::thread::current().id());
        assert!(seen.lock().unwrap().is_subset(&allowed));
    }

    #[test]
    fn nested_run_finishes_on_the_calling_thread() {
        let host = TestHost::spawn(3);
        let pool = TaskPool::warp(host.executor(), 4);
        let sums = pool.run((0..8u32).collect(), |outer| {
            pool.run((0..10u32).collect(), |inner| outer * inner)
                .into_iter()
                .sum::<u32>()
        });
        assert_eq!(sums, (0..8u32).map(|outer| outer * 45).collect::<Vec<_>>());
    }

    #[test]
    fn helpers_started_after_their_job_do_nothing() {
        let held: Mutex<Vec<Pass>> = Mutex::new(Vec::new());
        extern "C" fn hold(ctx: *mut c_void, run: HostRunFn, pass: *mut c_void) {
            let held = unsafe { &*ctx.cast::<Mutex<Vec<Pass>>>() };
            held.lock().unwrap().push(Pass(run, pass as usize));
        }
        let host = HostExecutor {
            submit: hold,
            ctx: (&held as *const Mutex<Vec<Pass>>).cast_mut().cast(),
        };
        let pool = TaskPool::warp(host, 4);
        for round in 0..3u32 {
            let doubled = pool.run((0..20u32).collect(), |n| n * 2 + round);
            assert_eq!(
                doubled,
                (0..20u32).map(|n| n * 2 + round).collect::<Vec<_>>()
            );
        }
        let passes = std::mem::take(&mut *held.lock().unwrap());
        assert_eq!(passes.len(), 9);
        for Pass(run, pass) in passes {
            run(pass as *mut c_void);
        }
    }
}
