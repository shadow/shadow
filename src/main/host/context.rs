//! This module provides several *Context* structs, intended to bundle together
//! current relevant objects in the hierarchy. These are meant to replace
//! `worker_getActiveThread`, etc. Passing around the current context explicitly
//! instead of putting them in globals both allows us to avoid interior mutability
//! (and its associated runtime cost and fallibility), and lets us keep a
//! hierarchical object structure (e.g. allow holding a mutable Process and a
//! mutable Thread belonging to that process simultaneously).
//!
//! Most code (e.g. syscall handlers) can take a `ThreadContext` argument and use
//! that to manipulate anything on the Host. The *current* `Thread` and `Process`
//! should typically be accessed directly. e.g. since a mutable reference to the
//! current `Thread` exists at `ThreadContext::thread`, it *cannot* also be
//! accessible via `ThreadContext::process` or `ThreadContext::host`.
//!
//! The manner in which they're unavailable isn't implemented yet, but the current
//! plan is that they'll be temporarily removed from their collections. e.g. something
//! conceptually like:
//!
//! ```ignore
//! impl Process {
//!     pub fn continue_thread(&mut self, host_ctx: &mut HostContext, tid: ThreadId) {
//!         let thread = self.threads.get_mut(tid).take();
//!         thread.continue(&mut host_ctx.add_process(self));
//!         self.threads.get_mut(tid).replace(thread);
//!     }
//! }
//! ```
//!
//! The Context objects are designed to allow simultaneously borrowing from multiple
//! of their objects.  This is currently implemented by exposing their fields
//! directly - Rust then allows each field to be borrowed independently. This could
//! alternatively be implemented by providing methods that borrow some or all of
//! their internal references simultaneously.

use std::ops::Deref;

use super::managed_thread::ManagedThread;
use super::process::ProcessId;
use super::thread::ThreadId;
use super::{host::Host, process::Process, thread::Thread};

/// Represent the "current" Host.
pub struct HostContext<'a> {
    // We expose fields directly rather than through accessors, so that
    // users can borrow from each field independently.
    pub host: &'a Host,
}

impl<'a> HostContext<'a> {
    pub fn new(host: &'a Host) -> Self {
        Self { host }
    }

    /// Add the given process to the context.
    pub fn with_process(&'a self, process: &'a Process) -> ProcessContext<'a> {
        ProcessContext::new(self.host, process)
    }
}

/// Represent the "current" `Host` and `Process`.
pub struct ProcessContext<'a> {
    pub host: &'a Host,
    pub process: &'a Process,
}

impl<'a> ProcessContext<'a> {
    pub fn new(host: &'a Host, process: &'a Process) -> Self {
        Self { host, process }
    }

    pub fn with_thread(&'a self, thread: &'a Thread) -> ThreadContext<'a> {
        ThreadContext::new(self.host, self.process, thread)
    }
}

/// Represent the "current" `Host`, `Process`, and `Thread`.
pub struct ThreadContext<'a> {
    pub host: &'a Host,
    pub process: &'a Process,
    pub thread: &'a Thread,
}

impl<'a> ThreadContext<'a> {
    pub fn new(host: &'a Host, process: &'a Process, thread: &'a Thread) -> Self {
        Self {
            host,
            process,
            thread,
        }
    }

    /// Split into a `&Process` and a `HostContext`. Useful e.g.
    /// for calling `Process` methods that take a `&HostContext`.
    pub fn split_process(&self) -> (HostContext, &Process) {
        (HostContext::new(self.host), self.process)
    }

    /// Split into a `&Thread` and a `ProcessContext`. Useful e.g.
    /// for calling `Thread` methods that take a `&ProcessContext`.
    pub fn split_thread(&self) -> (ProcessContext, &Thread) {
        (ProcessContext::new(self.host, self.process), self.thread)
    }

    pub fn mthread(&self) -> impl Deref<Target = ManagedThread> + '_ {
        self.thread.mthread()
    }
}

/// Shadow's C code doesn't know about contexts. In places where C code calls
/// Rust code, we can build them from C pointers.
pub struct ThreadContextObjs<'a> {
    host: &'a Host,
    pid: ProcessId,
    tid: ThreadId,
}

impl<'a> ThreadContextObjs<'a> {
    pub fn from_thread(host: &'a Host, thread: &'a Thread) -> Self {
        let pid = thread.process_id();
        let tid = thread.id();
        Self { host, pid, tid }
    }

    pub fn with_ctx<F, R>(&mut self, f: F) -> R
    where
        F: FnOnce(&mut ThreadContext) -> R,
    {
        // Avoid holding a borrow of process and threads lists here, since
        // handlers such as for `clone` may need to mutate them.

        let processrc = self
            .host
            .process_borrow(self.pid)
            .unwrap()
            .clone(self.host.root());
        let res = {
            let process = processrc.borrow(self.host.root());
            let threadrc = process
                .thread_borrow(self.tid)
                .unwrap()
                .clone(self.host.root());
            let res = {
                let thread = threadrc.borrow(self.host.root());
                let mut ctx = ThreadContext::new(self.host, &process, &thread);
                f(&mut ctx)
            };
            threadrc.explicit_drop_recursive(self.host.root(), self.host);
            res
        };
        processrc.explicit_drop_recursive(self.host.root(), self.host);
        res
    }
}
