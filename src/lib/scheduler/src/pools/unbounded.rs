// When comparing a loaded value that happens to be bool,
// assert_eq! reads better than assert!.
#![allow(clippy::bool_assert_comparison)]

use std::marker::PhantomData;
use std::ops::Deref;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use atomic_refcell::AtomicRefCell;

use crate::sync::count_down_latch::{self, build_count_down_latch};
use crate::sync::simple_latch;

// If making substantial changes to this scheduler, you should verify the compilation error message
// for each test at the end of this file to make sure that they correctly cause the expected
// compilation error.  This work pool unsafely transmutes the task closure lifetime, and the
// commented tests are meant to make sure that the work pool does not allow unsound code to compile.
// Due to lifetime sub-typing/variance, rust will sometimes allow closures with shorter or longer
// lifetimes than we specify in the API, so the tests check to make sure the closures are invariant
// over the lifetime and that the usage is sound.

/// A task that is run by the pool threads.
pub trait TaskFn: Fn(usize) + Send + Sync {}
impl<T> TaskFn for T where T: Fn(usize) + Send + Sync {}

/// A thread pool that runs a task on many threads. A task will run once on each thread.
pub struct UnboundedThreadPool {
    /// Handles for joining threads when they've exited.
    thread_handles: Vec<std::thread::JoinHandle<()>>,
    /// State shared between all threads.
    shared_state: Arc<SharedState>,
    /// A latch that is opened when the task is set. Indicates to the threads that they should start
    /// running the task.
    task_start_latch: simple_latch::Latch,
    /// The main thread uses this to wait for the threads to finish running the task.
    task_end_waiter: count_down_latch::LatchWaiter,
}

pub struct SharedState {
    /// The task to run during the next round.
    task: AtomicRefCell<Option<Box<dyn TaskFn>>>,
    /// Has a thread panicked?
    has_thread_panicked: AtomicBool,
}

impl UnboundedThreadPool {
    pub fn new(num_threads: usize, thread_name: &str, yield_spin: bool) -> Self {
        let shared_state = Arc::new(SharedState {
            task: AtomicRefCell::new(None),
            has_thread_panicked: AtomicBool::new(false),
        });

        let (task_end_counter, task_end_waiter) = build_count_down_latch();
        let mut task_start_latch = simple_latch::Latch::new();

        let mut thread_handles = Vec::new();

        for i in 0..num_threads {
            let shared_state_clone = Arc::clone(&shared_state);

            // enabling spinning on the threads may improve performance under some conditions
            // (see https://github.com/shadow/shadow/issues/2877)
            let task_start_waiter = task_start_latch.waiter(yield_spin);

            let task_end_counter_clone = task_end_counter.clone();

            let handle = std::thread::Builder::new()
                .name(thread_name.to_string())
                .spawn(move || {
                    work_loop(
                        i,
                        shared_state_clone,
                        task_start_waiter,
                        task_end_counter_clone,
                    )
                })
                .unwrap();

            thread_handles.push(handle);
        }

        Self {
            thread_handles,
            shared_state,
            task_start_latch,
            task_end_waiter,
        }
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

        // start the threads
        self.task_start_latch.open();

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

impl std::ops::Drop for UnboundedThreadPool {
    fn drop(&mut self) {
        self.join_internal();
    }
}

struct WorkerScope<'scope> {
    pool: &'scope mut UnboundedThreadPool,
    // when we are dropped, it's like dropping the task
    _phantom: PhantomData<Box<dyn TaskFn + 'scope>>,
}

impl<'a> std::ops::Drop for WorkerScope<'a> {
    fn drop(&mut self) {
        // if the task was set (if `TaskRunner::run` was called)
        if self.pool.shared_state.task.borrow().is_some() {
            // wait for the task to complete
            self.pool.task_end_waiter.wait();

            // clear the task
            *self.pool.shared_state.task.borrow_mut() = None;

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

impl<'a, 'scope> TaskRunner<'a, 'scope> {
    /// Run a task on the pool's threads.
    pub fn run(self, f: impl TaskFn + 'scope) {
        let f = Box::new(f);

        // SAFETY: WorkerScope will drop this TaskFn before the end of 'scope
        let f = unsafe {
            std::mem::transmute::<Box<dyn TaskFn + 'scope>, Box<dyn TaskFn + 'static>>(f)
        };

        *self.scope.pool.shared_state.task.borrow_mut() = Some(f);

        // we've set the task, so start the threads
        self.scope.pool.task_start_latch.open();
    }
}

fn work_loop(
    thread_index: usize,
    shared_state: Arc<SharedState>,
    mut start_waiter: simple_latch::LatchWaiter,
    mut end_counter: count_down_latch::LatchCounter,
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

    impl<'a> std::ops::Drop for PoisonWhenDropped<'a> {
        fn drop(&mut self) {
            // if we panicked, then inform other threads that we panicked and allow them to exit
            // gracefully
            self.0.has_thread_panicked.store(true, Ordering::Relaxed);
        }
    }

    let shared_state = shared_state.as_ref();
    let poison_when_dropped = PoisonWhenDropped(shared_state);

    loop {
        // wait for a new task
        start_waiter.wait();

        // scope used to make sure we drop the task before counting down
        {
            // run the task
            match shared_state.task.borrow().deref() {
                Some(task) => (task)(thread_index),
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

#[cfg(any(test, doctest))]
mod tests {
    use std::sync::atomic::{AtomicBool, AtomicU32};

    use super::*;

    #[test]
    fn test_scope() {
        let mut pool = UnboundedThreadPool::new(4, "worker", false);

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
        let mut pool = UnboundedThreadPool::new(4, "worker", false);

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
        let mut pool = UnboundedThreadPool::new(100, "worker", false);

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
        let mut pool = UnboundedThreadPool::new(1, "worker", false);

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
        let mut pool = UnboundedThreadPool::new(4, "worker", false);

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
    /// # use shadow_rs::core::scheduler::pools::unbounded::*;
    /// let x = 5;
    /// let mut pool = UnboundedThreadPool::new(4, "worker", false);
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
        let mut pool = UnboundedThreadPool::new(4, "worker", false);

        pool.scope(|s| {
            s.run(|i| {
                // all threads panic
                panic!("{}", i);
            });
        });
    }

    #[test]
    #[should_panic]
    fn test_panic_single() {
        let mut pool = UnboundedThreadPool::new(4, "worker", false);

        pool.scope(|s| {
            s.run(|i| {
                // one thread panics
                if i == 2 {
                    panic!("{}", i);
                }
            });
        });
    }

    // should not compile: "`x` does not live long enough"
    /// ```compile_fail
    /// # use shadow_rs::core::scheduler::pools::unbounded::*;
    /// let mut pool = UnboundedThreadPool::new(4, "worker", false);
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
    /// # use shadow_rs::core::scheduler::pools::unbounded::*;
    /// let mut pool = UnboundedThreadPool::new(4, "worker", false);
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
        let mut pool = UnboundedThreadPool::new(num_threads, "worker", false);

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
                s.run(|i: usize| {
                    // take item from queue n and push it to queue n+1
                    let wrapper = queues[i].pop().unwrap();
                    queues[(i + 1) % num_threads].push(wrapper);
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
