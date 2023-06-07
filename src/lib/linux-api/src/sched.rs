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
