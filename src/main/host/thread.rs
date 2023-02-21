use std::cell::Cell;
use std::ffi::{CStr, CString};
use std::os::fd::RawFd;

use super::context::ProcessContext;
use super::host::Host;
use super::process::{Process, ProcessId};
use super::syscall_types::{PluginPtr, SysCallReg};
use crate::cshadow as c;
use crate::host::syscall_condition::{SysCallConditionRef, SysCallConditionRefMut};
use crate::utility::{syscall, IsSend, SendPointer};
use nix::unistd::Pid;
use shadow_shim_helper_rs::rootedcell::rc::RootedRc;
use shadow_shim_helper_rs::rootedcell::refcell::RootedRefCell;
use shadow_shim_helper_rs::shim_shmem::{HostShmemProtected, ThreadShmem};
use shadow_shim_helper_rs::HostId;
use shadow_shmem::allocator::{Allocator, ShMemBlock};

/// A virtual Thread in Shadow. Currently a thin wrapper around the C Thread,
/// which this object owns, and frees on Drop.
pub struct Thread {
    id: ThreadId,
    host_id: HostId,
    process_id: ProcessId,
    // If non-NULL, this address should be cleared and futex-awoken on thread exit.
    // See set_tid_address(2).
    tid_address: Cell<PluginPtr>,
    shim_shared_memory: ShMemBlock<'static, ThreadShmem>,
    syscallhandler: SendPointer<c::SysCallHandler>,
    cond: Cell<SendPointer<c::SysCallCondition>>,
    /// The native, managed thread
    mthread: SendPointer<c::ManagedThread>,
}

impl IsSend for Thread {}

impl Thread {
    /// Have the plugin thread natively execute the given syscall.
    fn native_syscall_raw(&self, n: i64, args: &[SysCallReg]) -> libc::c_long {
        // We considered using an iterator here rather than having to pass an index everywhere
        // below; we avoided it because argument evaluation order is currently a bit of a murky
        // issue, even though it'll *probably* always be left-to-right.
        // https://internals.rust-lang.org/t/rust-expression-order-of-evaluation/2605/16
        let arg = |i| args[i];
        let mthread = self.mthread.ptr();

        unsafe {
            match args.len() {
                0 => c::managedthread_nativeSyscall(mthread, n),
                1 => c::managedthread_nativeSyscall(mthread, n, arg(0)),
                2 => c::managedthread_nativeSyscall(mthread, n, arg(0), arg(1)),
                3 => c::managedthread_nativeSyscall(mthread, n, arg(0), arg(1), arg(2)),
                4 => c::managedthread_nativeSyscall(mthread, n, arg(0), arg(1), arg(2), arg(3)),
                5 => c::managedthread_nativeSyscall(
                    mthread,
                    n,
                    arg(0),
                    arg(1),
                    arg(2),
                    arg(3),
                    arg(4),
                ),
                6 => c::managedthread_nativeSyscall(
                    mthread,
                    n,
                    arg(0),
                    arg(1),
                    arg(2),
                    arg(3),
                    arg(4),
                    arg(5),
                ),
                x => panic!("Bad number of syscall args {}", x),
            }
        }
    }

    /// Have the plugin thread natively execute the given syscall.
    fn native_syscall(&self, n: i64, args: &[SysCallReg]) -> nix::Result<SysCallReg> {
        syscall::raw_return_value_to_result(self.native_syscall_raw(n, args))
    }

    pub fn process_id(&self) -> ProcessId {
        self.process_id
    }

    pub fn host_id(&self) -> HostId {
        self.host_id
    }

    pub fn native_pid(&self) -> Pid {
        // Safety: Initialized in Self::new
        Pid::from_raw(unsafe { c::managedthread_getNativePid(self.mthread.ptr()) })
    }

    pub fn native_tid(&self) -> Pid {
        // Safety: Initialized in Self::new
        Pid::from_raw(unsafe { c::managedthread_getNativeTid(self.mthread.ptr()) })
    }

    pub fn csyscallhandler(&self) -> *mut c::SysCallHandler {
        self.syscallhandler.ptr()
    }

    pub fn id(&self) -> ThreadId {
        self.id
    }

    /// Returns whether the given thread is its thread group (aka process) leader.
    /// Typically this is true for the first thread created in a process.
    pub fn is_leader(&self) -> bool {
        self.id == self.process_id.into()
    }

    pub fn syscall_condition(&self) -> Option<SysCallConditionRef> {
        // We check the for null explicitly here instead of using `as_mut` to
        // construct and match an `Option<&mut c::SysCallCondition>`, since it's
        // difficult to ensure we're not breaking any Rust aliasing rules when
        // constructing a mutable reference.
        let c = self.cond.get().ptr();
        if c.is_null() {
            None
        } else {
            Some(unsafe { SysCallConditionRef::borrow_from_c(c) })
        }
    }

    pub fn syscall_condition_mut(&self) -> Option<SysCallConditionRefMut> {
        // We can't safely use `as_mut` here, since that would construct a mutable reference,
        // and we can't prove no other reference exists.
        let c = self.cond.get().ptr();
        if c.is_null() {
            None
        } else {
            Some(unsafe { SysCallConditionRefMut::borrow_from_c(c) })
        }
    }

    fn cleanup_syscall_condition(&self) {
        if let Some(c) = unsafe {
            self.cond
                .replace(SendPointer::new(std::ptr::null_mut()))
                .ptr()
                .as_mut()
        } {
            unsafe { c::syscallcondition_cancel(c) };
            unsafe { c::syscallcondition_unref(c) };
        }
    }

    /// Natively execute munmap(2) on the given thread.
    pub fn native_munmap(&self, ptr: PluginPtr, size: usize) -> nix::Result<()> {
        self.native_syscall(libc::SYS_munmap, &[ptr.into(), size.into()])?;
        Ok(())
    }

    /// Natively execute mmap(2) on the given thread.
    pub fn native_mmap(
        &self,
        addr: PluginPtr,
        len: usize,
        prot: i32,
        flags: i32,
        fd: i32,
        offset: i64,
    ) -> nix::Result<PluginPtr> {
        Ok(self
            .native_syscall(
                libc::SYS_mmap,
                &[
                    SysCallReg::from(addr),
                    SysCallReg::from(len),
                    SysCallReg::from(prot),
                    SysCallReg::from(flags),
                    SysCallReg::from(fd),
                    SysCallReg::from(offset),
                ],
            )?
            .into())
    }

    /// Natively execute mremap(2) on the given thread.
    pub fn native_mremap(
        &self,
        old_addr: PluginPtr,
        old_len: usize,
        new_len: usize,
        flags: i32,
        new_addr: PluginPtr,
    ) -> nix::Result<PluginPtr> {
        Ok(self
            .native_syscall(
                libc::SYS_mremap,
                &[
                    SysCallReg::from(old_addr),
                    SysCallReg::from(old_len),
                    SysCallReg::from(new_len),
                    SysCallReg::from(flags),
                    SysCallReg::from(new_addr),
                ],
            )?
            .into())
    }

    /// Natively execute mmap(2) on the given thread.
    pub fn native_mprotect(&self, addr: PluginPtr, len: usize, prot: i32) -> nix::Result<()> {
        self.native_syscall(
            libc::SYS_mprotect,
            &[
                SysCallReg::from(addr),
                SysCallReg::from(len),
                SysCallReg::from(prot),
            ],
        )?;
        Ok(())
    }

    /// Natively execute open(2) on the given thread.
    pub fn native_open(&self, pathname: PluginPtr, flags: i32, mode: i32) -> nix::Result<i32> {
        let res = self.native_syscall(
            libc::SYS_open,
            &[
                SysCallReg::from(pathname),
                SysCallReg::from(flags),
                SysCallReg::from(mode),
            ],
        );
        Ok(i32::from(res?))
    }

    /// Natively execute close(2) on the given thread.
    pub fn native_close(&self, fd: i32) -> nix::Result<()> {
        self.native_syscall(libc::SYS_close, &[SysCallReg::from(fd)])?;
        Ok(())
    }

    /// Natively execute brk(2) on the given thread.
    pub fn native_brk(&self, addr: PluginPtr) -> nix::Result<PluginPtr> {
        let res = self.native_syscall(libc::SYS_brk, &[SysCallReg::from(addr)])?;
        Ok(PluginPtr::from(res))
    }

    /// Allocates some space in the plugin's memory. Use `get_writeable_ptr` to write to it, and
    /// `flush` to ensure that the write is flushed to the plugin's memory.
    pub fn malloc_plugin_ptr(&self, size: usize) -> nix::Result<PluginPtr> {
        // SAFETY: No pointer specified; can't pass a bad one.
        self.native_mmap(
            PluginPtr::from(0usize),
            size,
            libc::PROT_READ | libc::PROT_WRITE,
            libc::MAP_PRIVATE | libc::MAP_ANONYMOUS,
            -1,
            0,
        )
    }

    /// Frees a pointer previously returned by `malloc_plugin_ptr`
    pub fn free_plugin_ptr(&self, ptr: PluginPtr, size: usize) -> nix::Result<()> {
        self.native_munmap(ptr, size)?;
        Ok(())
    }

    pub fn new(
        host: &Host,
        process: &Process,
        thread_id: ThreadId,
    ) -> RootedRc<RootedRefCell<Self>> {
        let thread = Self {
            mthread: unsafe {
                SendPointer::new(c::managedthread_new(
                    host.id(),
                    process.id().into(),
                    thread_id.into(),
                ))
            },
            syscallhandler: unsafe {
                SendPointer::new(c::syscallhandler_new(
                    host.id(),
                    process.id().into(),
                    thread_id.into(),
                ))
            },
            cond: Cell::new(unsafe { SendPointer::new(std::ptr::null_mut()) }),
            id: thread_id,
            host_id: host.id(),
            process_id: process.id(),
            tid_address: Cell::new(PluginPtr::from(0usize)),
            shim_shared_memory: Allocator::global().alloc(ThreadShmem::new(
                &host.shim_shmem_lock_borrow().unwrap(),
                thread_id.into(),
            )),
        };
        let root = host.root();
        RootedRc::new(root, RootedRefCell::new(root, thread))
    }

    /// Shared memory for this thread.
    pub fn shmem(&self) -> &ThreadShmem {
        &self.shim_shared_memory
    }

    pub fn run(
        &self,
        _host: &Host,
        plugin_path: &CStr,
        argv: &[CString],
        mut envv: Vec<CString>,
        working_dir: &CStr,
        strace_fd: Option<RawFd>,
        log_path: &CStr,
    ) {
        envv.push(
            CString::new(format!(
                "SHADOW_SHM_THREAD_BLK={}",
                self.shim_shared_memory.serialize().encode_to_string()
            ))
            .unwrap(),
        );

        let argv_ptrs: Vec<*const i8> = argv
            .iter()
            .map(|x| x.as_ptr())
            // the last element of argv must be NULL
            .chain(std::iter::once(std::ptr::null()))
            .collect();

        let envv_ptrs: Vec<*const i8> = envv
            .iter()
            .map(|x| x.as_ptr())
            // the last element of envv must be NULL
            .chain(std::iter::once(std::ptr::null()))
            .collect();

        unsafe {
            c::managedthread_run(
                self.mthread.ptr(),
                plugin_path.as_ptr(),
                argv_ptrs.as_ptr(),
                envv_ptrs.as_ptr(),
                working_dir.as_ptr(),
                strace_fd.unwrap_or(-1),
                log_path.as_ptr(),
            )
        };
    }

    pub fn resume(&self, process_ctx: &ProcessContext) {
        // Ensure the condition isn't triggered again, but don't clear it yet.
        // Syscall handler can still access.
        if let Some(c) = unsafe { self.cond.get().ptr().as_mut() } {
            unsafe { c::syscallcondition_cancel(c) };
        }

        let cond = unsafe { c::managedthread_resume(self.mthread.ptr()) };

        // Now we're done with old condition.
        if let Some(c) = unsafe {
            self.cond
                .replace(SendPointer::new(std::ptr::null_mut()))
                .ptr()
                .as_mut()
        } {
            unsafe { c::syscallcondition_unref(c) };
        }

        // Wait on new condition.
        self.cond.set(unsafe { SendPointer::new(cond) });
        if let Some(cond) = unsafe { cond.as_mut() } {
            unsafe {
                c::syscallcondition_waitNonblock(
                    cond,
                    process_ctx.host,
                    process_ctx.process.cprocess(process_ctx.host),
                    self,
                )
            }
        }
    }

    pub fn handle_process_exit(&self) {
        self.cleanup_syscall_condition();
        unsafe { c::managedthread_handleProcessExit(self.mthread.ptr()) };
    }

    pub fn return_code(&self) -> i32 {
        unsafe { c::managedthread_getReturnCode(self.mthread.ptr()) }
    }

    pub fn is_running(&self) -> bool {
        unsafe { c::managedthread_isRunning(self.mthread.ptr()) }
    }

    pub fn get_tid_address(&self) -> PluginPtr {
        self.tid_address.get()
    }

    pub fn set_tid_address(&self, ptr: PluginPtr) {
        self.tid_address.set(ptr)
    }

    pub fn unblocked_signal_pending(
        &self,
        process: &Process,
        host_shmem: &HostShmemProtected,
    ) -> bool {
        debug_assert_eq!(process.id(), self.process_id);

        let thread_shmem_protected = self.shmem().protected.borrow(&host_shmem.root);

        let unblocked_signals = !thread_shmem_protected.blocked_signals;
        let pending_signals = self
            .shmem()
            .protected
            .borrow(&host_shmem.root)
            .pending_signals
            | process
                .shmem()
                .protected
                .borrow(&host_shmem.root)
                .pending_signals;

        !(pending_signals & unblocked_signals).is_empty()
    }
}

impl Drop for Thread {
    fn drop(&mut self) {
        if let Some(c) = unsafe { self.cond.get_mut().ptr().as_mut() } {
            unsafe { c::syscallcondition_cancel(c) };
            unsafe { c::syscallcondition_unref(c) };
        }
        unsafe { c::managedthread_free(self.mthread.ptr()) };
        unsafe { c::syscallhandler_unref(self.syscallhandler.ptr()) };
    }
}

#[derive(Debug, PartialEq, Eq, Hash, Copy, Clone, Ord, PartialOrd)]
pub struct ThreadId(u32);

impl TryFrom<libc::pid_t> for ThreadId {
    type Error = <u32 as TryFrom<libc::pid_t>>::Error;

    fn try_from(value: libc::pid_t) -> Result<Self, Self::Error> {
        Ok(Self(u32::try_from(value)?))
    }
}

impl From<ProcessId> for ThreadId {
    fn from(value: ProcessId) -> Self {
        // A process ID is also a valid thread ID
        ThreadId(value.into())
    }
}

impl From<ThreadId> for libc::pid_t {
    fn from(val: ThreadId) -> Self {
        val.0.try_into().unwrap()
    }
}

impl std::fmt::Display for ThreadId {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}

mod export {
    use shadow_shim_helper_rs::shim_shmem::export::{ShimShmemHostLock, ShimShmemThread};
    use shadow_shmem::allocator::ShMemBlockSerialized;

    use crate::core::worker::Worker;
    use crate::host::host::Host;
    use crate::host::process::ProcessRefCell;

    use super::*;

    /// Make the requested syscall from within the plugin.
    ///
    /// Does *not* flush or invalidate MemoryManager pointers, such as those
    /// obtained through `process_getReadablePtr` etc.
    ///
    /// Arguments are treated opaquely. e.g. no pointer-marshalling is done.
    ///
    /// The return value is the value returned by the syscall *instruction*.
    /// You can map to a corresponding errno value with syscall_rawReturnValueToErrno.
    //
    // Rust doesn't support declaring a function with varargs (...), but this
    // declaration is ABI compatible with a caller who sees this function declared
    // with arguments `Thread* thread, long n, ...`. We manually generate that declartion
    // in our bindings.
    #[no_mangle]
    unsafe extern "C" fn thread_nativeSyscall(
        thread: *const Thread,
        n: libc::c_long,
        arg1: c::SysCallReg,
        arg2: c::SysCallReg,
        arg3: c::SysCallReg,
        arg4: c::SysCallReg,
        arg5: c::SysCallReg,
        arg6: c::SysCallReg,
    ) -> libc::c_long {
        let thread = unsafe { thread.as_ref().unwrap() };
        thread.native_syscall_raw(n, &[arg1, arg2, arg3, arg4, arg5, arg6])
    }

    #[no_mangle]
    pub unsafe extern "C" fn thread_getID(thread: *const Thread) -> libc::pid_t {
        let thread = unsafe { thread.as_ref().unwrap() };
        thread.id().into()
    }

    /// Create a new child thread as for `clone(2)`. Returns 0 on success, or a
    /// negative errno on failure.  On success, `child` will be set to a newly
    /// allocated and initialized child Thread, which will have already be added
    /// to the Process.
    #[no_mangle]
    pub unsafe extern "C" fn thread_clone(
        thread: *const Thread,
        flags: libc::c_ulong,
        child_stack: c::PluginVirtualPtr,
        ptid: c::PluginVirtualPtr,
        ctid: c::PluginVirtualPtr,
        newtls: libc::c_ulong,
        child: *mut *const Thread,
    ) -> i32 {
        let thread = unsafe { thread.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            Worker::with_active_process(|process| {
                let childrc = Thread::new(host, process, host.get_new_process_id().into());
                let rv = {
                    let child = childrc.borrow(host.root());
                    unsafe {
                        c::managedthread_clone(
                            child.mthread.ptr(),
                            thread.mthread.ptr(),
                            flags,
                            child_stack,
                            ptid,
                            ctid,
                            newtls,
                        )
                    }
                };
                if rv != 0 {
                    childrc.safely_drop(host.root());
                    return rv;
                }
                {
                    let child_ref = childrc.borrow(host.root());
                    let child_ref: &Thread = &child_ref;
                    unsafe { child.replace(child_ref as *const _) };
                }
                process.add_thread(host, childrc);
                rv
            })
            .unwrap()
        })
        .unwrap()
    }

    /// Sets the `clear_child_tid` attribute as for `set_tid_address(2)`. The thread
    /// will perform a futex-wake operation on the given address on termination.
    #[no_mangle]
    pub unsafe extern "C" fn thread_setTidAddress(
        thread: *const Thread,
        addr: c::PluginVirtualPtr,
    ) {
        let thread = unsafe { thread.as_ref().unwrap() };
        thread.set_tid_address(addr.into());
    }

    /// Gets the `clear_child_tid` attribute, as set by `thread_setTidAddress`.
    #[no_mangle]
    pub unsafe extern "C" fn thread_getTidAddress(thread: *const Thread) -> c::PluginVirtualPtr {
        let thread = unsafe { thread.as_ref().unwrap() };
        thread.get_tid_address().into()
    }

    /// Writes the serialized shared memory block handle to `out`
    #[no_mangle]
    pub unsafe extern "C" fn thread_getShMBlockSerialized(
        thread: *const Thread,
        out: *mut ShMemBlockSerialized,
    ) {
        let thread = unsafe { thread.as_ref().unwrap() };
        let out = unsafe { out.as_mut().unwrap() };
        *out = thread.shim_shared_memory.serialize();
    }

    /// Returns a typed pointer to memory shared with the shim (which is backed by
    /// the block returned by thread_getShMBlock).
    #[no_mangle]
    pub unsafe extern "C" fn thread_sharedMem(thread: *const Thread) -> *const ShimShmemThread {
        let thread = unsafe { thread.as_ref().unwrap() };
        &*thread.shim_shared_memory
    }

    #[no_mangle]
    pub unsafe extern "C" fn thread_getProcess(thread: *const Thread) -> *const ProcessRefCell {
        let thread = unsafe { thread.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let process = host.process_borrow(thread.process_id).unwrap();
            let process: &ProcessRefCell = &process;
            process as *const _
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn thread_getHost(thread: *const Thread) -> *const Host {
        let thread = unsafe { thread.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            assert_eq!(host.id(), thread.host_id());
            host as *const _
        })
        .unwrap()
    }

    /// Get the syscallhandler for this thread.
    #[no_mangle]
    pub unsafe extern "C" fn thread_getSysCallHandler(
        thread: *const Thread,
    ) -> *mut c::SysCallHandler {
        let thread = unsafe { thread.as_ref().unwrap() };
        thread.syscallhandler.ptr()
    }

    #[no_mangle]
    pub unsafe extern "C" fn thread_getSysCallCondition(
        thread: *const Thread,
    ) -> *mut c::SysCallCondition {
        let thread = unsafe { thread.as_ref().unwrap() };
        thread.cond.get().ptr()
    }

    #[no_mangle]
    pub unsafe extern "C" fn thread_clearSysCallCondition(thread: *const Thread) {
        let thread = unsafe { thread.as_ref().unwrap() };
        thread.cleanup_syscall_condition();
    }

    /// Returns true iff there is an unblocked, unignored signal pending for this
    /// thread (or its process).
    #[no_mangle]
    pub unsafe extern "C" fn thread_unblockedSignalPending(
        thread: *const Thread,
        host_lock: *const ShimShmemHostLock,
    ) -> bool {
        let thread = unsafe { thread.as_ref().unwrap() };
        let host_lock = unsafe { host_lock.as_ref().unwrap() };

        Worker::with_active_host(|host| {
            let process = host.process_borrow(thread.process_id()).unwrap();
            let process = process.borrow(host.root());
            thread.unblocked_signal_pending(&process, host_lock)
        })
        .unwrap()
    }
}
