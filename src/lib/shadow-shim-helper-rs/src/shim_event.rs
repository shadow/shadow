use shadow_shmem::allocator::ShMemBlockSerialized;
use vasi::VirtualAddressSpaceIndependent;

use crate::syscall_types::{PluginPtr, SysCallArgs, SysCallReg};

#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
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

/// Data for [`ShimEventID::Syscall`]
#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct ShimEventSyscall {
    pub syscall_args: SysCallArgs,
}

/// Data for [`ShimEventID::SyscallComplete`]
#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct ShimEventSyscallComplete {
    pub retval: SysCallReg,
    /// Whether the syscall is eligible to be restarted. Only applicable
    /// when retval is -EINTR. See signal(7).
    pub restartable: bool,
}

/// Data for several shared-memory shim events.
/// TODO: Document for which. Will be easier to see and maintain once ShimEvent
/// is refactored into an enum.
#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct ShimEventShmemBlk {
    pub serial: ShMemBlockSerialized,
    pub plugin_ptr: PluginPtr,
    pub n: usize,
}

/// Data for [`ShimEventID::AddThreadReq`]
#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct ShimEventAddThreadReq {
    pub ipc_block: ShMemBlockSerialized,
}

/// Data for [`ShimEvent`].
#[derive(Copy, Clone, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub union ShimEventData {
    pub none: (),
    pub syscall: ShimEventSyscall,
    pub syscall_complete: ShimEventSyscallComplete,
    pub shmem_blk: ShimEventShmemBlk,
    pub add_thread_req: ShimEventAddThreadReq,
}

/// A message between Shadow and the Shim.
/// TODO: Refactor from a tagged union into an enum.
#[derive(Copy, Clone, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct ShimEvent {
    pub event_id: ShimEventID,
    pub event_data: ShimEventData,
}
