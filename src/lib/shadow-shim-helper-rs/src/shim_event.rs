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

mod export {
    use super::*;

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_getId(event: *const ShimEvent) -> ShimEventID {
        let event = unsafe { event.as_ref().unwrap() };
        event.event_id
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_getSyscallData(
        event: *const ShimEvent,
    ) -> *const ShimEventSyscall {
        let event = unsafe { event.as_ref().unwrap() };
        match event.event_id {
            ShimEventID::Syscall => (),
            id => panic!("Unexpected event id {id:?}"),
        };
        unsafe { &event.event_data.syscall }
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_getSyscallCompleteData(
        event: *const ShimEvent,
    ) -> *const ShimEventSyscallComplete {
        let event = unsafe { event.as_ref().unwrap() };
        match event.event_id {
            ShimEventID::SyscallComplete => (),
            id => panic!("Unexpected event id {id:?}"),
        };
        unsafe { &event.event_data.syscall_complete }
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_getShmemBlkData(
        event: *const ShimEvent,
    ) -> *const ShimEventShmemBlk {
        let event = unsafe { event.as_ref().unwrap() };
        match event.event_id {
            ShimEventID::CloneReq | ShimEventID::CloneStringReq | ShimEventID::WriteReq => (),
            id => panic!("Unexpected event id {id:?}"),
        };
        unsafe { &event.event_data.shmem_blk }
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_getAddThreadReqData(
        event: *const ShimEvent,
    ) -> *const ShimEventAddThreadReq {
        let event = unsafe { event.as_ref().unwrap() };
        match event.event_id {
            ShimEventID::AddThreadReq => (),
            id => panic!("Unexpected event id {id:?}"),
        };
        unsafe { &event.event_data.add_thread_req }
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_initSyscall(
        dst: *mut ShimEvent,
        syscall_args: *const SysCallArgs,
    ) {
        let syscall_args = unsafe { syscall_args.as_ref().unwrap() };
        let event = ShimEvent {
            event_id: ShimEventID::Syscall,
            event_data: ShimEventData {
                syscall: ShimEventSyscall {
                    syscall_args: *syscall_args,
                },
            },
        };
        unsafe { dst.write(event) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_initShmemComplete(dst: *mut ShimEvent) {
        let event = ShimEvent {
            event_id: ShimEventID::ShmemComplete,
            event_data: ShimEventData { none: () },
        };
        unsafe { dst.write(event) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_initSysCallComplete(
        dst: *mut ShimEvent,
        retval: SysCallReg,
        restartable: bool,
    ) {
        let event = ShimEvent {
            event_id: ShimEventID::SyscallComplete,
            event_data: ShimEventData {
                syscall_complete: ShimEventSyscallComplete {
                    retval,
                    restartable,
                },
            },
        };
        unsafe { dst.write(event) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_initAddThreadParentRes(dst: *mut ShimEvent) {
        let event = ShimEvent {
            event_id: ShimEventID::AddThreadParentRes,
            event_data: ShimEventData { none: () },
        };
        unsafe { dst.write(event) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_initSyscallDoNative(dst: *mut ShimEvent) {
        let event = ShimEvent {
            event_id: ShimEventID::SyscallDoNative,
            event_data: ShimEventData { none: () },
        };
        unsafe { dst.write(event) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_initAddThreadReq(
        dst: *mut ShimEvent,
        ipc_block: *const ShMemBlockSerialized,
    ) {
        let ipc_block = unsafe { ipc_block.as_ref().unwrap() };
        let event = ShimEvent {
            event_id: ShimEventID::AddThreadReq,
            event_data: ShimEventData {
                shmem_blk: ShimEventShmemBlk {
                    serial: *ipc_block,
                    plugin_ptr: PluginPtr::null(),
                    n: 0,
                },
            },
        };
        unsafe { dst.write(event) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_initStart(dst: *mut ShimEvent) {
        let event = ShimEvent {
            event_id: ShimEventID::Start,
            event_data: ShimEventData { none: () },
        };
        unsafe { dst.write(event) };
    }
}
