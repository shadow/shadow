use nix::errno::Errno;
use nix::sys::epoll::{
    epoll_create1, epoll_ctl, epoll_wait, EpollCreateFlags, EpollEvent, EpollFlags, EpollOp,
};
use nix::unistd::close;
use nix::unistd::Pid;
use std::collections::HashMap;
use std::convert::{TryFrom, TryInto};
use std::os::unix::io::RawFd;
use std::sync::Arc;
use std::sync::Mutex;
use std::thread;

/// Utility for monitoring a set of child pid's, calling registered callbacks
/// when one exits or is killed. Starts a background thread, which is shut down
/// when the object is dropped.
pub struct ChildPidWatcher {
    inner: Arc<Mutex<Inner>>,
    epoll: std::os::unix::io::RawFd,
}

pub type WatchHandle = u64;

enum Command {
    RunCallbacks(Pid),
    Finish,
}

struct Inner {
    // Next unique handle ID.
    next_handle: WatchHandle,
    // Pending commands for watcher thread.
    commands: Vec<Command>,
    // For each monitored pid, the fd obtained via `pidfd_open`.
    pidfds: HashMap<Pid, RawFd>,
    // Registered callbacks.
    callbacks: HashMap<Pid, HashMap<WatchHandle, Box<dyn Send + FnOnce(Pid)>>>,
    // event_fd used to notify watcher thread via epoll. Calling thread writes a
    // single byte, which the watcher thread reads to reset.
    command_notifier: RawFd,
    thread_handle: Option<thread::JoinHandle<()>>,
}

impl Inner {
    fn send_command(&mut self, cmd: Command) {
        self.commands.push(cmd);
        nix::unistd::write(self.command_notifier, &1u64.to_ne_bytes()).unwrap();
    }

    fn unwatch_pid(&mut self, epoll: RawFd, pid: Pid) {
        self.callbacks.remove(&pid);
        if let Some(pidfd) = self.pidfds.remove(&pid) {
            epoll_ctl(epoll, EpollOp::EpollCtlDel, pidfd, None).unwrap();
            close(pidfd).unwrap();
        }
    }

    fn run_callbacks_for_fd(&mut self, epoll: RawFd, pid: Pid) {
        if let Some(callback_map) = self.callbacks.remove(&pid) {
            for (_handle, cb) in callback_map {
                cb(pid)
            }
        }
        self.unwatch_pid(epoll, pid);
    }
}

// Panics if pid doesn't exist at all.
fn is_zombie(pid: Pid) -> bool {
    let stat_name = format!("/proc/{}/stat", pid.as_raw());
    let contents = std::fs::read_to_string(stat_name).unwrap();
    contents.contains(") Z")
}

impl ChildPidWatcher {
    /// Create a ChildPidWatcher. Spawns a background thread, which is joined
    /// when the object is dropped.
    pub fn new() -> Self {
        let epoll = epoll_create1(EpollCreateFlags::empty()).unwrap();
        let command_notifier =
            nix::sys::eventfd::eventfd(0, nix::sys::eventfd::EfdFlags::EFD_NONBLOCK).unwrap();
        let mut event = EpollEvent::new(EpollFlags::EPOLLIN, 0);
        epoll_ctl(
            epoll,
            EpollOp::EpollCtlAdd,
            command_notifier,
            Some(&mut event),
        )
        .unwrap();
        let watcher = ChildPidWatcher {
            inner: Arc::new(Mutex::new(Inner {
                next_handle: 1,
                pidfds: HashMap::new(),
                callbacks: HashMap::new(),
                commands: Vec::new(),
                command_notifier,
                thread_handle: None,
            })),
            epoll,
        };
        let thread_handle = {
            let inner = Arc::clone(&watcher.inner);
            let epoll = watcher.epoll;
            thread::Builder::new()
                .name("child-pid-watcher".into())
                .spawn(move || ChildPidWatcher::thread_loop(&*inner, epoll))
                .unwrap()
        };
        watcher.inner.lock().unwrap().thread_handle = Some(thread_handle);
        watcher
    }

    fn thread_loop(inner: &Mutex<Inner>, epoll: RawFd) {
        let mut events = [EpollEvent::empty(); 10];
        let mut commands = Vec::new();
        let mut done = false;
        while !done {
            let nevents = match epoll_wait(epoll, &mut events, -1) {
                Ok(n) => n,
                Err(Errno::EINTR) => {
                    // Just try again.
                    continue;
                }
                Err(e) => panic!("epoll_wait: {:?}", e),
            };

            // We hold the lock the whole time we're processing events. While it'd
            // be nice to avoid holding it while executing callbacks (and therefor
            // not require that callbacks don't call ChildPidWatcher APIs), that'd
            // make it difficult to guarantee a callback *won't* be run if the
            // caller unregisters it.
            let mut inner = inner.lock().unwrap();

            for event in &events[0..nevents] {
                let pid = Pid::from_raw(i32::try_from(event.data()).unwrap());
                inner.run_callbacks_for_fd(epoll, pid);
            }
            // Reading an eventfd always returns an 8 byte integer. Do so to ensure it's
            // no longer marked 'readable'.
            let mut buf = [0; 8];
            let res = nix::unistd::read(inner.command_notifier, &mut buf);
            debug_assert!(match res {
                Ok(8) => true,
                Ok(i) => panic!("Unexpected read size {}", i),
                Err(Errno::EAGAIN) => true,
                Err(e) => panic!("Unexpected error {:?}", e),
            });
            // Run commands
            std::mem::swap(&mut commands, &mut inner.commands);
            for cmd in commands.drain(..) {
                match cmd {
                    Command::RunCallbacks(pid) => {
                        inner.run_callbacks_for_fd(epoll, pid);
                    }
                    Command::Finish => {
                        done = true;
                        // There could be more commands queued and/or more epoll
                        // events ready, but it doesn't matter. We don't
                        // guarantee to callers whether callbacks have run or
                        // not after having sent `Finish`; only that no more
                        // callbacks will run after the thread is joined.
                        break;
                    }
                }
            }
        }
    }

    /// Call `callback` exactly once from another thread after the child `pid`
    /// has exited, including if it has already exited. Does *not* reap the
    /// child itself.
    ///
    /// The returned handle is guaranteed to be non-zero.
    ///
    /// Panics if `pid` doesn't exist.
    pub fn watch(&self, pid: Pid, callback: impl Send + FnOnce(Pid) + 'static) -> WatchHandle {
        let mut inner = self.inner.lock().unwrap();
        if !inner.pidfds.contains_key(&pid) {
            let pidfd: RawFd = unsafe { libc::syscall(libc::SYS_pidfd_open, pid.as_raw(), 0) }
                .try_into()
                .unwrap();
            if pidfd < 0 {
                panic!("pidfd_open: {:?}", nix::errno::Errno::last());
            }
            inner.pidfds.insert(pid, pidfd);
            let mut event = EpollEvent::new(EpollFlags::EPOLLIN, pid.as_raw().try_into().unwrap());
            epoll_ctl(self.epoll, EpollOp::EpollCtlAdd, pidfd, Some(&mut event)).unwrap();
            if is_zombie(pid) {
                // Child is dead. If it died before adding it to the epoll,
                // we'll never get an event. Notify via the command notifier.
                inner.send_command(Command::RunCallbacks(pid));
            }
        };
        let handle = inner.next_handle;
        inner.next_handle += 1;
        let callbacks = inner.callbacks.entry(pid).or_default();
        callbacks.insert(handle, Box::new(callback));
        handle
    }

    /// Unregisters a callback. After returning, the corresponding callback is
    /// guaranteed either to have already run, or to never run. i.e. it's safe to
    /// free data that the callback might otherwise access.
    ///
    /// Calling with pids or handles that no longer exist is safe.
    pub fn unwatch(&self, pid: Pid, handle: WatchHandle) {
        let mut inner = self.inner.lock().unwrap();
        if let Some(callbacks) = inner.callbacks.get_mut(&pid) {
            callbacks.remove(&handle);
            if callbacks.is_empty() {
                inner.unwatch_pid(self.epoll, pid);
            }
        }
    }
}

impl Drop for ChildPidWatcher {
    fn drop(&mut self) {
        let handle = {
            let mut inner = self.inner.lock().unwrap();
            inner.send_command(Command::Finish);
            inner.thread_handle.take().unwrap()
        };
        handle.join().unwrap();
        let inner = self.inner.lock().unwrap();
        for (_pid, fd) in inner.pidfds.iter() {
            nix::unistd::close(*fd).unwrap();
        }
        nix::unistd::close(self.epoll).unwrap();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use nix::sys::wait::WaitStatus;
    use nix::sys::wait::{waitpid, WaitPidFlag};
    use nix::unistd::{fork, ForkResult};
    use std::sync::{Arc, Condvar};

    #[test]
    fn register_before_exit() {
        // Pipe used to synchronize the child process.
        let (read_fd, write_fd) = nix::unistd::pipe().unwrap();

        let child = match unsafe { fork() }.unwrap() {
            ForkResult::Parent { child } => child,
            ForkResult::Child => {
                let mut buf = [0u8];
                // Wait for parent to register its callback.
                nix::unistd::read(read_fd, &mut buf).unwrap();
                unsafe { libc::_exit(42) }
            }
        };

        let watcher = ChildPidWatcher::new();
        let callback_ran = Arc::new((Mutex::new(false), Condvar::new()));
        {
            let callback_ran = callback_ran.clone();
            watcher.watch(
                child,
                Box::new(move |pid| {
                    assert_eq!(pid, child);
                    *callback_ran.0.lock().unwrap() = true;
                    callback_ran.1.notify_all();
                }),
            );
        }

        // Child should still be alive.
        assert_eq!(
            waitpid(child, Some(WaitPidFlag::WNOHANG)).unwrap(),
            WaitStatus::StillAlive
        );

        // Callback shouldn't have run yet.
        assert!(!*callback_ran.0.lock().unwrap());

        // Let the child exit.
        nix::unistd::write(write_fd, &[0u8]).unwrap();

        // Wait for our callback to run.
        let mut callback_ran_lock = callback_ran.0.lock().unwrap();
        while !*callback_ran_lock {
            callback_ran_lock = callback_ran.1.wait(callback_ran_lock).unwrap();
        }

        // Child should be ready to be reaped.
        assert_eq!(
            waitpid(child, Some(WaitPidFlag::WNOHANG)).unwrap(),
            WaitStatus::Exited(child, 42)
        );
    }

    #[test]
    fn register_after_exit() {
        let child = match unsafe { fork() }.unwrap() {
            ForkResult::Parent { child } => child,
            ForkResult::Child => unsafe { libc::_exit(42) },
        };

        // Wait until child is dead, but don't reap it yet.
        while !is_zombie(child) {
            unsafe {
                libc::sched_yield();
            }
        }

        let watcher = ChildPidWatcher::new();

        // Used to wait until after the ChildPidWatcher has ran our callback
        let callback_ran = Arc::new((Mutex::new(false), Condvar::new()));
        {
            let callback_ran = callback_ran.clone();
            watcher.watch(
                child,
                Box::new(move |pid| {
                    assert_eq!(pid, child);
                    *callback_ran.0.lock().unwrap() = true;
                    callback_ran.1.notify_all();
                }),
            );
        }

        // Wait for our callback to run.
        let mut callback_ran_lock = callback_ran.0.lock().unwrap();
        while !*callback_ran_lock {
            callback_ran_lock = callback_ran.1.wait(callback_ran_lock).unwrap();
        }

        // Child should be ready to be reaped.
        assert_eq!(
            waitpid(child, Some(WaitPidFlag::WNOHANG)).unwrap(),
            WaitStatus::Exited(child, 42)
        );
    }

    #[test]
    fn register_multiple() {
        let cb1_ran = Arc::new((Mutex::new(false), Condvar::new()));
        let cb2_ran = Arc::new((Mutex::new(false), Condvar::new()));

        let child = match unsafe { fork() }.unwrap() {
            ForkResult::Parent { child } => child,
            ForkResult::Child => unsafe { libc::_exit(42) },
        };

        let watcher = ChildPidWatcher::new();

        for cb_ran in vec![cb1_ran.clone(), cb2_ran.clone()].drain(..) {
            let cb_ran = cb_ran.clone();
            watcher.watch(
                child,
                Box::new(move |pid| {
                    assert_eq!(pid, child);
                    *cb_ran.0.lock().unwrap() = true;
                    cb_ran.1.notify_all();
                }),
            );
        }

        for cb_ran in vec![cb1_ran, cb2_ran].drain(..) {
            let mut cb_ran_lock = cb_ran.0.lock().unwrap();
            while !*cb_ran_lock {
                cb_ran_lock = cb_ran.1.wait(cb_ran_lock).unwrap();
            }
        }

        // Child should be ready to be reaped.
        assert_eq!(
            waitpid(child, Some(WaitPidFlag::WNOHANG)).unwrap(),
            WaitStatus::Exited(child, 42)
        );
    }

    #[test]
    fn unregister_one() {
        let cb1_ran = Arc::new((Mutex::new(false), Condvar::new()));
        let cb2_ran = Arc::new((Mutex::new(false), Condvar::new()));

        // Pipe used to synchronize the child process.
        let (read_fd, write_fd) = nix::unistd::pipe().unwrap();

        let child = match unsafe { fork() }.unwrap() {
            ForkResult::Parent { child } => child,
            ForkResult::Child => {
                let mut buf = [0u8];
                // Wait for parent to register its callback.
                nix::unistd::read(read_fd, &mut buf).unwrap();
                unsafe { libc::_exit(42) }
            }
        };

        let watcher = ChildPidWatcher::new();

        let handles: Vec<WatchHandle> = [&cb1_ran, &cb2_ran]
            .iter()
            .cloned()
            .map(|cb_ran| {
                let cb_ran = cb_ran.clone();
                watcher.watch(
                    child,
                    Box::new(move |pid| {
                        assert_eq!(pid, child);
                        *cb_ran.0.lock().unwrap() = true;
                        cb_ran.1.notify_all();
                    }),
                )
            })
            .collect();

        watcher.unwatch(child, handles[0]);

        // Let the child exit.
        nix::unistd::write(write_fd, &[0u8]).unwrap();

        // Wait for the still-registered callback to run.
        let mut cb_ran_lock = cb2_ran.0.lock().unwrap();
        while !*cb_ran_lock {
            cb_ran_lock = cb2_ran.1.wait(cb_ran_lock).unwrap();
        }

        // The unregistered cb should *not* have run.
        assert!(!*cb1_ran.0.lock().unwrap());

        // Child should be ready to be reaped.
        assert_eq!(
            waitpid(child, Some(WaitPidFlag::WNOHANG)).unwrap(),
            WaitStatus::Exited(child, 42)
        );
    }
}

mod export {
    use super::*;
    use crate::utility::notnull::*;
    use crate::utility::SyncSendPointer;

    #[no_mangle]
    pub unsafe extern "C" fn childpidwatcher_new() -> *mut ChildPidWatcher {
        Box::into_raw(Box::new(ChildPidWatcher::new()))
    }

    #[no_mangle]
    pub unsafe extern "C" fn childpidwatcher_free(watcher: *mut ChildPidWatcher) {
        unsafe { Box::from_raw(notnull_mut(watcher)) };
    }

    /// Call `callback` exactly once from another thread after the child `pid`
    /// has exited, including if it has already exited. Does *not* reap the
    /// child itself.
    ///
    /// The returned handle is guaranteed to be non-zero.
    ///
    /// Panics if `pid` doesn't exist.
    #[no_mangle]
    pub unsafe extern "C" fn childpidwatcher_watch(
        watcher: *const ChildPidWatcher,
        pid: libc::pid_t,
        callback: extern "C" fn(libc::pid_t, *mut libc::c_void),
        data: *mut libc::c_void,
    ) -> WatchHandle {
        let data = SyncSendPointer(data);
        unsafe { watcher.as_ref() }
            .unwrap()
            .watch(Pid::from_raw(pid), move |pid| {
                callback(pid.into(), data.ptr())
            })
    }

    /// Unregisters a callback. After returning, the corresponding callback is
    /// guaranteed either to have already run, or to never run. i.e. it's safe to
    /// free data that the callback might otherwise access.
    ///
    /// Calling with pids or handles that no longer exist is safe.
    #[no_mangle]
    pub unsafe extern "C" fn childpidwatcher_unwatch(
        watcher: *const ChildPidWatcher,
        pid: libc::pid_t,
        handle: WatchHandle,
    ) {
        unsafe { watcher.as_ref() }
            .unwrap()
            .unwatch(Pid::from_raw(pid), handle);
    }
}
