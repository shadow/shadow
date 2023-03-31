use std::collections::HashMap;
use std::fs::File;
use std::os::unix::prelude::{AsRawFd, FromRawFd};
use std::sync::Arc;
use std::sync::Mutex;
use std::thread;

use nix::errno::Errno;
use nix::fcntl::{FcntlArg, FdFlag, OFlag};
use nix::sys::epoll::{
    epoll_create1, epoll_ctl, epoll_wait, EpollCreateFlags, EpollEvent, EpollFlags, EpollOp,
};
use nix::unistd::Pid;

/// Utility for monitoring a set of child pid's, calling registered callbacks
/// when one exits or is killed. Starts a background thread, which is shut down
/// when the object is dropped.
#[derive(Debug)]
pub struct ChildPidWatcher {
    inner: Arc<Mutex<Inner>>,
    epoll: Arc<File>,
}

pub type WatchHandle = u64;

#[derive(Debug)]
enum Command {
    RunCallbacks(Pid),
    UnregisterPid(Pid),
    Finish,
}

struct PidData {
    // Registered callbacks.
    callbacks: HashMap<WatchHandle, Box<dyn Send + FnOnce(Pid)>>,
    // After the pid has exited, this fd is closed and set to None.
    fd: Option<File>,
    // Whether this pid has been unregistered. The whole struct is removed after
    // both the pid is unregistered, and `callbacks` is empty.
    unregistered: bool,
}

#[derive(Debug)]
struct Inner {
    // Next unique handle ID.
    next_handle: WatchHandle,
    // Pending commands for watcher thread.
    commands: Vec<Command>,
    // Data for each monitored pid.
    pids: HashMap<Pid, PidData>,
    // event_fd used to notify watcher thread via epoll. Calling thread writes a
    // single byte, which the watcher thread reads to reset.
    command_notifier: File,
    thread_handle: Option<thread::JoinHandle<()>>,
}

impl Inner {
    fn send_command(&mut self, cmd: Command) {
        self.commands.push(cmd);
        nix::unistd::write(self.command_notifier.as_raw_fd(), &1u64.to_ne_bytes()).unwrap();
    }

    fn unwatch_pid(&mut self, epoll: &File, pid: Pid) {
        let Some(piddata) = self.pids.get_mut(&pid) else {
            // Already unregistered the pid
            return;
        };
        let Some(fd) = piddata.fd.take() else {
            // Already unwatched the pid
            return;
        };
        epoll_ctl(
            epoll.as_raw_fd(),
            EpollOp::EpollCtlDel,
            fd.as_raw_fd(),
            None,
        )
        .unwrap();
    }

    fn pid_has_exited(&self, pid: Pid) -> bool {
        self.pids.get(&pid).unwrap().fd.is_none()
    }

    fn remove_pid(&mut self, epoll: &File, pid: Pid) {
        debug_assert!(self.should_remove_pid(pid));
        self.unwatch_pid(epoll, pid);
        self.pids.remove(&pid);
    }

    fn run_callbacks_for_pid(&mut self, pid: Pid) {
        for (_handle, cb) in self.pids.get_mut(&pid).unwrap().callbacks.drain() {
            cb(pid)
        }
    }

    fn should_remove_pid(&mut self, pid: Pid) -> bool {
        let pid_data = self.pids.get(&pid).unwrap();
        pid_data.callbacks.is_empty() && pid_data.unregistered
    }

    fn maybe_remove_pid(&mut self, epoll: &File, pid: Pid) {
        if self.should_remove_pid(pid) {
            self.remove_pid(epoll, pid)
        }
    }
}

impl ChildPidWatcher {
    /// Create a ChildPidWatcher. Spawns a background thread, which is joined
    /// when the object is dropped.
    pub fn new() -> Self {
        let epoll = {
            let raw = epoll_create1(EpollCreateFlags::empty()).unwrap();
            Arc::new(unsafe { File::from_raw_fd(raw) })
        };
        let command_notifier = {
            let raw =
                nix::sys::eventfd::eventfd(0, nix::sys::eventfd::EfdFlags::EFD_NONBLOCK).unwrap();
            unsafe { File::from_raw_fd(raw) }
        };
        let mut event = EpollEvent::new(EpollFlags::EPOLLIN, 0);
        epoll_ctl(
            epoll.as_raw_fd(),
            EpollOp::EpollCtlAdd,
            command_notifier.as_raw_fd(),
            Some(&mut event),
        )
        .unwrap();
        let watcher = ChildPidWatcher {
            inner: Arc::new(Mutex::new(Inner {
                next_handle: 1,
                pids: HashMap::new(),
                commands: Vec::new(),
                command_notifier,
                thread_handle: None,
            })),
            epoll,
        };
        let thread_handle = {
            let inner = Arc::clone(&watcher.inner);
            let epoll = watcher.epoll.clone();
            thread::Builder::new()
                .name("child-pid-watcher".into())
                .spawn(move || ChildPidWatcher::thread_loop(&inner, &epoll))
                .unwrap()
        };
        watcher.inner.lock().unwrap().thread_handle = Some(thread_handle);
        watcher
    }

    fn thread_loop(inner: &Mutex<Inner>, epoll: &File) {
        let mut events = [EpollEvent::empty(); 10];
        let mut commands = Vec::new();
        let mut done = false;
        while !done {
            let nevents = match epoll_wait(epoll.as_raw_fd(), &mut events, -1) {
                Ok(n) => n,
                Err(Errno::EINTR) => {
                    // Just try again.
                    continue;
                }
                Err(e) => panic!("epoll_wait: {:?}", e),
            };

            // We hold the lock the whole time we're processing events. While it'd
            // be nice to avoid holding it while executing callbacks (and therefore
            // not require that callbacks don't call ChildPidWatcher APIs), that'd
            // make it difficult to guarantee a callback *won't* be run if the
            // caller unregisters it.
            let mut inner = inner.lock().unwrap();

            for event in &events[0..nevents] {
                let pid = Pid::from_raw(i32::try_from(event.data()).unwrap());
                // We get an event for pid=0 when there's a write to the command_notifier;
                // Ignore that here and handle below.
                if pid.as_raw() != 0 {
                    inner.unwatch_pid(epoll, pid);
                    inner.run_callbacks_for_pid(pid);
                    inner.maybe_remove_pid(epoll, pid);
                }
            }
            // Reading an eventfd always returns an 8 byte integer. Do so to ensure it's
            // no longer marked 'readable'.
            let mut buf = [0; 8];
            let res = nix::unistd::read(inner.command_notifier.as_raw_fd(), &mut buf);
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
                        debug_assert!(inner.pid_has_exited(pid));
                        inner.run_callbacks_for_pid(pid);
                        inner.maybe_remove_pid(epoll, pid);
                    }
                    Command::UnregisterPid(pid) => {
                        if let Some(pid_data) = inner.pids.get_mut(&pid) {
                            pid_data.unregistered = true;
                            inner.maybe_remove_pid(epoll, pid);
                        }
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

    unsafe fn fork_watchable_internal(
        &self,
        fork_syscall: i64,
        child_fn: impl FnOnce(),
    ) -> Result<Pid, nix::Error> {
        let (read_fd, write_fd) = nix::unistd::pipe2(OFlag::O_CLOEXEC)?;

        // TODO: Allow vfork when Rust supports it:
        assert!(fork_syscall == libc::SYS_fork);
        let raw_pid = unsafe { libc::syscall(fork_syscall) };
        if raw_pid < 0 {
            let rv = Err(Errno::last());
            nix::unistd::close(read_fd).unwrap();
            nix::unistd::close(write_fd).unwrap();
            return rv;
        }
        if raw_pid == 0 {
            // Keep the write-end of the pipe open.
            nix::fcntl::fcntl(write_fd, FcntlArg::F_SETFD(FdFlag::empty())).unwrap();
            child_fn();
            panic!("child_fn shouldn't have returned");
        }
        // Parent doesn't need the write end.
        nix::unistd::close(write_fd).unwrap();

        let pid = Pid::from_raw(raw_pid.try_into().unwrap());
        self.register_pid(pid, unsafe { File::from_raw_fd(read_fd) });

        Ok(pid)
    }

    /// Fork a child and register it. Uses `fork` internally; it `vfork` is desired,
    /// use `register_pid` instead.
    ///
    /// TODO: add a vfork version when Rust supports vfork:
    /// <https://github.com/rust-lang/rust/issues/58314>
    ///
    /// # Safety
    ///
    /// As for fork in Rust in general. *Probably*, *mostly*, safe, since the
    /// child process gets its own copy of the address space and OS resources etc.
    /// Still, there may be some dragons here. Best to call exec before too long
    /// in the child.
    pub unsafe fn fork_watchable(&self, child_fn: impl FnOnce()) -> Result<Pid, nix::Error> {
        unsafe { self.fork_watchable_internal(libc::SYS_fork, child_fn) }
    }

    /// Register interest in `pid`, and associate it with `read_fd`.
    ///
    /// `read_fd` should be the read end of a pipe, whose write end is owned
    /// *solely* by `pid`, causing `read_fd` to become invalid when `pid` exits.
    /// In a multi-threaded program care must be taken to prevent a concurrent
    /// fork from leaking the write end of the pipe into other children. One way
    /// to avoid this is to use O_CLOEXEC when creating the pipe, and then unset
    /// O_CLOEXEC in the child before calling exec.
    ///
    /// Be sure to close the parent's write-end of the pipe.
    ///
    /// Takes ownership of `read_fd`, and will close it when appropriate.
    pub fn register_pid(&self, pid: Pid, read_fd: File) {
        let mut inner = self.inner.lock().unwrap();
        let raw_read_fd = read_fd.as_raw_fd();
        let prev = inner.pids.insert(
            pid,
            PidData {
                callbacks: HashMap::new(),
                fd: Some(read_fd),
                unregistered: false,
            },
        );
        assert!(prev.is_none());
        let mut event = EpollEvent::new(EpollFlags::empty(), pid.as_raw().try_into().unwrap());
        epoll_ctl(
            self.epoll.as_raw_fd(),
            EpollOp::EpollCtlAdd,
            raw_read_fd,
            Some(&mut event),
        )
        .unwrap();
    }

    // TODO: Re-enable when Rust supports vfork: https://github.com/rust-lang/rust/issues/58314
    // pub unsafe fn vfork_watchable(&self, child_fn: impl FnOnce()) -> Result<Pid, nix::Error> {
    //     unsafe { self.fork_watchable_internal(libc::SYS_vfork, child_fn) }
    // }

    /// Unregister the pid. After unregistration, no more callbacks may be
    /// registered for the given pid. Already-registered callbacks will still be
    /// called if and when the pid exits unless individually unregistered.
    ///
    /// Safe to call multiple times.
    pub fn unregister_pid(&self, pid: Pid) {
        // Let the worker handle the actual unregistration. This avoids a race
        // where we unregister a pid at the same time as the worker thread
        // receives an epoll event for it.
        let mut inner = self.inner.lock().unwrap();
        inner.send_command(Command::UnregisterPid(pid));
    }

    /// Call `callback` from another thread after the child `pid`
    /// has exited, including if it has already exited. Does *not* reap the
    /// child itself.
    ///
    /// The returned handle is guaranteed to be non-zero.
    ///
    /// Panics if `pid` isn't registered.
    pub fn register_callback(
        &self,
        pid: Pid,
        callback: impl Send + FnOnce(Pid) + 'static,
    ) -> WatchHandle {
        let mut inner = self.inner.lock().unwrap();
        let handle = inner.next_handle;
        inner.next_handle += 1;
        let pid_data = inner.pids.get_mut(&pid).unwrap();
        assert!(!pid_data.unregistered);
        pid_data.callbacks.insert(handle, Box::new(callback));
        if pid_data.fd.is_none() {
            // pid is already dead. Run the callback we just registered.
            inner.send_command(Command::RunCallbacks(pid));
        }
        handle
    }

    /// Unregisters a callback. After returning, the corresponding callback is
    /// guaranteed either to have already run, or to never run. i.e. it's safe to
    /// free data that the callback might otherwise access.
    ///
    /// No-op if `pid` isn't registered.
    pub fn unregister_callback(&self, pid: Pid, handle: WatchHandle) {
        let mut inner = self.inner.lock().unwrap();
        if let Some(pid_data) = inner.pids.get_mut(&pid) {
            pid_data.callbacks.remove(&handle);
            inner.maybe_remove_pid(&self.epoll, pid);
        }
    }
}

impl Default for ChildPidWatcher {
    fn default() -> Self {
        Self::new()
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
    }
}

impl std::fmt::Debug for PidData {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("PidData")
            .field("fd", &self.fd)
            .field("unregistered", &self.unregistered)
            .finish_non_exhaustive()
    }
}

#[cfg(test)]
mod tests {
    use std::sync::{Arc, Condvar};

    use nix::sys::wait::WaitStatus;
    use nix::sys::wait::{waitpid, WaitPidFlag};

    use super::*;

    fn is_zombie(pid: Pid) -> bool {
        let stat_name = format!("/proc/{}/stat", pid.as_raw());
        let contents = std::fs::read_to_string(stat_name).unwrap();
        contents.contains(") Z")
    }

    #[test]
    // can't call foreign function: pipe
    #[cfg_attr(miri, ignore)]
    fn register_before_exit() {
        let notifier = nix::sys::eventfd::eventfd(0, nix::sys::eventfd::EfdFlags::empty()).unwrap();

        let watcher = ChildPidWatcher::new();
        let child = unsafe {
            watcher.fork_watchable(|| {
                let mut buf = [0; 8];
                // Wait for parent to register its callback.
                nix::unistd::read(notifier, &mut buf).unwrap();
                libc::_exit(42);
            })
        }
        .unwrap();

        let callback_ran = Arc::new((Mutex::new(false), Condvar::new()));
        {
            let callback_ran = callback_ran.clone();
            watcher.register_callback(
                child,
                Box::new(move |pid| {
                    assert_eq!(pid, child);
                    *callback_ran.0.lock().unwrap() = true;
                    callback_ran.1.notify_all();
                }),
            );
        }

        // Should be safe to unregister the pid now.
        // We don't be able to register any more callbacks, but existing one
        // should still work.
        watcher.unregister_pid(child);

        // Child should still be alive.
        assert_eq!(
            waitpid(child, Some(WaitPidFlag::WNOHANG)).unwrap(),
            WaitStatus::StillAlive
        );

        // Callback shouldn't have run yet.
        assert!(!*callback_ran.0.lock().unwrap());

        // Let the child exit.
        nix::unistd::write(notifier, &1u64.to_ne_bytes()).unwrap();

        // Wait for our callback to run.
        let mut callback_ran_lock = callback_ran.0.lock().unwrap();
        while !*callback_ran_lock {
            callback_ran_lock = callback_ran.1.wait(callback_ran_lock).unwrap();
        }

        // Child should be ready to be reaped.
        // TODO: use WNOHANG here if we go back to a pidfd-based implementation.
        // With the current fd-based implementation we may be notified before kernel
        // marks the child reapable.
        assert_eq!(waitpid(child, None).unwrap(), WaitStatus::Exited(child, 42));
    }

    #[test]
    // can't call foreign function: pipe
    #[cfg_attr(miri, ignore)]
    fn register_after_exit() {
        let (read_fd, write_fd) = nix::unistd::pipe().unwrap();
        let child = match unsafe { nix::unistd::fork() }.unwrap() {
            nix::unistd::ForkResult::Parent { child } => {
                nix::unistd::close(write_fd).unwrap();
                child
            }
            nix::unistd::ForkResult::Child => {
                nix::unistd::close(read_fd).unwrap();
                unsafe { libc::_exit(42) };
            }
        };

        // Wait until child is dead, but don't reap it yet.
        while !is_zombie(child) {
            unsafe {
                libc::sched_yield();
            }
        }

        let watcher = ChildPidWatcher::new();
        watcher.register_pid(child, unsafe { File::from_raw_fd(read_fd) });

        // Used to wait until after the ChildPidWatcher has ran our callback
        let callback_ran = Arc::new((Mutex::new(false), Condvar::new()));
        {
            let callback_ran = callback_ran.clone();
            watcher.register_callback(
                child,
                Box::new(move |pid| {
                    assert_eq!(pid, child);
                    *callback_ran.0.lock().unwrap() = true;
                    callback_ran.1.notify_all();
                }),
            );
        }

        // Should be safe to unregister the pid now.
        // We don't be able to register any more callbacks, but existing one
        // should still work.
        watcher.unregister_pid(child);

        // Wait for our callback to run.
        let mut callback_ran_lock = callback_ran.0.lock().unwrap();
        while !*callback_ran_lock {
            callback_ran_lock = callback_ran.1.wait(callback_ran_lock).unwrap();
        }

        // Child should be ready to be reaped.
        // TODO: use WNOHANG here if we go back to a pidfd-based implementation.
        // With the current fd-based implementation we may be notified before kernel
        // marks the child reapable.
        assert_eq!(waitpid(child, None).unwrap(), WaitStatus::Exited(child, 42));
    }

    #[test]
    // can't call foreign function: pipe
    #[cfg_attr(miri, ignore)]
    fn register_multiple() {
        let cb1_ran = Arc::new((Mutex::new(false), Condvar::new()));
        let cb2_ran = Arc::new((Mutex::new(false), Condvar::new()));

        let watcher = ChildPidWatcher::new();
        let child = unsafe {
            watcher.fork_watchable(|| {
                libc::_exit(42);
            })
        }
        .unwrap();

        for cb_ran in vec![cb1_ran.clone(), cb2_ran.clone()].drain(..) {
            let cb_ran = cb_ran.clone();
            watcher.register_callback(
                child,
                Box::new(move |pid| {
                    assert_eq!(pid, child);
                    *cb_ran.0.lock().unwrap() = true;
                    cb_ran.1.notify_all();
                }),
            );
        }

        // Should be safe to unregister the pid now.
        // We don't be able to register any more callbacks, but existing one
        // should still work.
        watcher.unregister_pid(child);

        for cb_ran in vec![cb1_ran, cb2_ran].drain(..) {
            let mut cb_ran_lock = cb_ran.0.lock().unwrap();
            while !*cb_ran_lock {
                cb_ran_lock = cb_ran.1.wait(cb_ran_lock).unwrap();
            }
        }

        // Child should be ready to be reaped.
        // TODO: use WNOHANG here if we go back to a pidfd-based implementation.
        // With the current fd-based implementation we may be notified before kernel
        // marks the child reapable.
        assert_eq!(waitpid(child, None).unwrap(), WaitStatus::Exited(child, 42));
    }

    #[test]
    // can't call foreign function: pipe
    #[cfg_attr(miri, ignore)]
    fn unregister_one() {
        let cb1_ran = Arc::new((Mutex::new(false), Condvar::new()));
        let cb2_ran = Arc::new((Mutex::new(false), Condvar::new()));

        let notifier = nix::sys::eventfd::eventfd(0, nix::sys::eventfd::EfdFlags::empty()).unwrap();

        let watcher = ChildPidWatcher::new();
        let child = unsafe {
            watcher.fork_watchable(|| {
                let mut buf = [0; 8];
                // Wait for parent to register its callback.
                nix::unistd::read(notifier, &mut buf).unwrap();
                libc::_exit(42);
            })
        }
        .unwrap();

        let handles: Vec<WatchHandle> = [&cb1_ran, &cb2_ran]
            .iter()
            .cloned()
            .map(|cb_ran| {
                let cb_ran = cb_ran.clone();
                watcher.register_callback(
                    child,
                    Box::new(move |pid| {
                        assert_eq!(pid, child);
                        *cb_ran.0.lock().unwrap() = true;
                        cb_ran.1.notify_all();
                    }),
                )
            })
            .collect();

        // Should be safe to unregister the pid now.
        // We don't be able to register any more callbacks, but existing one
        // should still work.
        watcher.unregister_pid(child);

        watcher.unregister_callback(child, handles[0]);

        // Let the child exit.
        nix::unistd::write(notifier, &1u64.to_ne_bytes()).unwrap();

        // Wait for the still-registered callback to run.
        let mut cb_ran_lock = cb2_ran.0.lock().unwrap();
        while !*cb_ran_lock {
            cb_ran_lock = cb2_ran.1.wait(cb_ran_lock).unwrap();
        }

        // The unregistered cb should *not* have run.
        assert!(!*cb1_ran.0.lock().unwrap());

        // Child should be ready to be reaped.
        // TODO: use WNOHANG here if we go back to a pidfd-based implementation.
        // With the current fd-based implementation we may be notified before kernel
        // marks the child reapable.
        assert_eq!(waitpid(child, None).unwrap(), WaitStatus::Exited(child, 42));
    }
}

mod export {
    use shadow_shim_helper_rs::notnull::*;

    use super::*;
    use crate::utility::SyncSendPointer;

    #[no_mangle]
    pub unsafe extern "C" fn childpidwatcher_new() -> *mut ChildPidWatcher {
        Box::into_raw(Box::new(ChildPidWatcher::new()))
    }

    #[no_mangle]
    pub unsafe extern "C" fn childpidwatcher_free(watcher: *mut ChildPidWatcher) {
        unsafe { Box::from_raw(notnull_mut(watcher)) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn childpidwatcher_forkWatchable(
        watcher: *const ChildPidWatcher,
        child_fn: extern "C" fn(*mut libc::c_void),
        child_fn_data: *mut libc::c_void,
    ) -> i32 {
        match unsafe {
            watcher
                .as_ref()
                .unwrap()
                .fork_watchable(|| child_fn(child_fn_data))
        } {
            Ok(pid) => pid.as_raw(),
            Err(e) => -(e as i32),
        }
    }

    /// Register interest in `pid`, and associate it with `read_fd`.
    ///
    /// `read_fd` should be the read end of a pipe, whose write end is owned
    /// *solely* by `pid`, causing `read_fd` to become invalid when `pid` exits.
    /// In a multi-threaded program care must be taken to prevent a concurrent
    /// fork from leaking the write end of the pipe into other children. One way
    /// to avoid this is to use O_CLOEXEC when creating the pipe, and then unset
    /// O_CLOEXEC in the child before calling exec.
    ///
    /// Be sure to close the parent's write-end of the pipe.
    ///
    /// Takes ownership of `read_fd`, and will close it when appropriate.
    #[no_mangle]
    pub unsafe extern "C" fn childpidwatcher_registerPid(
        watcher: *const ChildPidWatcher,
        pid: i32,
        read_fd: i32,
    ) {
        unsafe { watcher.as_ref() }
            .unwrap()
            .register_pid(Pid::from_raw(pid), unsafe { File::from_raw_fd(read_fd) });
    }

    #[no_mangle]
    pub unsafe extern "C" fn childpidwatcher_unregisterPid(
        watcher: *const ChildPidWatcher,
        pid: i32,
    ) {
        unsafe { watcher.as_ref() }
            .unwrap()
            .unregister_pid(Pid::from_raw(pid));
    }

    /// Call `callback` exactly once from another thread after the child `pid`
    /// has exited, including if it has already exited. Does *not* reap the
    /// child itself.
    ///
    /// The returned handle is guaranteed to be non-zero.
    ///
    /// Panics if `pid` doesn't exist.
    ///
    /// SAFETY: It must be safe for `callback` to execute and manipulate `data`
    /// from another thread. e.g. typically this means that `data` must be `Send`
    /// and `Sync`.
    #[no_mangle]
    pub unsafe extern "C" fn childpidwatcher_watch(
        watcher: *const ChildPidWatcher,
        pid: libc::pid_t,
        callback: extern "C" fn(libc::pid_t, *mut libc::c_void),
        data: *mut libc::c_void,
    ) -> WatchHandle {
        let data = unsafe { SyncSendPointer::new(data) };
        unsafe { watcher.as_ref() }
            .unwrap()
            .register_callback(Pid::from_raw(pid), move |pid| {
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
            .unregister_callback(Pid::from_raw(pid), handle);
    }
}
