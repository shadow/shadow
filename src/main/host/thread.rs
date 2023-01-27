use super::process::ProcessId;
use super::syscall_types::{PluginPtr, SysCallReg};
use crate::cshadow as c;
use crate::host::syscall_condition::{SysCallConditionRef, SysCallConditionRefMut};
use crate::utility::{syscall, HostTreePointer, IsSend};
use nix::unistd::Pid;
use shadow_shim_helper_rs::shim_shmem::ThreadShmem;
use shadow_shim_helper_rs::HostId;

/// Accessor for a C Thread struct. Stores a pointer to the C Thread, but
/// does not manipulate its ref count.
pub struct ThreadRef {
    cthread: HostTreePointer<c::Thread>,
}

impl IsSend for ThreadRef {}

impl ThreadRef {
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
                0 => c::thread_nativeSyscall(self.cthread(), n),
                1 => c::thread_nativeSyscall(self.cthread(), n, arg(0)),
                2 => c::thread_nativeSyscall(self.cthread(), n, arg(0), arg(1)),
                3 => c::thread_nativeSyscall(self.cthread(), n, arg(0), arg(1), arg(2)),
                4 => c::thread_nativeSyscall(self.cthread(), n, arg(0), arg(1), arg(2), arg(3)),
                5 => c::thread_nativeSyscall(
                    self.cthread(),
                    n,
                    arg(0),
                    arg(1),
                    arg(2),
                    arg(3),
                    arg(4),
                ),
                6 => c::thread_nativeSyscall(
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
        ProcessId::try_from(unsafe { c::thread_getProcessId(self.cthread()) }).unwrap()
    }

    pub fn host_id(&self) -> HostId {
        // Safety: self.cthread initialized in CThread::new.
        unsafe { c::thread_getHostId(self.cthread()) }
    }

    pub fn system_pid(&self) -> Pid {
        // Safety: self.cthread initialized in CThread::new.
        Pid::from_raw(unsafe { c::thread_getNativePid(self.cthread()) })
    }

    pub fn system_tid(&self) -> Pid {
        // Safety: self.cthread initialized in CThread::new.
        Pid::from_raw(unsafe { c::thread_getNativeTid(self.cthread()) })
    }

    pub fn csyscallhandler(&self) -> *mut c::SysCallHandler {
        unsafe { c::thread_getSysCallHandler(self.cthread()) }
    }

    pub fn id(&self) -> ThreadId {
        ThreadId(unsafe { c::thread_getID(self.cthread()).try_into().unwrap() })
    }

    pub fn syscall_condition(&self) -> Option<SysCallConditionRef> {
        let syscall_condition_ptr = unsafe { c::thread_getSysCallCondition(self.cthread()) };
        if syscall_condition_ptr.is_null() {
            return None;
        }

        Some(unsafe { SysCallConditionRef::borrow_from_c(syscall_condition_ptr) })
    }

    pub fn syscall_condition_mut(&self) -> Option<SysCallConditionRefMut> {
        let syscall_condition_ptr = unsafe { c::thread_getSysCallCondition(self.cthread()) };
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

    /// # Safety
    /// * `cthread` must point to a valid Thread struct.
    /// * The returned object must not outlive `cthread`
    pub unsafe fn new(cthread: *mut c::Thread) -> Self {
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
