use crate::bindings;

bitflags::bitflags! {
    // While `clone` is documented as taking an i32 parameter for flags,
    // in `clone3` its a u64. Promote to u64 throughout.
    #[derive(Copy, Clone, Debug)]
    pub struct LinuxCloneFlags: u64 {
        const CLONE_CLEAR_SIGHAND = bindings::CLONE_CLEAR_SIGHAND;
        const CLONE_INTO_CGROUP = bindings::CLONE_INTO_CGROUP;
        const CLONE_NEWTIME = bindings::CLONE_NEWTIME as u64;

        const CLONE_VM = bindings::CLONE_VM as u64;
        const CLONE_FS = bindings::CLONE_FS as u64;
        const CLONE_FILES = bindings::CLONE_FILES as u64;
        const CLONE_SIGHAND = bindings::CLONE_SIGHAND as u64;
        const CLONE_PIDFD = bindings::CLONE_PIDFD as u64;
        const CLONE_PTRACE = bindings::CLONE_PTRACE as u64;
        const CLONE_VFORK = bindings::CLONE_VFORK as u64;
        const CLONE_PARENT = bindings::CLONE_PARENT as u64;
        const CLONE_THREAD = bindings::CLONE_THREAD as u64;
        const CLONE_NEWNS = bindings::CLONE_NEWNS as u64;
        const CLONE_SYSVSEM = bindings::CLONE_SYSVSEM as u64;
        const CLONE_SETTLS = bindings::CLONE_SETTLS as u64;
        const CLONE_PARENT_SETTID = bindings::CLONE_PARENT_SETTID as u64;
        const CLONE_CHILD_CLEARTID = bindings::CLONE_CHILD_CLEARTID as u64;
        const CLONE_DETACHED = bindings::CLONE_DETACHED as u64;
        const CLONE_UNTRACED = bindings::CLONE_UNTRACED as u64;
        const CLONE_CHILD_SETTID = bindings::CLONE_CHILD_SETTID as u64;
        const CLONE_NEWCGROUP = bindings::CLONE_NEWCGROUP as u64;
        const CLONE_NEWUTS = bindings::CLONE_NEWUTS as u64;
        const CLONE_NEWIPC = bindings::CLONE_NEWIPC as u64;
        const CLONE_NEWUSER = bindings::CLONE_NEWUSER as u64;
        const CLONE_NEWPID = bindings::CLONE_NEWPID as u64;
        const CLONE_NEWNET = bindings::CLONE_NEWNET as u64;
        const CLONE_IO = bindings::CLONE_IO as u64;
    }
}
