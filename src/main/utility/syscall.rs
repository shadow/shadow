use shadow_shim_helper_rs::syscall_types::SyscallReg;

// Linux reserves -1 through -4095 for errors. See
// https://sourceware.org/git/?p=glibc.git;a=blob;f=sysdeps/unix/sysv/linux/x86_64/sysdep.h;h=24d8b8ec20a55824a4806f8821ecba2622d0fe8e;hb=HEAD#l41
const MAX_ERRNO: i64 = 4095;

pub fn raw_return_value_to_errno(rv: i64) -> Result<SyscallReg, i32> {
    if (-MAX_ERRNO..=-1).contains(&rv) {
        return Err(-rv as i32);
    }
    Ok(rv.into())
}

pub fn raw_return_value_to_result(rv: i64) -> Result<SyscallReg, linux_api::errno::Errno> {
    if let Ok(rv) = u16::try_from(-rv)
        && let Ok(errno) = linux_api::errno::Errno::try_from(rv)
    {
        return Err(errno);
    }

    Ok(rv.into())
}
