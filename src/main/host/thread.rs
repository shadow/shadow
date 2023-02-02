use super::process::{ProcessId, Process};
use super::syscall_types::{PluginPtr, SysCallReg};
use crate::cshadow as c;
use crate::host::syscall_condition::{SysCallConditionRef, SysCallConditionRefMut};
use crate::utility::{syscall, HostTreePointer, IsSend};
use nix::unistd::Pid;
use shadow_shim_helper_rs::rootedcell::rc::RootedRc;
use shadow_shim_helper_rs::shim_shmem::ThreadShmem;
use shadow_shim_helper_rs::HostId;

/// Used for C interop.
pub type ThreadRc = RootedRc<Thread>;

/// A virtual Thread in Shadow. Currently a thin wrapper around the C Thread,
/// which this object owns, and frees on Drop.
pub struct Thread {
    cthread: RootedRefCell<HostTreePointer<c::Thread>>,
}

impl IsSend for Thread {}

impl Thread {
    /// Have the plugin thread natively execute the given syscall.
    pub fn native_syscall(&self, n: i64, args: &[SysCallReg]) -> nix::Result<SysCallReg> {
        // We considered using an iterator here rather than having to pass an index everywhere
        // below; we avoided it because argument evaluation order is currently a bit of a murky
        // issue, even though it'll *probably* always be left-to-right.
        // https://internals.rust-lang.org/t/rust-expression-order-of-evaluation/2605/16
        let arg = |i| args[i];
        // Safety: self.cthread initialized in CThread::new.
        let raw_res = unsafe {
            match args.len() {
                //
                0 => c::cthread_nativeSyscall(self.cthread(), n),
                1 => c::cthread_nativeSyscall(self.cthread(), n, arg(0)),
                2 => c::cthread_nativeSyscall(self.cthread(), n, arg(0), arg(1)),
                3 => c::cthread_nativeSyscall(self.cthread(), n, arg(0), arg(1), arg(2)),
                4 => c::cthread_nativeSyscall(self.cthread(), n, arg(0), arg(1), arg(2), arg(3)),
                5 => c::cthread_nativeSyscall(
                    self.cthread(),
                    n,
                    arg(0),
                    arg(1),
                    arg(2),
                    arg(3),
                    arg(4),
                ),
                6 => c::cthread_nativeSyscall(
                    self.cthread(),
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
        };
        syscall::raw_return_value_to_result(raw_res)
    }

    pub fn process_id(&self) -> ProcessId {
        // Safety: self.cthread initialized in CThread::new.
        ProcessId::try_from(unsafe { c::cthread_getProcessId(self.cthread()) }).unwrap()
    }

    pub fn host_id(&self) -> HostId {
        // Safety: self.cthread initialized in CThread::new.
        unsafe { c::cthread_getHostId(self.cthread()) }
    }

    pub fn system_pid(&self) -> Pid {
        // Safety: self.cthread initialized in CThread::new.
        Pid::from_raw(unsafe { c::cthread_getNativePid(self.cthread()) })
    }

    pub fn system_tid(&self) -> Pid {
        // Safety: self.cthread initialized in CThread::new.
        Pid::from_raw(unsafe { c::cthread_getNativeTid(self.cthread()) })
    }

    pub fn csyscallhandler(&self) -> *mut c::SysCallHandler {
        unsafe { c::cthread_getSysCallHandler(self.cthread()) }
    }

    pub fn id(&self) -> ThreadId {
        ThreadId(unsafe { c::cthread_getID(self.cthread()).try_into().unwrap() })
    }

    pub fn syscall_condition(&self) -> Option<SysCallConditionRef> {
        let syscall_condition_ptr = unsafe { c::cthread_getSysCallCondition(self.cthread()) };
        if syscall_condition_ptr.is_null() {
            return None;
        }

        Some(unsafe { SysCallConditionRef::borrow_from_c(syscall_condition_ptr) })
    }

    pub fn syscall_condition_mut(&self) -> Option<SysCallConditionRefMut> {
        let syscall_condition_ptr = unsafe { c::cthread_getSysCallCondition(self.cthread()) };
        if syscall_condition_ptr.is_null() {
            return None;
        }

        Some(unsafe { SysCallConditionRefMut::borrow_from_c(syscall_condition_ptr) })
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

    /// Takes ownership of `cthread`.
    ///
    /// # Safety
    /// * `cthread` must point to a valid Thread struct.
    /// * The returned object must not outlive `cthread`
    pub unsafe fn new(host: &Host, process: &Process, tid: ThreadId) -> RootedRc<Self> {
        

        assert!(!cthread.is_null());
        Self {
            cthread: HostTreePointer::new(cthread),
        }
    }

    /// # Safety
    ///
    /// Returned thread may only be accessed while the current Host is still
    /// active.
    pub unsafe fn cthread(&self) -> *mut c::Thread {
        // SAFETY: Enforced by caller.
        unsafe { self.cthread.ptr() }
    }

    /// Shared memory for this thread.
    pub fn shmem(&self) -> &ThreadShmem {
        unsafe { c::thread_sharedMem(self.cthread()).as_ref().unwrap() }
    }
}

impl Drop for Thread {
    fn drop(&mut self) {
        unsafe { c::cthread_free(self.cthread.ptr()) }
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

    use crate::host::host::Host;
    use crate::host::process::ProcessRefCell;

    use super::*;

    #[no_mangle]
    pub unsafe extern "C" fn threadrc_new(
        host: *const Host,
        proc: *const ProcessRefCell,
        threadId: libc::pid_t,
    ) -> *mut ThreadRc {
        todo!()
    }

    /// This drops the ref-counted wrapper, not the thread itself.
    #[no_mangle]
    pub unsafe extern "C" fn threadrc_drop(
        thread: *mut ThreadRc
    ) {
        todo!()
    }

    /// This clones the ref-counted wrapper, not the thread itself.
    #[no_mangle]
    pub unsafe extern "C" fn threadrc_clone(
        thread: *const ThreadRc
    ) -> *mut ThreadRc {
        todo!()
    }

    #[no_mangle]
    pub unsafe extern "C" fn thread_run(
        thread: *const ThreadRc,
        plugin_path: *const libc::c_char,
        argv: * const * const libc::c_char,
        envv: * const * const libc::c_char,
        working_dir: * const libc::c_char,
        strace_fd: i32)  {
            todo!()
    }

    #[no_mangle]
    pub unsafe extern "C" fn thread_resume(
        thread: *const ThreadRc)  {
            todo!()
    }

    #[no_mangle]
    pub unsafe extern "C" fn thread_handleProcessExit(
        thread: *const ThreadRc)  {
            todo!()
    }

    #[no_mangle]
    pub unsafe extern "C" fn thread_getReturnCode(
        thread: *const ThreadRc)  {
            todo!()
    }

    /// Make the requested syscall from within the plugin. For now, does *not* flush
    /// or invalidate pointers, but we may need to revisit this to support some
    /// use-cases.
    ///
    /// Arguments are treated opaquely. e.g. no pointer-marshalling is done.
    ///
    /// The return value is the value returned by the syscall *instruction*.
    /// You can map to a corresponding errno value with syscall_rawReturnValueToErrno.
    // XXX: rust doesn't support declaring a function with varargs (...), but this
    // declaration is ABI compatible with a caller who sees this function declared
    // with arguments `ThreadRc* thread, long n, ...`. We manually generate that declartion
    // in our bindings.
    #[no_mangle]
    unsafe extern "C" fn thread_nativeSyscall(
        thread: *const ThreadRc,
        n: libc::c_long,
        arg1: libc::c_long,
        arg2: libc::c_long,
        arg3: libc::c_long,
        arg4: libc::c_long,
        arg5: libc::c_long,
        arg6: libc::c_long
    ) -> libc::c_long {
            todo!()
    }

    #[no_mangle]
    pub unsafe extern "C" fn thread_isRunning(
        thread: *const ThreadRc)  {
            todo!()
    }
    
    #[no_mangle]
    pub unsafe extern "C" fn thread_getProcessId(
        thread: *const ThreadRc) -> libc::pid_t {
            todo!()
    }

    #[no_mangle]
    pub unsafe extern "C" fn thread_getHostId(
        thread: *const ThreadRc) -> HostId {
            todo!()
    }

    #[no_mangle]
    pub unsafe extern "C" fn thread_getNativePid(
        thread: *const ThreadRc) -> libc::pid_t {
            todo!()
    }

    #[no_mangle]
    pub unsafe extern "C" fn thread_getNativeTid(
        thread: *const ThreadRc) -> libc::pid_t {
            todo!()
    }

    #[no_mangle]
    pub unsafe extern "C" fn thread_getID(
        thread: *const ThreadRc) -> libc::pid_t {
            todo!()
    }

    /// Create a new child thread as for `clone(2)`. Returns 0 on success, or a
    /// negative errno on failure.  On success, `child` will be set to a newly
    /// allocated and initialized child Thread. Caller is responsible for adding the
    /// Thread to the process and arranging for it to run (typically by calling
    /// process_addThread).
    #[no_mangle]
    pub unsafe extern "C" fn thread_clone(
        thread: *const ThreadRc,
        flags: libc::c_ulong,
        child_stack: c::PluginVirtualPtr,
        ptid: c::PluginVirtualPtr,
        ctid: c::PluginVirtualPtr,
        newtls: libc::c_ulong,
        child: *mut *mut ThreadRc
    ) -> i32 {
            todo!()
    }

    /// Sets the `clear_child_tid` attribute as for `set_tid_address(2)`. The thread
    /// will perform a futex-wake operation on the given address on termination.
    #[no_mangle]
    pub unsafe extern "C" fn thread_setTidAddress(
        thread: *const ThreadRc,
        addr: c::PluginVirtualPtr
    ) {
            todo!()
    }

    /// Gets the `clear_child_tid` attribute, as set by `thread_setTidAddress`.
    #[no_mangle]
    pub unsafe extern "C" fn thread_getTidAddress(
        thread: *const ThreadRc,
    ) -> c::PluginVirtualPtr {
            todo!()
    }

    /// Returns whether the given thread is its thread group (aka process) leader.
    /// Typically this is true for the first thread created in a process.
    #[no_mangle]
    pub unsafe extern "C" fn thread_isLeader(
        thread: *const ThreadRc,
    ) -> bool {
            todo!()
    }

    /// Returns the block used for IPC, or NULL if no such block is is used.
    #[no_mangle]
    pub unsafe extern "C" fn thread_getIPCBlock(
        thread: *const ThreadRc,
    ) -> *mut c::ShMemBlock{
            todo!()
    }

    /// Returns the block used for shared state, or NULL if no such block is is used.
    #[no_mangle]
    pub unsafe extern "C" fn thread_getShMBlock(
        thread: *const ThreadRc,
    ) -> *mut c::ShMemBlock{
            todo!()
    }

    /// Returns a typed pointer to memory shared with the shim (which is backed by
    /// the block returned by thread_getShMBlock).
    #[no_mangle]
    pub unsafe extern "C" fn thread_sharedMem(
        thread: *const ThreadRc,
    ) -> *mut ShimShmemThread {
            todo!()
    }

    #[no_mangle]
    pub unsafe extern "C" fn thread_getProcess(
        thread: *const ThreadRc,
    ) -> *const ProcessRefCell{
            todo!()
    }

    #[no_mangle]
    pub unsafe extern "C" fn thread_getHost(
        thread: *const ThreadRc,
    ) -> *const Host {
            todo!()
    }

    /// Get the syscallhandler for this thread.
    #[no_mangle]
    pub unsafe extern "C" fn thread_getSysCallHandler(
        thread: *const ThreadRc,
    ) -> *mut c::SysCallHandler {
            todo!()
    }

    #[no_mangle]
    pub unsafe extern "C" fn thread_getSysCallCondition(
        thread: *const ThreadRc,
    ) -> *mut c::SysCallCondition{
            todo!()
    }
    
    #[no_mangle]
    pub unsafe extern "C" fn thread_clearSysCallCondition(
        thread: *const ThreadRc,
    ) {
            todo!()
    }

    #[no_mangle]
    pub unsafe extern "C" fn thread_getSignalSet (
        thread: *const ThreadRc,
    ) -> *mut libc::sigset_t {
            todo!()
    }

    /// Returns true iff there is an unblocked, unignored signal pending for this
    /// thread (or its process).
    #[no_mangle]
    pub unsafe extern "C" fn thread_unblockedSignalPending (
        thread: *const ThreadRc,
        host_lock: *const ShimShmemHostLock
    ) -> bool {
            todo!()
    }


}