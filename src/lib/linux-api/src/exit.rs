use linux_syscall::Result as LinuxSyscallResult;

use crate::errno::Errno;

/// Exits the current thread, setting `val & 0xff` as the exit code.
pub fn exit_raw(val: i32) -> Result<(), Errno> {
    unsafe { linux_syscall::syscall!(linux_syscall::SYS_exit, val) }
        .check()
        .map_err(Errno::from)
}

/// Exits the current thread, setting `val` as the exit code.
pub fn exit(val: i8) -> ! {
    exit_raw(val.into()).unwrap();
    unreachable!()
}

/// Exits the process, setting `val & 0xff` as the exit code.
pub fn exit_group_raw(val: i32) -> Result<(), Errno> {
    unsafe { linux_syscall::syscall!(linux_syscall::SYS_exit_group, val) }
        .check()
        .map_err(Errno::from)
}

/// Exits the current process, setting `val` as the exit code.
pub fn exit_group(val: i8) -> ! {
    exit_group_raw(val.into()).unwrap();
    unreachable!()
}
