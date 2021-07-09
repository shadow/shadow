use std::collections::HashMap;
use std::sync::Mutex;
use nix::{sys::{signal::Signal, wait::WaitStatus}, unistd::Pid};
use once_cell::sync::OnceCell;

#[derive(Copy, Clone)]
pub enum ExitStatus {
    Exited(i32),
    Signaled(Signal)
}

pub struct ChildPidWatcher {
    inner: Mutex<Inner>,
}

struct Inner {
    callbacks: HashMap<Pid, Vec<Box<dyn Send + FnOnce(Pid, ExitStatus)>>>,
    // To handle race conditions of a child exiting before all callbacks are
    // registered, we save the exit status of all exited children until
    // explicitly unregistered.
    zombies: HashMap<Pid, ExitStatus>,
}

fn watcher_thread_loop() {
    loop {
        let wait_status = nix::sys::wait::wait().unwrap();
        let (pid, exit_status) = match wait_status {
            WaitStatus::Exited(pid, status) => (pid, ExitStatus::Exited(status)),
            WaitStatus::Signaled(pid,signal, _core) => (pid, ExitStatus::Signaled(signal)),
            _ => panic!("Unexpected WaitStatus {:?}", wait_status),
        };
        if let Some(callbacks) = {
            let mut inner = ChildPidWatcher::get().inner.lock().unwrap();
            inner.zombies.insert(pid, exit_status);
            inner.callbacks.remove(&pid)
        } {
            for c in callbacks {
                c(pid, exit_status);
            }
        }
    }
}

impl ChildPidWatcher {
    /// Get a reference to the global singleton watcher.
    pub fn get() -> &'static Self {
        static WATCHER: OnceCell<ChildPidWatcher> = OnceCell::new();
        WATCHER.get_or_init(|| {
            std::thread::Builder::new().name("child-pid-watcher".into()).spawn(watcher_thread_loop).unwrap();
            ChildPidWatcher  { inner: Mutex::new(Inner { callbacks: HashMap::new(), zombies: HashMap::new() })}
        })
    }

    /// Call `callback` exactly once after the child `pid` has exited. All child
    /// `pid`'s are eligible until if and when `childpidwatcher_unwatch` has
    /// been called for them.
    ///
    /// No other code may capture the exit transition via `wait` etc. (But
    /// catching e.g. ptrace stops is ok).
    pub fn watch(&self, pid: Pid, callback: Box<dyn Send + FnOnce(Pid, ExitStatus)>) {
        let mut inner = self.inner.lock().unwrap();
        if let Some(x) = inner.zombies.get(&pid).map(|x| *x) {
            // Call the callback, taking care not to hold the mutex while doing so.
            drop(inner);
            callback(pid, x);
        } else {
            let callbacks = inner.callbacks.entry(pid).or_default();
            callbacks.push(callback);
        }
    }

    /// Unregister interest in the given pid, recovering internal resources etc.
    pub fn unwatch(&self, pid: Pid) {
        let mut inner = self.inner.lock().unwrap();
        inner.zombies.remove(&pid);
        inner.callbacks.remove(&pid);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use nix::unistd::{fork, ForkResult};
    use std::sync::{Arc, Barrier};
    use std::sync::atomic::{AtomicI32, Ordering};

    #[test]
    fn register_before_exit() {
        // Used to wait until after the ChildPidWatcher has ran our callback
        let callback_ran_barrier= Arc::new(Barrier::new(2));
        // Child exit code, to be written from the ChildPidWatcher callback.
        let exit_code = Arc::new(AtomicI32::new(-1));
        // Pipe used to synchronize the child process.
        let (read_fd, write_fd) = nix::unistd::pipe().unwrap();

        let child= match unsafe { fork() }.unwrap() {
            ForkResult::Parent { child } => child,
            ForkResult::Child => {
                let mut buf = [0u8];
                // Wait for parent to register its callback.
                nix::unistd::read(read_fd, &mut buf).unwrap();
                unsafe { libc::_exit(42) }
            }
        };
        {
            let callback_ran_barrier = callback_ran_barrier.clone();
            let exit_code = exit_code.clone();
            ChildPidWatcher::get().watch(child, Box::new(move |pid, exit_status| {
                assert_eq!(pid, child);
                exit_code.store(match exit_status {
                    ExitStatus::Exited(i) => i,
                    ExitStatus::Signaled(s) => panic!("Unexpected exit via signal {:?}", s),
                }, Ordering::Relaxed);
                callback_ran_barrier.wait();
            }));
        }
        // Let the child exit.
        nix::unistd::write(write_fd, &[0u8]).unwrap();

        // Wait for our callback to run.
        callback_ran_barrier.wait();

        ChildPidWatcher::get().unwatch(child);
        assert_eq!(exit_code.load(Ordering::Relaxed), 42);
    }

    #[test]
    fn register_after_exit() {
        // child exit code, to be written by callback in ChildPidWatcher thread.
        let exit_code = Arc::new(AtomicI32::new(-1));

        let child= match unsafe { fork() }.unwrap() {
            ForkResult::Parent { child } => child,
            ForkResult::Child => {
                unsafe { libc::_exit(42) }
            }
        };
        // Wait until child is dead. We can't use waitpid, since that'd "steal" the notification
        // from the ChildPidWatcher.
        while nix::sys::signal::kill(child, None).is_ok() {
            unsafe { libc::sched_yield(); }
        }
        {
            let exit_code = exit_code.clone();
            ChildPidWatcher::get().watch(child, Box::new(move |pid, exit_status| {
                assert_eq!(pid, child);
                exit_code.store(match exit_status {
                    ExitStatus::Exited(i) => i,
                    ExitStatus::Signaled(s) => panic!("Unexpected exit via signal {:?}", s),
                }, Ordering::Relaxed);
            }));
        }
        assert_eq!(exit_code.load(Ordering::Relaxed), 42);
        ChildPidWatcher::get().unwatch(child);
    }
}

mod export {
    use super::*;
    use crate::{cshadow, utility::SyncSendPointer};

    /// Call `callback` exactly once after the child `pid` has exited. All child
    /// `pid`'s are eligible until if and when `childpidwatcher_unwatch` has
    /// been called for them.
    ///
    /// No other code may capture the exit transition via `wait` etc. (But
    /// catching e.g. ptrace stops is ok).
    #[no_mangle]
    pub unsafe extern "C" fn childpidwatcher_watch(
        pid: libc::pid_t,
        callback: extern fn(libc::pid_t, exit_status: i32, *mut libc::c_void),
        data: *mut libc::c_void){
            let data = SyncSendPointer(data);
        ChildPidWatcher::get().watch(Pid::from_raw(pid), Box::new(move |pid, exit_status| {
            let exit_code = match exit_status {
                ExitStatus::Exited(i) => i,
                ExitStatus::Signaled(s) => unsafe { cshadow::return_code_for_signal(s as i32)},
            };
            callback(pid.into(), exit_code, data.ptr())
        }
    ))
    }

    /// Unregister interest in the given pid, recovering internal resources etc.
    #[no_mangle]
    pub unsafe extern "C" fn childpidwatcher_unwatch(pid: libc::pid_t) {
        ChildPidWatcher::get().unwatch(Pid::from_raw(pid));
    }
}