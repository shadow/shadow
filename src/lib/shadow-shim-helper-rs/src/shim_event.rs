use shadow_shmem::allocator::ShMemBlockSerialized;
use vasi::VirtualAddressSpaceIndependent;

use crate::syscall_types::{ForeignPtr, SysCallArgs, SysCallReg, UntypedForeignPtr};

#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(C)]
/// Data for [`ShimEventToShim::Syscall`] and [`ShimEventToShadow::Syscall`]
pub struct ShimEventSyscall {
    pub syscall_args: SysCallArgs,
}

/// Data for [`ShimEventToShim::SyscallComplete`] and [`ShimEventToShadow::SyscallComplete`]
#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct ShimEventSyscallComplete {
    pub retval: SysCallReg,
    /// Whether the syscall is eligible to be restarted. Only applicable
    /// when retval is -EINTR. See signal(7).
    pub restartable: bool,
}

/// Data for [`ShimEventToShim::AddThreadReq`]
#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct ShimEventAddThreadReq {
    pub ipc_block: ShMemBlockSerialized,
    /// clone flags.
    pub flags: libc::c_ulong,
    /// clone stack. u8 pointer in shim's memory
    pub child_stack: UntypedForeignPtr,
    /// clone ptid. pid_t pointer in shim's memory
    pub ptid: UntypedForeignPtr,
    /// clone ctid. pid_t pointer in shim's memory
    pub ctid: UntypedForeignPtr,
    /// clone tls.
    pub newtls: libc::c_ulong,
}

/// Data for [`ShimEventToShadow::AddThreadRes`]
#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct ShimEventAddThreadRes {
    pub clone_res: i64,
}

/// Data for [`ShimEventToShadow::StartReq`]
#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct ShimEventStartReq {
    /// Shim pointer to be initd by Shadow. Required.
    pub thread_shmem_block_to_init: ForeignPtr<ShMemBlockSerialized>,
    /// Shim pointer to be initd by Shadow. Optional.
    pub process_shmem_block_to_init: ForeignPtr<ShMemBlockSerialized>,
}

/// A message between Shadow and the Shim.

#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
// SAFETY: `shimevent2shadow_getId` assumes this representation.
#[repr(u32)]
// Clippy suggests boxing large enum variants. We can't do that, since
// it'd make ShimEvent unsafe for use in shared memory.
#[allow(clippy::large_enum_variant)]
pub enum ShimEventToShadow {
    /// First message from the shim, requesting that it's ready to start
    /// executing.
    StartReq(ShimEventStartReq),
    /// The whole process has died.
    /// We inject this event to trigger cleanup after we've detected that the
    /// native process has died.
    ProcessDeath,
    /// Request to emulate the given syscall.
    Syscall(ShimEventSyscall),
    /// Response to ShimEventToShim::Syscall
    SyscallComplete(ShimEventSyscallComplete),
    /// Response to `ShimEventToShim::AddThreadReq`
    AddThreadRes(ShimEventAddThreadRes),
}

#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
// SAFETY: `shimevent2shim_getId` assumes this representation.
#[repr(u32)]
// Clippy suggests boxing large enum variants. We can't do that, since
// it'd make ShimEvent unsafe for use in shared memory.
#[allow(clippy::large_enum_variant)]
pub enum ShimEventToShim {
    /// First message from shadow, indicating that it is ready for
    /// the shim to start executing.
    StartRes,
    /// Request to execute the given syscall natively.
    Syscall(ShimEventSyscall),
    /// Request from Shadow to Shim to take the included shared memory block,
    /// which holds an `IpcData`, and use it to initialize a newly spawned
    /// thread.
    AddThreadReq(ShimEventAddThreadReq),
    /// Response to ShimEventToShadow::Syscall
    SyscallComplete(ShimEventSyscallComplete),
    /// Response to ShimEventToShadow::Syscall indicating to execute it
    /// natively.
    SyscallDoNative,
}
