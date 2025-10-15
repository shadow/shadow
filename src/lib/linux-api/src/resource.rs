use num_enum::{IntoPrimitive, TryFromPrimitive};
use shadow_pod::Pod;

use crate::bindings;

#[allow(non_camel_case_types)]
pub type rusage = crate::bindings::linux_rusage;
unsafe impl Pod for rusage {}

#[allow(non_camel_case_types)]
pub type rlimit = crate::bindings::linux_rlimit;
unsafe impl Pod for rlimit {}

#[allow(non_camel_case_types)]
pub type rlimit64 = crate::bindings::linux_rlimit64;
unsafe impl Pod for rlimit64 {}

// Resource  identifier, as used in getrlimit, setrlimit, prlimit.
#[derive(Debug, Copy, Clone, Eq, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(u32)]
#[allow(non_camel_case_types)]
pub enum Resource {
    RLIMIT_CPU = bindings::LINUX_RLIMIT_CPU,
    RLIMIT_FSIZE = bindings::LINUX_RLIMIT_FSIZE,
    RLIMIT_DATA = bindings::LINUX_RLIMIT_DATA,
    RLIMIT_STACK = bindings::LINUX_RLIMIT_STACK,
    RLIMIT_CORE = bindings::LINUX_RLIMIT_CORE,
    RLIMIT_RSS = bindings::LINUX_RLIMIT_RSS,
    RLIMIT_NPROC = bindings::LINUX_RLIMIT_NPROC,
    RLIMIT_NOFILE = bindings::LINUX_RLIMIT_NOFILE,
    RLIMIT_MEMLOCK = bindings::LINUX_RLIMIT_MEMLOCK,
    RLIMIT_AS = bindings::LINUX_RLIMIT_AS,
    RLIMIT_LOCKS = bindings::LINUX_RLIMIT_LOCKS,
    RLIMIT_SIGPENDING = bindings::LINUX_RLIMIT_SIGPENDING,
    RLIMIT_MSGQUEUE = bindings::LINUX_RLIMIT_MSGQUEUE,
    RLIMIT_NICE = bindings::LINUX_RLIMIT_NICE,
    RLIMIT_RTPRIO = bindings::LINUX_RLIMIT_RTPRIO,
    RLIMIT_RTTIME = bindings::LINUX_RLIMIT_RTTIME,
}
