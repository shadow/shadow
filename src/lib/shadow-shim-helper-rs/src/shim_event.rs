/// cbindgen:prefix-with-name=false
#[repr(C)]
pub enum ShimEventID {
    Null = 0,
    Start = 1,
    /// The whole process has died.
    /// We inject this event to trigger cleanup after we've detected that the
    /// native process has died.
    ProcessDeath = 2,
    Syscall = 3,
    SyscallComplete = 4,
    SyscallDoNative = 8,
    CloneReq = 5,
    CloneStringReq = 9,
    ShmemComplete = 6,
    WriteReq = 7,
    Block = 10,
    AddThreadReq = 11,
    AddThreadParentRes = 12,
}
