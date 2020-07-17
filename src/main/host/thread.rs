use super::syscall_types::{PluginPtr, SysCallReg};
use crate::cbindings as c;
use crate::utility::syscall;
use libc;

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

    fn native_syscall(&mut self, n: i64, args: &[SysCallReg]) -> Result<SysCallReg, i32> {
        let raw_res;
        // The `Into` here isn't strictly necessary since SysCallReg is an alias for c::SysCallReg,
        // but that's an implementation detail of SysCallReg that could change later, and the
        // compiler won't help us catch it when calling a variadic C function.
        //
        // We considered using an iterator here rather than having to pass an index everywhere
        // below; we avoided it because argument evaluation order is currently a bit of a murky
        // issue, even though it'll *probably* always be left-to-right.
        // https://internals.rust-lang.org/t/rust-expression-order-of-evaluation/2605/16
        let arg = |i| Into::<c::SysCallReg>::into(args[i]);
        unsafe {
            raw_res = match args.len() {
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
        Ok(self
            .native_syscall(
                libc::SYS_mmap,
                &[
                    addr.into(),
                    len.into(),
                    prot.into(),
                    flags.into(),
                    fd.into(),
                    offset.into(),
                ],
            )?
            .into())
    }
}

impl Drop for Thread {
    fn drop(&mut self) {
        unsafe {
            c::thread_unref(self.cthread);
        }
    }
}
