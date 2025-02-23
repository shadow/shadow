// When comparing a loaded value that happens to be bool,
// assert_eq! reads better than assert!.
#![allow(clippy::bool_assert_comparison)]

use std::marker::PhantomData;
use std::ops::Deref;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

use atomic_refcell::AtomicRefCell;

use crate::logical_processor::LogicalProcessors;
use crate::sync::count_down_latch::{LatchCounter, LatchWaiter, build_count_down_latch};
use crate::sync::thread_parking::{ThreadUnparker, ThreadUnparkerUnassigned};

// If making substantial changes to this scheduler, you should verify the compilation error message
// for each test at the end of this file to make sure that they correctly cause the expected
// compilation error. This work pool unsafely transmutes the task closure lifetime, and the
// commented tests are meant to make sure that the work pool does not allow unsound code to compile.
// Due to lifetime sub-typing/variance, rust will sometimes allow closures with shorter or longer
// lifetimes than we specify in the API, so the tests check to make sure the closures are invariant
// over the lifetime and that the usage is sound.

/// Context information provided to each task closure.
pub struct TaskData {
    pub thread_idx: usize,
    pub processor_idx: usize,
    pub cpu_id: Option<u32>,
}

/// A task that is run by the pool threads.
trait TaskFn: Fn(&TaskData) + Send + Sync {}
impl<T> TaskFn for T where T: Fn(&TaskData) + Send + Sync {}

/// A thread pool that runs a task on many threads. A task will run once on each thread. Each
/// logical processor will run threads sequentially, meaning that the thread pool's parallelism
/// depends on the number of processors, not the number of threads. Threads are assigned to logical
/// processors, which can be bound to operating system processors.
pub struct ParallelismBoundedThreadPool {
    /// Handles for joining threads when they've exited.
    thread_handles: Vec<std::thread::JoinHandle<()>>,
    /// State shared between all threads.
    shared_state: Arc<SharedState>,
    /// The main thread uses this to wait for the threads to finish running the task.
    task_end_waiter: LatchWaiter,
}

pub struct SharedState {
    /// The task to run during the next round.
    task: AtomicRefCell<Option<Box<dyn TaskFn>>>,
    /// Has a thread panicked?
    has_thread_panicked: AtomicBool,
    /// The logical processors.
    logical_processors: AtomicRefCell<LogicalProcessors>,
    /// The threads which run on logical processors.
    threads: Vec<ThreadScheduling>,
}

/// Scheduling state for a thread.
pub struct ThreadScheduling {
    /// Used to unpark the thread when it has a new task.
    unparker: ThreadUnparker,
    /// The OS pid for this thread. This will have an invalid value when running under miri.
    #[cfg_attr(miri, allow(dead_code))]
    tid: nix::unistd::Pid,
    /// The logical processor index that this thread is assigned to.
    logical_processor_idx: AtomicUsize,
}

impl ParallelismBoundedThreadPool {
    /// A new work pool with logical processors that are pinned to the provided OS processors.
    /// Each logical processor is assigned many threads.
    pub fn new(cpu_ids: &[Option<u32>], num_threads: usize, thread_name: &str) -> Self {
        // we don't need more logical processors than threads
        let cpu_ids = &cpu_ids[..std::cmp::min(cpu_ids.len(), num_threads)];

        let logical_processors = LogicalProcessors::new(cpu_ids, num_threads);

        let (task_end_counter, task_end_waiter) = build_count_down_latch();

        let mut thread_handles = Vec::new();
        let mut shared_state_senders = Vec::new();
        let mut tids = Vec::new();

        // start the threads
        for i in 0..num_threads {
            // the thread will send us the tid, then we'll later send the shared state to the thread
            let (tid_send, tid_recv) = crossbeam::channel::bounded(1);
            let (shared_state_send, shared_state_recv) = crossbeam::channel::bounded(1);

            let task_end_counter_clone = task_end_counter.clone();

            let handle = std::thread::Builder::new()
                .name(thread_name.to_string())
                .spawn(move || work_loop(i, tid_send, shared_state_recv, task_end_counter_clone))
                .unwrap();

            thread_handles.push(handle);
            shared_state_senders.push(shared_state_send);
            tids.push(tid_recv.recv().unwrap());
        }

        // build the scheduling data for the threads
        let thread_data: Vec<ThreadScheduling> = logical_processors
            .iter()
            .cycle()
            .zip(&tids)
            .zip(&thread_handles)
            .map(|((processor_idx, tid), handle)| ThreadScheduling {
                unparker: ThreadUnparkerUnassigned::new().assign(handle.thread().clone()),
                tid: *tid,
                logical_processor_idx: AtomicUsize::new(processor_idx),
            })
            .collect();

        // add each thread to its logical processor
        for (thread_idx, thread) in thread_data.iter().enumerate() {
            let logical_processor_idx = thread.logical_processor_idx.load(Ordering::Relaxed);
            logical_processors.add_worker(logical_processor_idx, thread_idx);
        }

        // state shared between all threads
        let shared_state = Arc::new(SharedState {
            task: AtomicRefCell::new(None),
            has_thread_panicked: AtomicBool::new(false),
            logical_processors: AtomicRefCell::new(logical_processors),
            threads: thread_data,
        });

        // send the shared state to each thread
        for s in shared_state_senders.into_iter() {
            s.send(Arc::clone(&shared_state)).unwrap();
        }

        Self {
            thread_handles,
            shared_state,
            task_end_waiter,
        }
    }

    /// The total number of logical processors.
    pub fn num_processors(&self) -> usize {
        self.shared_state.logical_processors.borrow().iter().len()
    }

    /// The total number of threads.
    pub fn num_threads(&self) -> usize {
        self.thread_handles.len()
    }

    /// Stop and join the threads.
    pub fn join(self) {
        // the drop handler will join the threads
    }

    fn join_internal(&mut self) {
        // a `None` indicates that the threads should end
        assert!(self.shared_state.task.borrow().is_none());

        // only check the thread join return value if no threads have yet panicked
        let check_for_errors = !self
            .shared_state
            .has_thread_panicked
            .load(Ordering::Relaxed);

        // send the sentinel task to all threads
        for thread in &self.shared_state.threads {
            thread.unparker.unpark();
        }

        for handle in self.thread_handles.drain(..) {
            let result = handle.join();
            if check_for_errors {
                result.expect("A thread panicked while stopping");
            }
        }
    }

    /// Create a new scope for the pool. The scope will ensure that any task run on the pool within
    /// this scope has completed before leaving the scope.
    //
    // SAFETY: This works because:
    //
    // 1. WorkerScope<'scope> is covariant over 'scope.
    // 2. TaskRunner<'a, 'scope> is invariant over WorkerScope<'scope>, so TaskRunner<'a, 'scope>
    //    is invariant over 'scope.
    // 3. FnOnce(TaskRunner<'a, 'scope>) is contravariant over TaskRunner<'a, 'scope>, so
    //    FnOnce(TaskRunner<'a, 'scope>) is invariant over 'scope.
    //
    // This means that the provided scope closure cannot take a TaskRunner<'a, 'scope2> where
    // 'scope2 is shorter than 'scope, and therefore 'scope must be as long as this function call.
    //
    // If TaskRunner<'a, 'scope> was covariant over 'scope, then FnOnce(TaskRunner<'a, 'scope>)
    // would have been contravariant over 'scope. This would have allowed the user to provide a
    // scope closure that could take a TaskRunner<'a, 'scope2> where 'scope2 is shorter than 'scope.
    // Then when TaskRunner<'a, 'scope2>::run(...) would eventually be called, the run closure would
    // capture data with a lifetime of only 'scope2, which would be a shorter lifetime than the
    // scope closure's lifetime of 'scope. Then, any captured mutable references would be accessible
    // from both the run closure and the scope closure, leading to mutable aliasing.
    pub fn scope<'scope>(
        &'scope mut self,
        f: impl for<'a> FnOnce(TaskRunner<'a, 'scope>) + 'scope,
    ) {
        assert!(
            !self
                .shared_state
                .has_thread_panicked
                .load(Ordering::Relaxed),
            "Attempting to use a workpool that previously panicked"
        );

        // makes sure that the task is properly cleared even if 'f' panics
        let mut scope = WorkerScope::<'scope> {
            pool: self,
            _phantom: Default::default(),
        };

        let runner = TaskRunner { scope: &mut scope };

        f(runner);
    }
}

impl std::ops::Drop for ParallelismBoundedThreadPool {
    fn drop(&mut self) {
        self.join_internal();
    }
}

struct WorkerScope<'scope> {
    pool: &'scope mut ParallelismBoundedThreadPool,
    // when we are dropped, it's like dropping the task
    _phantom: PhantomData<Box<dyn TaskFn + 'scope>>,
}

impl std::ops::Drop for WorkerScope<'_> {
    fn drop(&mut self) {
        // if the task was set (if `TaskRunner::run` was called)
        if self.pool.shared_state.task.borrow().is_some() {
            // wait for the task to complete
            self.pool.task_end_waiter.wait();

            // clear the task
            *self.pool.shared_state.task.borrow_mut() = None;

            // we should have run every thread, so swap the logical processors' internal queues
            self.pool
                .shared_state
                .logical_processors
                .borrow_mut()
                .reset();

            // generally following https://docs.rs/rayon/latest/rayon/fn.scope.html#panics
            if self
                .pool
                .shared_state
                .has_thread_panicked
                .load(Ordering::Relaxed)
            {
                // we could store the thread's panic message and propagate it, but I don't think
                // that's worth handling
                panic!("A work thread panicked");
            }
        }
    }
}

/// Allows a single task to run per pool scope.
pub struct TaskRunner<'a, 'scope> {
    // SAFETY: Self must be invariant over 'scope, which is why we use &mut here. See the
    // documentation for scope() above for details.
    scope: &'a mut WorkerScope<'scope>,
}

impl<'scope> TaskRunner<'_, 'scope> {
    /// Run a task on the pool's threads.
    // unfortunately we need to use `Fn(&TaskData) + Send + Sync` and not `TaskFn` here, otherwise
    // rust's type inference doesn't work nicely in the calling code
    pub fn run(self, f: impl Fn(&TaskData) + Send + Sync + 'scope) {
        let f = Box::new(f);

        // SAFETY: WorkerScope will drop this TaskFn before the end of 'scope
        let f = unsafe {
            std::mem::transmute::<Box<dyn TaskFn + 'scope>, Box<dyn TaskFn + 'static>>(f)
        };

        *self.scope.pool.shared_state.task.borrow_mut() = Some(f);

        let logical_processors = self.scope.pool.shared_state.logical_processors.borrow();

        // start the first thread for each logical processor
        for processor_idx in logical_processors.iter() {
            start_next_thread(
                processor_idx,
                &self.scope.pool.shared_state,
                &logical_processors,
            );
        }
    }
}

fn work_loop(
    thread_idx: usize,
    tid_send: crossbeam::channel::Sender<nix::unistd::Pid>,
    shared_state_recv: crossbeam::channel::Receiver<Arc<SharedState>>,
    mut end_counter: LatchCounter,
) {
    // we don't use `catch_unwind` here for two main reasons:
    //
    // 1. `catch_unwind` requires that the closure is `UnwindSafe`, which means that `TaskFn` also
    // needs to be `UnwindSafe`. This is a big restriction on the types of tasks that we could run,
    // since it requires that there's no interior mutability in the closure. rayon seems to get
    // around this by wrapping the closure in `AssertUnwindSafe`, under the assumption that the
    // panic will be propagated later with `resume_unwinding`, but this is a little more difficult
    // to reason about compared to simply avoiding `catch_unwind` altogether.
    // https://github.com/rayon-rs/rayon/blob/c571f8ffb4f74c8c09b4e1e6d9979b71b4414d07/rayon-core/src/unwind.rs#L9
    //
    // 2. There is a footgun with `catch_unwind` that could cause unexpected behaviour. If the
    // closure called `panic_any()` with a type that has a Drop implementation, and that Drop
    // implementation panics, it will cause a panic that is not caught by the `catch_unwind`,
    // causing the thread to panic again with no chance to clean up properly. The work pool would
    // then deadlock. Since we don't use `catch_unwind`, the thread will instead "panic when
    // panicking" and abort, which is a more ideal outcome.
    // https://github.com/rust-lang/rust/issues/86027

    // this will poison the workpool when it's dropped
    struct PoisonWhenDropped<'a>(&'a SharedState);

    impl std::ops::Drop for PoisonWhenDropped<'_> {
        fn drop(&mut self) {
            // if we panicked, then inform other threads that we panicked and allow them to exit
            // gracefully
            self.0.has_thread_panicked.store(true, Ordering::Relaxed);
        }
    }

    // this will start the next thread when it's dropped
    struct StartNextThreadOnDrop<'a> {
        shared_state: &'a SharedState,
        logical_processors: &'a LogicalProcessors,
        current_processor_idx: usize,
    }

    impl std::ops::Drop for StartNextThreadOnDrop<'_> {
        fn drop(&mut self) {
            start_next_thread(
                self.current_processor_idx,
                self.shared_state,
                self.logical_processors,
            );
        }
    }

    let tid = if cfg!(not(miri)) {
        nix::unistd::gettid()
    } else {
        // the sched_setaffinity() should be disabled under miri, so this should be fine
        nix::unistd::Pid::from_raw(-1)
    };

    // send this thread's tid to the main thread
    tid_send.send(tid).unwrap();

    // get the shared state
    let shared_state = shared_state_recv.recv().unwrap();
    let shared_state = shared_state.as_ref();

    let poison_when_dropped = PoisonWhenDropped(shared_state);

    let thread_data = &shared_state.threads[thread_idx];
    let thread_parker = thread_data.unparker.parker();

    loop {
        // wait for a new task
        thread_parker.park();

        // scope used to make sure we drop everything (including the task) before counting down
        {
            let logical_processors = &shared_state.logical_processors.borrow();

            // the logical processor for this thread may have been changed by the previous thread if
            // the thread was stolen from another logical processor
            let current_processor_idx = thread_data.logical_processor_idx.load(Ordering::Relaxed);

            // this will start the next thread even if the below task panics or we break from the
            // loop
            //
            // we must start the next thread before we count down, otherwise we'll have runtime
            // panics due to simultaneous exclusive and shared borrows of `logical_processors`
            let _start_next_thread_when_dropped = StartNextThreadOnDrop {
                shared_state,
                logical_processors,
                current_processor_idx,
            };

            // context information for the task
            let task_data = TaskData {
                thread_idx,
                processor_idx: current_processor_idx,
                cpu_id: logical_processors.cpu_id(current_processor_idx),
            };

            // run the task
            match shared_state.task.borrow().deref() {
                Some(task) => (task)(&task_data),
                None => {
                    // received the sentinel value
                    break;
                }
            };
        }

        // SAFETY: we do not hold any references/borrows to the task at this time
        end_counter.count_down();
    }

    // didn't panic, so forget the poison handler and return normally
    std::mem::forget(poison_when_dropped);
}

/// Choose the next thread to run on the logical processor, and then start it.
fn start_next_thread(
    processor_idx: usize,
    shared_state: &SharedState,
    logical_processors: &LogicalProcessors,
) {
    // if there is a thread to run on this logical processor, then start it
    if let Some((next_thread_idx, from_processor_idx)) =
        logical_processors.next_worker(processor_idx)
    {
        let next_thread = &shared_state.threads[next_thread_idx];

        debug_assert_eq!(
            from_processor_idx,
            next_thread.logical_processor_idx.load(Ordering::Relaxed)
        );

        // if the next thread is assigned to a different processor
        if processor_idx != from_processor_idx {
            assign_to_processor(next_thread, processor_idx, logical_processors);
        }

        // start the thread
        next_thread.unparker.unpark();
    }
}

/// Assigns the thread to the logical processor.
fn assign_to_processor(
    thread: &ThreadScheduling,
    processor_idx: usize,
    logical_processors: &LogicalProcessors,
) {
    // set thread's affinity if the logical processor has a cpu ID
    if let Some(cpu_id) = logical_processors.cpu_id(processor_idx) {
        let mut cpus = nix::sched::CpuSet::new();
        cpus.set(cpu_id as usize).unwrap();

        // only set the affinity if not running in miri
        #[cfg(not(miri))]
        nix::sched::sched_setaffinity(thread.tid, &cpus).unwrap();
    }

    // set thread's processor
    thread
        .logical_processor_idx
        .store(processor_idx, Ordering::Release);
}

#[cfg(any(test, doctest))]
mod tests {
    use std::sync::atomic::{AtomicBool, AtomicU32};

    use super::*;

    #[test]
    fn test_scope() {
        let mut pool = ParallelismBoundedThreadPool::new(&[None, None], 4, "worker");

        let mut counter = 0u32;
        for _ in 0..3 {
            pool.scope(|_| {
                counter += 1;
            });
        }

        assert_eq!(counter, 3);
    }

    #[test]
    fn test_run() {
        let mut pool = ParallelismBoundedThreadPool::new(&[None, None], 4, "worker");

        let counter = AtomicU32::new(0);
        for _ in 0..3 {
            pool.scope(|s| {
                s.run(|_| {
                    counter.fetch_add(1, Ordering::SeqCst);
                });
            });
        }

        assert_eq!(counter.load(Ordering::SeqCst), 12);
    }

    #[test]
    fn test_pinning() {
        let mut pool = ParallelismBoundedThreadPool::new(&[Some(0), Some(1)], 4, "worker");

        let counter = AtomicU32::new(0);
        for _ in 0..3 {
            pool.scope(|s| {
                s.run(|_| {
                    counter.fetch_add(1, Ordering::SeqCst);
                });
            });
        }

        assert_eq!(counter.load(Ordering::SeqCst), 12);
    }

    #[test]
    fn test_large_parallelism() {
        let mut pool = ParallelismBoundedThreadPool::new(&vec![None; 100], 4, "worker");

        let counter = AtomicU32::new(0);
        for _ in 0..3 {
            pool.scope(|s| {
                s.run(|_| {
                    counter.fetch_add(1, Ordering::SeqCst);
                });
            });
        }

        assert_eq!(counter.load(Ordering::SeqCst), 12);
    }

    #[test]
    fn test_large_num_threads() {
        let mut pool = ParallelismBoundedThreadPool::new(&[None, None], 100, "worker");

        let counter = AtomicU32::new(0);
        for _ in 0..3 {
            pool.scope(|s| {
                s.run(|_| {
                    counter.fetch_add(1, Ordering::SeqCst);
                });
            });
        }

        assert_eq!(counter.load(Ordering::SeqCst), 300);
    }

    #[test]
    fn test_scope_runner_order() {
        let mut pool = ParallelismBoundedThreadPool::new(&[None], 1, "worker");

        let flag = AtomicBool::new(false);
        pool.scope(|s| {
            s.run(|_| {
                std::thread::sleep(std::time::Duration::from_millis(10));
                flag.compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst)
                    .unwrap();
            });
            assert_eq!(flag.load(Ordering::SeqCst), false);
        });

        assert_eq!(flag.load(Ordering::SeqCst), true);
    }

    #[test]
    fn test_non_aliasing_borrows() {
        let mut pool = ParallelismBoundedThreadPool::new(&[None, None], 4, "worker");

        let mut counter = 0;
        pool.scope(|s| {
            counter += 1;
            s.run(|_| {
                let _x = counter;
            });
        });

        assert_eq!(counter, 1);
    }

    // should not compile: "cannot assign to `counter` because it is borrowed"
    /// ```compile_fail
    /// # use shadow_rs::core::scheduler::pools::bounded::*;
    /// let mut pool = ParallelismBoundedThreadPool::new(&[None, None], 4, "worker");
    ///
    /// let mut counter = 0;
    /// pool.scope(|s| {
    ///     s.run(|_| {
    ///         let _x = counter;
    ///     });
    ///     counter += 1;
    /// });
    ///
    /// assert_eq!(counter, 1);
    /// ```
    fn _test_aliasing_borrows() {}

    #[test]
    #[should_panic]
    fn test_panic_all() {
        let mut pool = ParallelismBoundedThreadPool::new(&[None, None], 4, "worker");

        pool.scope(|s| {
            s.run(|t| {
                // all threads panic
                panic!("{}", t.thread_idx);
            });
        });
    }

    #[test]
    #[should_panic]
    fn test_panic_single() {
        let mut pool = ParallelismBoundedThreadPool::new(&[None, None], 4, "worker");

        pool.scope(|s| {
            s.run(|t| {
                // one thread panics
                if t.thread_idx == 2 {
                    panic!("{}", t.thread_idx);
                }
            });
        });
    }

    // should not compile: "`x` does not live long enough"
    /// ```compile_fail
    /// # use shadow_rs::core::scheduler::pools::bounded::*;
    /// let mut pool = ParallelismBoundedThreadPool::new(&[None, None], 4, "worker");
    ///
    /// let x = 5;
    /// pool.scope(|s| {
    ///     s.run(|_| {
    ///         std::panic::panic_any(&x);
    ///     });
    /// });
    /// ```
    fn _test_panic_any() {}

    // should not compile: "closure may outlive the current function, but it borrows `x`, which is
    // owned by the current function"
    /// ```compile_fail
    /// # use shadow_rs::core::scheduler::pools::bounded::*;
    /// let mut pool = ParallelismBoundedThreadPool::new(&[None, None], 4, "worker");
    ///
    /// pool.scope(|s| {
    ///     // 'x' will be dropped when the closure is dropped, but 's' lives longer than that
    ///     let x = 5;
    ///     s.run(|_| {
    ///         let _x = x;
    ///     });
    /// });
    /// ```
    fn _test_scope_lifetime() {}

    #[test]
    fn test_queues() {
        let num_threads = 4;
        let mut pool = ParallelismBoundedThreadPool::new(&[None, None], num_threads, "worker");

        // a non-copy usize wrapper
        struct Wrapper(usize);

        let queues: Vec<_> = (0..num_threads)
            .map(|_| crossbeam::queue::SegQueue::<Wrapper>::new())
            .collect();

        // queues[0] has Wrapper(0), queues[1] has Wrapper(1), etc
        for (i, queue) in queues.iter().enumerate() {
            queue.push(Wrapper(i));
        }

        let num_iters = 3;
        for _ in 0..num_iters {
            pool.scope(|s| {
                s.run(|t| {
                    // take item from queue n and push it to queue n+1
                    let wrapper = queues[t.thread_idx].pop().unwrap();
                    queues[(t.thread_idx + 1) % num_threads].push(wrapper);
                });
            });
        }

        for (i, queue) in queues.iter().enumerate() {
            assert_eq!(
                queue.pop().unwrap().0,
                i.wrapping_sub(num_iters) % num_threads
            );
        }
    }
}
