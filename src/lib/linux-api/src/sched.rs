use linux_syscall::{Result as LinuxSyscallResult, Result64};

use crate::errno::Errno;
use crate::ldt::linux_user_desc;
use crate::posix_types::{kernel_pid_t, Pid};
use crate::signal::Signal;
use crate::{bindings, const_conversions};

bitflags::bitflags! {
    /// The flags passed to the `clone` and `clone3` syscalls.
    /// While `clone` is documented as taking an i32 parameter for flags,
    /// in `clone3` its a u64. Promote to u64 throughout.
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct CloneFlags: u64 {
        const CLONE_CLEAR_SIGHAND = bindings::LINUX_CLONE_CLEAR_SIGHAND;
        const CLONE_INTO_CGROUP = bindings::LINUX_CLONE_INTO_CGROUP;
        const CLONE_NEWTIME = const_conversions::u64_from_u32(bindings::LINUX_CLONE_NEWTIME);
        const CLONE_VM = const_conversions::u64_from_u32(bindings::LINUX_CLONE_VM);
        const CLONE_FS = const_conversions::u64_from_u32(bindings::LINUX_CLONE_FS);
        const CLONE_FILES = const_conversions::u64_from_u32(bindings::LINUX_CLONE_FILES);
        const CLONE_SIGHAND = const_conversions::u64_from_u32(bindings::LINUX_CLONE_SIGHAND);
        const CLONE_PIDFD = const_conversions::u64_from_u32(bindings::LINUX_CLONE_PIDFD);
        const CLONE_PTRACE = const_conversions::u64_from_u32(bindings::LINUX_CLONE_PTRACE);
        const CLONE_VFORK = const_conversions::u64_from_u32(bindings::LINUX_CLONE_VFORK);
        const CLONE_PARENT = const_conversions::u64_from_u32(bindings::LINUX_CLONE_PARENT);
        const CLONE_THREAD = const_conversions::u64_from_u32(bindings::LINUX_CLONE_THREAD);
        const CLONE_NEWNS = const_conversions::u64_from_u32(bindings::LINUX_CLONE_NEWNS);
        const CLONE_SYSVSEM = const_conversions::u64_from_u32(bindings::LINUX_CLONE_SYSVSEM);
        const CLONE_SETTLS = const_conversions::u64_from_u32(bindings::LINUX_CLONE_SETTLS);
        const CLONE_PARENT_SETTID = const_conversions::u64_from_u32(bindings::LINUX_CLONE_PARENT_SETTID);
        const CLONE_CHILD_CLEARTID = const_conversions::u64_from_u32(bindings::LINUX_CLONE_CHILD_CLEARTID);
        const CLONE_DETACHED = const_conversions::u64_from_u32(bindings::LINUX_CLONE_DETACHED);
        const CLONE_UNTRACED = const_conversions::u64_from_u32(bindings::LINUX_CLONE_UNTRACED);
        const CLONE_CHILD_SETTID = const_conversions::u64_from_u32(bindings::LINUX_CLONE_CHILD_SETTID);
        const CLONE_NEWCGROUP = const_conversions::u64_from_u32(bindings::LINUX_CLONE_NEWCGROUP);
        const CLONE_NEWUTS = const_conversions::u64_from_u32(bindings::LINUX_CLONE_NEWUTS);
        const CLONE_NEWIPC = const_conversions::u64_from_u32(bindings::LINUX_CLONE_NEWIPC);
        const CLONE_NEWUSER = const_conversions::u64_from_u32(bindings::LINUX_CLONE_NEWUSER);
        const CLONE_NEWPID = const_conversions::u64_from_u32(bindings::LINUX_CLONE_NEWPID);
        const CLONE_NEWNET = const_conversions::u64_from_u32(bindings::LINUX_CLONE_NEWNET);
        const CLONE_IO = const_conversions::u64_from_u32(bindings::LINUX_CLONE_IO);
    }
}

pub use bindings::linux_clone_args;
#[allow(non_camel_case_types)]
pub type clone_args = linux_clone_args;

#[allow(clippy::derivable_impls)]
impl Default for clone_args {
    fn default() -> Self {
        Self {
            flags: Default::default(),
            pidfd: Default::default(),
            child_tid: Default::default(),
            parent_tid: Default::default(),
            exit_signal: Default::default(),
            stack: Default::default(),
            stack_size: Default::default(),
            tls: Default::default(),
            set_tid: Default::default(),
            set_tid_size: Default::default(),
            cgroup: Default::default(),
        }
    }
}

impl clone_args {
    #[inline]
    pub fn with_flags(mut self, flags: CloneFlags) -> Self {
        self.flags = flags.bits();
        self
    }

    #[inline]
    pub fn with_exit_signal(mut self, exit_signal: Option<Signal>) -> Self {
        self.exit_signal = u64::try_from(Signal::as_raw(exit_signal)).unwrap();
        self
    }
}

unsafe impl shadow_pod::Pod for clone_args {}

/// The "dumpable" state, as manipulated via the prctl operations `PR_SET_DUMPABLE` and
/// `PR_GET_DUMPABLE`.
#[derive(Copy, Clone, PartialEq, Eq)]
pub struct SuidDump(i32);

impl SuidDump {
    pub const SUID_DUMP_DISABLE: Self = Self(const_conversions::i32_from_u32(
        bindings::LINUX_SUID_DUMP_DISABLE,
    ));
    pub const SUID_DUMP_USER: Self = Self(const_conversions::i32_from_u32(
        bindings::LINUX_SUID_DUMP_USER,
    ));
    pub const SUID_DUMP_ROOT: Self = Self(const_conversions::i32_from_u32(
        bindings::LINUX_SUID_DUMP_ROOT,
    ));
    // NOTE: add new entries to `to_str` below

    pub const fn new(val: i32) -> Self {
        Self(val)
    }

    pub const fn val(&self) -> i32 {
        self.0
    }

    pub const fn to_str(&self) -> Option<&'static str> {
        match *self {
            Self::SUID_DUMP_DISABLE => Some("SUID_DUMP_DISABLE"),
            Self::SUID_DUMP_USER => Some("SUID_DUMP_USER"),
            Self::SUID_DUMP_ROOT => Some("SUID_DUMP_ROOT"),
            _ => None,
        }
    }
}

impl core::fmt::Display for SuidDump {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> Result<(), core::fmt::Error> {
        match self.to_str() {
            Some(s) => formatter.write_str(s),
            None => write!(formatter, "(unknown dumpable option {})", self.0),
        }
    }
}

impl core::fmt::Debug for SuidDump {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> Result<(), core::fmt::Error> {
        match self.to_str() {
            Some(s) => write!(formatter, "SuidDump::{s}"),
            None => write!(formatter, "SuidDump::<{}>", self.0),
        }
    }
}

pub fn sched_yield() -> Result<(), Errno> {
    unsafe { linux_syscall::syscall!(linux_syscall::SYS_sched_yield) }
        .check()
        .map_err(Errno::from)
}

/// # Safety
///
/// Too many requirements to list here. See `clone(2)`.
pub unsafe fn clone_raw(
    flags: core::ffi::c_ulong,
    stack: core::ffi::c_ulong,
    parent_tid: *mut core::ffi::c_int,
    child_tid: *mut core::ffi::c_int,
    tls: core::ffi::c_ulong,
) -> Result<core::ffi::c_long, Errno> {
    unsafe {
        linux_syscall::syscall!(
            linux_syscall::SYS_clone,
            flags,
            stack,
            parent_tid,
            child_tid,
            tls
        )
    }
    .try_i64()
    .map_err(Errno::from)
}

/// # Safety
///
/// Too many requirements to list here. See `clone(2)`.
pub unsafe fn clone3_raw(args: *const clone_args, size: usize) -> Result<core::ffi::c_long, Errno> {
    unsafe { linux_syscall::syscall!(linux_syscall::SYS_clone3, args, size,) }
        .try_i64()
        .map_err(Errno::from)
}

pub enum CloneResult {
    CallerIsChild,
    // Caller is the parent; child has the given pid
    CallerIsParent(Pid),
}

/// # Safety
///
/// Too many requirements to list here. See `clone(2)`.
pub unsafe fn clone(
    flags: CloneFlags,
    exit_signal: Option<Signal>,
    stack: *mut core::ffi::c_void,
    parent_tid: *mut kernel_pid_t,
    child_tid: *mut kernel_pid_t,
    tls: *mut linux_user_desc,
) -> Result<CloneResult, Errno> {
    unsafe {
        clone_raw(
            flags.bits() | u64::try_from(Signal::as_raw(exit_signal)).unwrap(),
            stack as core::ffi::c_ulong,
            parent_tid,
            child_tid,
            tls as core::ffi::c_ulong,
        )
    }
    .map(|res| match res.cmp(&0) {
        core::cmp::Ordering::Equal => CloneResult::CallerIsChild,
        core::cmp::Ordering::Greater => {
            CloneResult::CallerIsParent(Pid::from_raw(res.try_into().unwrap()).unwrap())
        }
        core::cmp::Ordering::Less => unreachable!(),
    })
}

/// # Safety
///
/// Too many requirements to list here. See `clone(2)`.
pub unsafe fn clone3(args: &clone_args) -> Result<CloneResult, Errno> {
    unsafe { clone3_raw(args, core::mem::size_of::<clone_args>()) }.map(|res| match res.cmp(&0) {
        core::cmp::Ordering::Equal => CloneResult::CallerIsChild,
        core::cmp::Ordering::Greater => {
            CloneResult::CallerIsParent(Pid::from_raw(res.try_into().unwrap()).unwrap())
        }
        core::cmp::Ordering::Less => unreachable!(),
    })
}

/// See `fork(2)`.
///
/// # Safety
///
/// *Mostly* safe, since most memory will be copy-on-write in the child process.
/// Non-private mutable mappings *are* shared in the child, though, which may
/// break soundness. (Such mappings aren't common in practice)
///
/// Additionally some OS resources are shared with the parent, and others are
/// dropped, which may /// break assumptions by code that uses or wraps such
/// resources. See `fork(2)` for a full list, but some notable examples include:
///
/// * The child shares file descriptors (and underlying file descriptions) with the parent.
/// * The child is the only thread in the new process.
pub unsafe fn fork_raw() -> Result<core::ffi::c_long, Errno> {
    unsafe { linux_syscall::syscall!(linux_syscall::SYS_fork) }
        .try_i64()
        .map_err(Errno::from)
}

/// # Safety
///
/// See `fork_raw`
pub unsafe fn fork() -> Result<CloneResult, Errno> {
    unsafe { fork_raw() }.map(|res| match res.cmp(&0) {
        core::cmp::Ordering::Equal => CloneResult::CallerIsChild,
        core::cmp::Ordering::Greater => {
            CloneResult::CallerIsParent(Pid::from_raw(res.try_into().unwrap()).unwrap())
        }
        core::cmp::Ordering::Less => unreachable!(),
    })
}
