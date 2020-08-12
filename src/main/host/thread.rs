use super::syscall_types::{PluginPtr, SysCallReg};
use crate::cbindings as c;
use crate::utility::syscall;
use libc;

pub trait Thread {
    /// Have the plugin thread natively execute the given syscall.
    /// SAFETY
    /// * `args` must not contain invalid PluginPtrs.
    unsafe fn native_syscall(&self, n: i64, args: &[SysCallReg]) -> Result<SysCallReg, i32>;

    /// Get a readable pointer to the plugin's memory.
    /// SAFETY
    /// * The specified memory region must be valid and readable.
    /// * Returned pointer mustn't be accessed after Thread runs again or flush is called.
    unsafe fn get_readable_ptr(
        &self,
        plugin_src: PluginPtr,
        n: usize,
    ) -> Result<*const ::std::os::raw::c_void, i32>;

    /// Get a writeable pointer to the plugin's memory.
    /// SAFETY
    /// * The specified memory region must be valid and writeable.
    /// * Returned pointer mustn't be accessed after Thread runs again or flush is called.
    unsafe fn get_writeable_ptr(
        &self,
        plugin_src: PluginPtr,
        n: usize,
    ) -> Result<*mut ::std::os::raw::c_void, i32>;

    fn get_process_id(&self) -> u32;
    fn get_host_id(&self) -> u32;
    fn get_system_pid(&self) -> libc::pid_t;

    /// Flush (and invalidate) pointers previously returned by `get_readable_ptr` and
    /// `get_writeable_ptr`.
    fn flush(&mut self);

    /// Natively execute munmap(2) on the given thread.
    unsafe fn native_munmap(&self, ptr: PluginPtr, size: usize) -> Result<PluginPtr, i32> {
        Ok(self
            .native_syscall(libc::SYS_munmap, &[ptr.into(), size.into()])?
            .into())
    }

    /// Natively execute mmap(2) on the given thread.
    unsafe fn native_mmap(
        &self,
        addr: PluginPtr,
        len: usize,
        prot: i32,
        flags: i32,
        fd: i32,
        offset: i64,
    ) -> Result<PluginPtr, i32> {
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
    unsafe fn native_mremap(
        &self,
        old_addr: PluginPtr,
        old_len: usize,
        new_len: usize,
        flags: i32,
        new_addr: PluginPtr,
    ) -> Result<PluginPtr, i32> {
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

    /// Natively execute open(2) on the given thread.
    fn native_open(&self, pathname: PluginPtr, flags: i32, mode: i32) -> Result<i32, i32> {
        let res = unsafe {
            self.native_syscall(
                libc::SYS_open,
                &[
                    SysCallReg::from(pathname),
                    SysCallReg::from(flags),
                    SysCallReg::from(mode),
                ],
            )
        };
        Ok(i32::from(res?))
    }

    /// Natively execute brk(2) on the given thread.
    fn native_brk(&self, addr: PluginPtr) -> Result<PluginPtr, i32> {
        let res = unsafe { self.native_syscall(libc::SYS_brk, &[SysCallReg::from(addr)]) };
        Ok(PluginPtr::from(res?))
    }

    /// Allocates some space in the plugin's memory. Use `get_writeable_ptr` to write to it, and
    /// `flush` to ensure that the write is flushed to the plugin's memory.
    fn malloc_plugin_ptr(&self, size: usize) -> Result<PluginPtr, i32> {
        // SAFETY: No pointer specified; can't pass a bad one.
        unsafe {
            self.native_mmap(
                PluginPtr::from(0usize),
                size,
                libc::PROT_READ | libc::PROT_WRITE,
                libc::MAP_PRIVATE | libc::MAP_ANONYMOUS,
                -1,
                0,
            )
        }
    }

    /// SAFETY: ptr must have been returned from `malloc_plugin_ptr`
    unsafe fn free_plugin_ptr(&self, ptr: PluginPtr, size: usize) -> Result<(), i32> {
        self.native_munmap(ptr, size)?;
        Ok(())
    }
}

/// Wraps the C Thread struct.
pub struct CThread {
    cthread: *mut c::Thread,
}

impl CThread {
    pub fn new(cthread: *mut c::Thread) -> CThread {
        unsafe {
            c::thread_ref(cthread);
        }
        CThread { cthread }
    }
}

impl Thread for CThread {
    unsafe fn native_syscall(&self, n: i64, args: &[SysCallReg]) -> Result<SysCallReg, i32> {
        // We considered using an iterator here rather than having to pass an index everywhere
        // below; we avoided it because argument evaluation order is currently a bit of a murky
        // issue, even though it'll *probably* always be left-to-right.
        // https://internals.rust-lang.org/t/rust-expression-order-of-evaluation/2605/16
        let arg = |i| c::SysCallReg::from(args[i]);
        let raw_res = match args.len() {
            //
            0 => c::thread_nativeSyscall(self.cthread, n),
            1 => c::thread_nativeSyscall(self.cthread, n, arg(0)),
            2 => c::thread_nativeSyscall(self.cthread, n, arg(0), arg(1)),
            3 => c::thread_nativeSyscall(self.cthread, n, arg(0), arg(1), arg(2)),
            4 => c::thread_nativeSyscall(self.cthread, n, arg(0), arg(1), arg(2), arg(3)),
            5 => c::thread_nativeSyscall(self.cthread, n, arg(0), arg(1), arg(2), arg(3), arg(4)),
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
        };
        syscall::raw_return_value_to_errno(raw_res)
    }

    unsafe fn get_readable_ptr(
        &self,
        plugin_src: PluginPtr,
        n: usize,
    ) -> Result<*const ::std::os::raw::c_void, i32> {
        Ok(c::thread_getReadablePtr(
            self.cthread,
            plugin_src.into(),
            n as u64,
        ))
    }

    unsafe fn get_writeable_ptr(
        &self,
        plugin_src: PluginPtr,
        n: usize,
    ) -> Result<*mut ::std::os::raw::c_void, i32> {
        Ok(c::thread_getWriteablePtr(
            self.cthread,
            plugin_src.into(),
            n as u64,
        ))
    }

    fn flush(&mut self) {
        unsafe {
            c::thread_flushPtrs(self.cthread);
        }
    }

    fn get_process_id(&self) -> u32 {
        unsafe { c::thread_getProcessId(self.cthread) }
    }

    fn get_host_id(&self) -> u32 {
        unsafe { c::thread_getHostId(self.cthread) }
    }

    fn get_system_pid(&self) -> libc::pid_t {
        unsafe { c::thread_getNativePid(self.cthread) }
    }
}

impl Drop for CThread {
    fn drop(&mut self) {
        unsafe {
            c::thread_unref(self.cthread);
        }
    }
}
