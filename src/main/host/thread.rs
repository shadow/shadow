use super::host::HostId;
use super::process::ProcessId;
use super::syscall_types::{PluginPtr, SysCallReg};
use crate::cshadow as c;
use crate::utility::syscall;
use nix::unistd::Pid;
use std::convert::TryInto;

#[derive(Debug, PartialEq, Eq, Hash, Copy, Clone)]
pub struct ThreadId(u32);

pub trait Thread {
    /// Have the plugin thread natively execute the given syscall.
    fn native_syscall(&mut self, n: i64, args: &[SysCallReg]) -> nix::Result<SysCallReg>;

    fn process_id(&self) -> ProcessId;
    fn host_id(&self) -> HostId;
    fn system_pid(&self) -> Pid;
    fn system_tid(&self) -> Pid;
    fn csyscallhandler(&mut self) -> *mut c::SysCallHandler;
    fn id(&self) -> ThreadId;

    /// Natively execute munmap(2) on the given thread.
    fn native_munmap(&mut self, ptr: PluginPtr, size: usize) -> nix::Result<()> {
        self.native_syscall(libc::SYS_munmap, &[ptr.into(), size.into()])?;
        Ok(())
    }

    /// Natively execute mmap(2) on the given thread.
    fn native_mmap(
        &mut self,
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
    fn native_mremap(
        &mut self,
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
    fn native_mprotect(&mut self, addr: PluginPtr, len: usize, prot: i32) -> nix::Result<()> {
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
    fn native_open(&mut self, pathname: PluginPtr, flags: i32, mode: i32) -> nix::Result<i32> {
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
    fn native_close(&mut self, fd: i32) -> nix::Result<()> {
        self.native_syscall(libc::SYS_close, &[SysCallReg::from(fd)])?;
        Ok(())
    }

    /// Natively execute brk(2) on the given thread.
    fn native_brk(&mut self, addr: PluginPtr) -> nix::Result<PluginPtr> {
        let res = self.native_syscall(libc::SYS_brk, &[SysCallReg::from(addr)])?;
        Ok(PluginPtr::from(res))
    }

    /// Allocates some space in the plugin's memory. Use `get_writeable_ptr` to write to it, and
    /// `flush` to ensure that the write is flushed to the plugin's memory.
    fn malloc_plugin_ptr(&mut self, size: usize) -> nix::Result<PluginPtr> {
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
    fn free_plugin_ptr(&mut self, ptr: PluginPtr, size: usize) -> nix::Result<()> {
        self.native_munmap(ptr, size)?;
        Ok(())
    }
}

/// Wraps the C Thread struct.
pub struct CThread {
    cthread: *mut c::Thread,
}

impl CThread {
    /// # Safety
    /// * `cthread` must point to a valid Thread struct.
    pub unsafe fn new(cthread: *mut c::Thread) -> CThread {
        assert!(!cthread.is_null());
        unsafe { c::thread_ref(cthread) };
        CThread { cthread }
    }

    pub fn cthread(&self) -> *mut c::Thread {
        self.cthread
    }
}

impl Thread for CThread {
    fn native_syscall(&mut self, n: i64, args: &[SysCallReg]) -> nix::Result<SysCallReg> {
        // We considered using an iterator here rather than having to pass an index everywhere
        // below; we avoided it because argument evaluation order is currently a bit of a murky
        // issue, even though it'll *probably* always be left-to-right.
        // https://internals.rust-lang.org/t/rust-expression-order-of-evaluation/2605/16
        let arg = |i| c::SysCallReg::from(args[i]);
        // Safety: self.cthread initialized in CThread::new.
        let raw_res = unsafe {
            match args.len() {
                //
                0 => c::thread_nativeSyscall(self.cthread, n),
                1 => c::thread_nativeSyscall(self.cthread, n, arg(0)),
                2 => c::thread_nativeSyscall(self.cthread, n, arg(0), arg(1)),
                3 => c::thread_nativeSyscall(self.cthread, n, arg(0), arg(1), arg(2)),
                4 => c::thread_nativeSyscall(self.cthread, n, arg(0), arg(1), arg(2), arg(3)),
                5 => {
                    c::thread_nativeSyscall(self.cthread, n, arg(0), arg(1), arg(2), arg(3), arg(4))
                }
                6 => c::thread_nativeSyscall(
                    self.cthread,
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

    fn id(&self) -> ThreadId {
        ThreadId(unsafe { c::thread_getID(self.cthread).try_into().unwrap() })
    }

    fn process_id(&self) -> ProcessId {
        // Safety: self.cthread initialized in CThread::new.
        ProcessId::from(unsafe { c::thread_getProcessId(self.cthread) })
    }

    fn host_id(&self) -> HostId {
        // Safety: self.cthread initialized in CThread::new.
        HostId::from(unsafe { c::thread_getHostId(self.cthread) })
    }

    fn system_pid(&self) -> Pid {
        // Safety: self.cthread initialized in CThread::new.
        Pid::from_raw(unsafe { c::thread_getNativePid(self.cthread) })
    }

    fn system_tid(&self) -> Pid {
        // Safety: self.cthread initialized in CThread::new.
        Pid::from_raw(unsafe { c::thread_getNativeTid(self.cthread) })
    }

    fn csyscallhandler(&mut self) -> *mut c::SysCallHandler {
        unsafe { c::thread_getSysCallHandler(self.cthread) }
    }
}

impl Drop for CThread {
    fn drop(&mut self) {
        // Safety: self.cthread initialized in CThread::new.
        unsafe {
            c::thread_unref(self.cthread);
        }
    }
}
