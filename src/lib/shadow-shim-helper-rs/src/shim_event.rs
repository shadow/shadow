use shadow_shmem::allocator::ShMemBlockSerialized;
use vasi::VirtualAddressSpaceIndependent;

use crate::syscall_types::{SysCallArgs, SysCallReg};

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
}

/// A message between Shadow and the Shim.

#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
// SAFETY: `shimevent2shadow_getId` assumes this representation.
#[repr(u32)]
// Clippy suggests boxing large enum variants. We can't do that, since
// it'd make ShimEvent unsafe for use in shared memory.
#[allow(clippy::large_enum_variant)]
pub enum ShimEventToShadow {
    Null,
    /// The whole process has died.
    /// We inject this event to trigger cleanup after we've detected that the
    /// native process has died.
    ProcessDeath,
    /// Request to emulate the given syscall.
    Syscall(ShimEventSyscall),
    /// Response to ShimEventToShim::Syscall
    SyscallComplete(ShimEventSyscallComplete),
    /// Response to `ShimEventToShim::AddThreadReq`
    AddThreadParentRes,
}

#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
// SAFETY: `shimevent2shim_getId` assumes this representation.
#[repr(u32)]
// Clippy suggests boxing large enum variants. We can't do that, since
// it'd make ShimEvent unsafe for use in shared memory.
#[allow(clippy::large_enum_variant)]
pub enum ShimEventToShim {
    /// Sent from Shadow to Shim to allow a shim thread to start executing
    /// after creation.
    Start,
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

mod export {
    use super::*;

    #[no_mangle]
    pub unsafe extern "C" fn shimevent2shadow_getId(event: *const ShimEventToShadow) -> u32 {
        let event = unsafe { event.as_ref().unwrap() };
        // Example cast taken from documentation for `std::mem::Discriminant`.
        //
        // SAFETY: In a repr(Int) or repr(C, Int) struct, The integer discrimenant
        // is guaranteed to be at the start of the object.
        // * https://github.com/rust-lang/rfcs/blob/master/text/2195-really-tagged-unions.md
        unsafe { *<*const _>::from(event).cast::<u32>() }
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent2shim_getId(event: *const ShimEventToShim) -> u32 {
        let event = unsafe { event.as_ref().unwrap() };
        // Example cast taken from documentation for `std::mem::Discriminant`.
        //
        // SAFETY: In a repr(Int) or repr(C, Int) struct, The integer discrimenant
        // is guaranteed to be at the start of the object.
        // * https://github.com/rust-lang/rfcs/blob/master/text/2195-really-tagged-unions.md
        unsafe { *<*const _>::from(event).cast::<u32>() }
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent2shadow_getSyscallData(
        event: *const ShimEventToShadow,
    ) -> *const ShimEventSyscall {
        let event = unsafe { event.as_ref().unwrap() };
        match event {
            ShimEventToShadow::Syscall(data) => data,
            _ => {
                panic!("Unexpected event type: {event:?}");
            }
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent2shim_getSyscallCompleteData(
        event: *const ShimEventToShim,
    ) -> *const ShimEventSyscallComplete {
        let event = unsafe { event.as_ref().unwrap() };
        match event {
            ShimEventToShim::SyscallComplete(data) => data,
            _ => {
                panic!("Unexpected event type: {event:?}");
            }
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent2shim_getSyscallData(
        event: *const ShimEventToShim,
    ) -> *const ShimEventSyscall {
        let event = unsafe { event.as_ref().unwrap() };
        match event {
            ShimEventToShim::Syscall(data) => data,
            _ => {
                panic!("Unexpected event type: {event:?}");
            }
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent2shadow_getSyscallCompleteData(
        event: *const ShimEventToShadow,
    ) -> *const ShimEventSyscallComplete {
        let event = unsafe { event.as_ref().unwrap() };
        match event {
            ShimEventToShadow::SyscallComplete(data) => data,
            _ => {
                panic!("Unexpected event type: {event:?}");
            }
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent2shim_getAddThreadReqData(
        event: *const ShimEventToShim,
    ) -> *const ShimEventAddThreadReq {
        let event = unsafe { event.as_ref().unwrap() };
        match event {
            ShimEventToShim::AddThreadReq(data) => data,
            _ => {
                panic!("Unexpected event type: {event:?}");
            }
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent2shadow_initSyscall(
        dst: *mut ShimEventToShadow,
        syscall_args: *const SysCallArgs,
    ) {
        let syscall_args = unsafe { syscall_args.as_ref().unwrap() };
        let event = ShimEventToShadow::Syscall(ShimEventSyscall {
            syscall_args: *syscall_args,
        });
        unsafe { dst.write(event) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent2shim_initSysCallComplete(
        dst: *mut ShimEventToShim,
        retval: SysCallReg,
        restartable: bool,
    ) {
        let event = ShimEventToShim::SyscallComplete(ShimEventSyscallComplete {
            retval,
            restartable,
        });
        unsafe { dst.write(event) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent2shim_initSyscall(
        dst: *mut ShimEventToShim,
        syscall_args: *const SysCallArgs,
    ) {
        let syscall_args = unsafe { syscall_args.as_ref().unwrap() };
        let event = ShimEventToShim::Syscall(ShimEventSyscall {
            syscall_args: *syscall_args,
        });
        unsafe { dst.write(event) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent2shadow_initSysCallComplete(
        dst: *mut ShimEventToShadow,
        retval: SysCallReg,
        restartable: bool,
    ) {
        let event = ShimEventToShadow::SyscallComplete(ShimEventSyscallComplete {
            retval,
            restartable,
        });
        unsafe { dst.write(event) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent2shadow_initAddThreadParentRes(dst: *mut ShimEventToShadow) {
        let event = ShimEventToShadow::AddThreadParentRes;
        unsafe { dst.write(event) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_initSyscallDoNative(dst: *mut ShimEventToShim) {
        let event = ShimEventToShim::SyscallDoNative;
        unsafe { dst.write(event) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_initAddThreadReq(
        dst: *mut ShimEventToShim,
        ipc_block: *const ShMemBlockSerialized,
    ) {
        let ipc_block = unsafe { ipc_block.as_ref().unwrap() };
        let event = ShimEventToShim::AddThreadReq(ShimEventAddThreadReq {
            ipc_block: *ipc_block,
        });
        unsafe { dst.write(event) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_initStart(dst: *mut ShimEventToShim) {
        let event = ShimEventToShim::Start;
        unsafe { dst.write(event) };
    }
}
