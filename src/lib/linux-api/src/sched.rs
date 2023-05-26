use crate::bindings;

bitflags::bitflags! {
    // While `clone` is documented as taking an i32 parameter for flags,
    // in `clone3` its a u64. Promote to u64 throughout.
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct CloneFlags: u64 {
        const CLEAR_SIGHAND = bindings::LINUX_CLONE_CLEAR_SIGHAND;
        const INTO_CGROUP = bindings::LINUX_CLONE_INTO_CGROUP;
        const NEWTIME = bindings::LINUX_CLONE_NEWTIME as u64;
        const VM = bindings::LINUX_CLONE_VM as u64;
        const FS = bindings::LINUX_CLONE_FS as u64;
        const FILES = bindings::LINUX_CLONE_FILES as u64;
        const SIGHAND = bindings::LINUX_CLONE_SIGHAND as u64;
        const PIDFD = bindings::LINUX_CLONE_PIDFD as u64;
        const PTRACE = bindings::LINUX_CLONE_PTRACE as u64;
        const VFORK = bindings::LINUX_CLONE_VFORK as u64;
        const PARENT = bindings::LINUX_CLONE_PARENT as u64;
        const THREAD = bindings::LINUX_CLONE_THREAD as u64;
        const NEWNS = bindings::LINUX_CLONE_NEWNS as u64;
        const SYSVSEM = bindings::LINUX_CLONE_SYSVSEM as u64;
        const SETTLS = bindings::LINUX_CLONE_SETTLS as u64;
        const PARENT_SETTID = bindings::LINUX_CLONE_PARENT_SETTID as u64;
        const CHILD_CLEARTID = bindings::LINUX_CLONE_CHILD_CLEARTID as u64;
        const DETACHED = bindings::LINUX_CLONE_DETACHED as u64;
        const UNTRACED = bindings::LINUX_CLONE_UNTRACED as u64;
        const CHILD_SETTID = bindings::LINUX_CLONE_CHILD_SETTID as u64;
        const NEWCGROUP = bindings::LINUX_CLONE_NEWCGROUP as u64;
        const NEWUTS = bindings::LINUX_CLONE_NEWUTS as u64;
        const NEWIPC = bindings::LINUX_CLONE_NEWIPC as u64;
        const NEWUSER = bindings::LINUX_CLONE_NEWUSER as u64;
        const NEWPID = bindings::LINUX_CLONE_NEWPID as u64;
        const NEWNET = bindings::LINUX_CLONE_NEWNET as u64;
        const IO = bindings::LINUX_CLONE_IO as u64;
    }
}
