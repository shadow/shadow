use crate::bindings;

bitflags::bitflags! {
    // While `clone` is documented as taking an i32 parameter for flags,
    // in `clone3` its a u64. Promote to u64 throughout.
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct LinuxCloneFlags: u64 {
        const CLONE_CLEAR_SIGHAND = bindings::LINUX_CLONE_CLEAR_SIGHAND;
        const CLONE_INTO_CGROUP = bindings::LINUX_CLONE_INTO_CGROUP;
        const CLONE_NEWTIME = bindings::LINUX_CLONE_NEWTIME as u64;
        const CLONE_VM = bindings::LINUX_CLONE_VM as u64;
        const CLONE_FS = bindings::LINUX_CLONE_FS as u64;
        const CLONE_FILES = bindings::LINUX_CLONE_FILES as u64;
        const CLONE_SIGHAND = bindings::LINUX_CLONE_SIGHAND as u64;
        const CLONE_PIDFD = bindings::LINUX_CLONE_PIDFD as u64;
        const CLONE_PTRACE = bindings::LINUX_CLONE_PTRACE as u64;
        const CLONE_VFORK = bindings::LINUX_CLONE_VFORK as u64;
        const CLONE_PARENT = bindings::LINUX_CLONE_PARENT as u64;
        const CLONE_THREAD = bindings::LINUX_CLONE_THREAD as u64;
        const CLONE_NEWNS = bindings::LINUX_CLONE_NEWNS as u64;
        const CLONE_SYSVSEM = bindings::LINUX_CLONE_SYSVSEM as u64;
        const CLONE_SETTLS = bindings::LINUX_CLONE_SETTLS as u64;
        const CLONE_PARENT_SETTID = bindings::LINUX_CLONE_PARENT_SETTID as u64;
        const CLONE_CHILD_CLEARTID = bindings::LINUX_CLONE_CHILD_CLEARTID as u64;
        const CLONE_DETACHED = bindings::LINUX_CLONE_DETACHED as u64;
        const CLONE_UNTRACED = bindings::LINUX_CLONE_UNTRACED as u64;
        const CLONE_CHILD_SETTID = bindings::LINUX_CLONE_CHILD_SETTID as u64;
        const CLONE_NEWCGROUP = bindings::LINUX_CLONE_NEWCGROUP as u64;
        const CLONE_NEWUTS = bindings::LINUX_CLONE_NEWUTS as u64;
        const CLONE_NEWIPC = bindings::LINUX_CLONE_NEWIPC as u64;
        const CLONE_NEWUSER = bindings::LINUX_CLONE_NEWUSER as u64;
        const CLONE_NEWPID = bindings::LINUX_CLONE_NEWPID as u64;
        const CLONE_NEWNET = bindings::LINUX_CLONE_NEWNET as u64;
        const CLONE_IO = bindings::LINUX_CLONE_IO as u64;
    }
}
