use shadow_shmem::allocator::ShMemBlockSerialized;
use vasi::VirtualAddressSpaceIndependent;

use crate::syscall_types::{SysCallArgs, SysCallReg};

#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(C)]
/// Data for [`ShimEvent::Syscall`]
pub struct ShimEventSyscall {
    pub syscall_args: SysCallArgs,
}

/// Data for [`ShimEvent::SyscallComplete`]
#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct ShimEventSyscallComplete {
    pub retval: SysCallReg,
    /// Whether the syscall is eligible to be restarted. Only applicable
    /// when retval is -EINTR. See signal(7).
    pub restartable: bool,
}

/// Data for [`ShimEvent::AddThreadReq`]
#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct ShimEventAddThreadReq {
    pub ipc_block: ShMemBlockSerialized,
}

/// A message between Shadow and the Shim.

#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
// SAFETY: `shimevent_getId` assumes this representation.
#[repr(u32)]
// Clippy suggests boxing large enum variants. We can't do that, since
// it'd make ShimEvent unsafe for use in shared memory.
#[allow(clippy::large_enum_variant)]
pub enum ShimEvent {
    Null,
    /// Sent from Shadow to Shim to allow a shim thread to start executing
    /// after creation.
    Start,
    /// The whole process has died.
    /// We inject this event to trigger cleanup after we've detected that the
    /// native process has died.
    ProcessDeath,
    /// Sent from Shim to Shadow to request handling of a syscall.
    Syscall(ShimEventSyscall),
    /// Response from Shadow for a completed emulated syscall.
    SyscallComplete(ShimEventSyscallComplete),
    /// Request from Shadow to Shim to execute a syscall natively.
    SyscallDoNative,
    /// Request from Shadow to Shim to take the included shared memory block,
    /// which holds an `IpcData`, and use it to initialize a newly spawned
    /// thread.
    AddThreadReq(ShimEventAddThreadReq),
    /// Response from Shim to Shadow that `AddThreadReq` has completed.
    AddThreadParentRes,
}

mod export {
    use super::*;

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_getId(event: *const ShimEvent) -> u32 {
        let event = unsafe { event.as_ref().unwrap() };
        // Example cast taken from documentation for `std::mem::Discriminant`.
        //
        // SAFETY: In a repr(Int) or repr(C, Int) struct, The integer discrimenant
        // is guaranteed to be at the start of the object.
        // * https://github.com/rust-lang/rfcs/blob/master/text/2195-really-tagged-unions.md
        unsafe { *<*const _>::from(event).cast::<u32>() }
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_getSyscallData(
        event: *const ShimEvent,
    ) -> *const ShimEventSyscall {
        let event = unsafe { event.as_ref().unwrap() };
        match event {
            ShimEvent::Syscall(data) => data,
            _ => {
                panic!("Unexpected event type: {event:?}");
            }
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_getSyscallCompleteData(
        event: *const ShimEvent,
    ) -> *const ShimEventSyscallComplete {
        let event = unsafe { event.as_ref().unwrap() };
        match event {
            ShimEvent::SyscallComplete(data) => data,
            _ => {
                panic!("Unexpected event type: {event:?}");
            }
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_getAddThreadReqData(
        event: *const ShimEvent,
    ) -> *const ShimEventAddThreadReq {
        let event = unsafe { event.as_ref().unwrap() };
        match event {
            ShimEvent::AddThreadReq(data) => data,
            _ => {
                panic!("Unexpected event type: {event:?}");
            }
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_initSyscall(
        dst: *mut ShimEvent,
        syscall_args: *const SysCallArgs,
    ) {
        let syscall_args = unsafe { syscall_args.as_ref().unwrap() };
        let event = ShimEvent::Syscall(ShimEventSyscall {
            syscall_args: *syscall_args,
        });
        unsafe { dst.write(event) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_initSysCallComplete(
        dst: *mut ShimEvent,
        retval: SysCallReg,
        restartable: bool,
    ) {
        let event = ShimEvent::SyscallComplete(ShimEventSyscallComplete {
            retval,
            restartable,
        });
        unsafe { dst.write(event) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_initAddThreadParentRes(dst: *mut ShimEvent) {
        let event = ShimEvent::AddThreadParentRes;
        unsafe { dst.write(event) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_initSyscallDoNative(dst: *mut ShimEvent) {
        let event = ShimEvent::SyscallDoNative;
        unsafe { dst.write(event) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_initAddThreadReq(
        dst: *mut ShimEvent,
        ipc_block: *const ShMemBlockSerialized,
    ) {
        let ipc_block = unsafe { ipc_block.as_ref().unwrap() };
        let event = ShimEvent::AddThreadReq(ShimEventAddThreadReq {
            ipc_block: *ipc_block,
        });
        unsafe { dst.write(event) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_initStart(dst: *mut ShimEvent) {
        let event = ShimEvent::Start;
        unsafe { dst.write(event) };
    }
}
