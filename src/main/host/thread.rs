use super::syscall_types::*;
use crate::cbindings as c;
use crate::utility::syscall;
use libc;
use SysCallReg::*;

/// Wraps the C Thread struct.
pub struct Thread {
    cthread: *mut c::Thread,
}

impl Thread {
    pub fn new(cthread: *mut c::Thread) -> Thread {
        unsafe {
            c::thread_ref(cthread);
        }
        Thread { cthread }
    }

    fn native_syscall(&mut self, n: i64, args: &[SysCallReg]) -> Result<i64, i32> {
        let raw_res;
        unsafe {
            raw_res = match args.len() {
                0 => c::thread_nativeSyscall(self.cthread, n),
                1 => c::thread_nativeSyscall(self.cthread, n, args[0]),
                2 => c::thread_nativeSyscall(self.cthread, n, args[1], args[2]),
                3 => c::thread_nativeSyscall(self.cthread, n, args[1], args[2], args[3]),
                4 => c::thread_nativeSyscall(self.cthread, n, args[1], args[2], args[3], args[4]),
                5 => c::thread_nativeSyscall(
                    self.cthread,
                    n,
                    args[1],
                    args[2],
                    args[3],
                    args[4],
                    args[5],
                ),
                6 => c::thread_nativeSyscall(
                    self.cthread,
                    n,
                    args[1],
                    args[2],
                    args[3],
                    args[4],
                    args[5],
                    args[6],
                ),
                x => panic!("Bad number of syscall args {}", x),
            }
        }
        syscall::raw_return_value_to_errno(raw_res)
    }

    pub fn native_mmap(
        &mut self,
        addr: PluginPtr,
        len: usize,
        prot: i32,
        flags: i32,
        fd: i32,
        offset: i64,
    ) -> Result<PluginPtr, i32> {
        match self.native_syscall(
            libc::SYS_mmap,
            &[
                U64(addr.val),
                U64(len as u64),
                I64(prot.into()),
                I64(flags.into()),
                I64(fd.into()),
                I64(offset),
            ],
        ) {
            Ok(p) => Ok(c::PluginPtr { val: p as u64 }),
            Err(i) => Err(i),
        }
    }
}

impl Drop for Thread {
    fn drop(&mut self) {
        unsafe {
            c::thread_unref(self.cthread);
        }
    }
}
