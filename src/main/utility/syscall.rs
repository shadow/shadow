use crate::host::syscall_types::SysCallReg;

pub fn raw_return_value_to_errno(rv: i64) -> Result<SysCallReg, i32> {
    if rv <= -1 && rv >= -4095 {
        // Linux reserves -1 through -4095 for errors. See
        // https://sourceware.org/git/?p=glibc.git;a=blob;f=sysdeps/unix/sysv/linux/x86_64/sysdep.h;h=24d8b8ec20a55824a4806f8821ecba2622d0fe8e;hb=HEAD#l41
        return Err(-rv as i32);
    }
    return Ok(rv.into());
}
