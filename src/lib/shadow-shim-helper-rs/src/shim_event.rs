use shadow_shmem::allocator::ShMemBlockSerialized;
use vasi::VirtualAddressSpaceIndependent;

use crate::syscall_types::{PluginPtr, SysCallArgs, SysCallReg};

#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub enum ShimEventID {
    Null = 0,
    Start = 1,
    // The whole process has died.
    // We inject this event to trigger cleanup after we've detected that the
    // native process has died.
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

#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct ShimEventSyscall {
    pub syscall_args: SysCallArgs,
}

#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct ShimEventSyscallComplete {
    pub retval: SysCallReg,
    // Whether the syscall is eligible to be restarted. Only applicable
    // when retval is -EINTR. See signal(7).
    pub restartable: bool,
}

#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct ShimEventShmemBlk {
    pub serial: ShMemBlockSerialized,
    pub plugin_ptr: PluginPtr,
    pub n: usize,
}

#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct ShimEventAddThreadReq {
    pub ipc_block: ShMemBlockSerialized,
}

#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub enum ShimEvent {
    Null,
    Start,
    // The whole process has died.
    // We inject this event to trigger cleanup after we've detected that the
    // native process has died.
    ProcessDeath,
    Syscall(ShimEventSyscall),
    SyscallComplete(ShimEventSyscallComplete),
    SyscallDoNative,
    CloneReq(ShimEventShmemBlk),
    CloneStringReq(ShimEventShmemBlk),
    ShmemComplete,
    WriteReq(ShimEventShmemBlk),
    Block,
    AddThreadReq(ShimEventAddThreadReq),
    AddThreadParentRes,
}

mod export {
    use super::*;

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_getId(event: *const ShimEvent) -> ShimEventID {
        let event = unsafe { event.as_ref().unwrap() };
        match event {
            ShimEvent::Null => ShimEventID::Null,
            ShimEvent::Start => ShimEventID::Start,
            ShimEvent::ProcessDeath => ShimEventID::ProcessDeath,
            ShimEvent::Syscall(_) => ShimEventID::Syscall,
            ShimEvent::SyscallComplete(_) => ShimEventID::SyscallComplete,
            ShimEvent::SyscallDoNative => ShimEventID::SyscallDoNative,
            ShimEvent::CloneReq(_) => ShimEventID::CloneReq,
            ShimEvent::CloneStringReq(_) => ShimEventID::CloneStringReq,
            ShimEvent::ShmemComplete => ShimEventID::ShmemComplete,
            ShimEvent::WriteReq(_) => ShimEventID::WriteReq,
            ShimEvent::Block => ShimEventID::Block,
            ShimEvent::AddThreadReq(_) => ShimEventID::AddThreadReq,
            ShimEvent::AddThreadParentRes => ShimEventID::AddThreadParentRes,
        }
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
    pub unsafe extern "C" fn shimevent_getShmemBlkData(
        event: *const ShimEvent,
    ) -> *const ShimEventShmemBlk {
        let event = unsafe { event.as_ref().unwrap() };
        match event {
            ShimEvent::CloneReq(blk) => blk,
            ShimEvent::CloneStringReq(blk) => blk,
            ShimEvent::WriteReq(blk) => blk,
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
    pub unsafe extern "C" fn shimevent_initShmemComplete(dst: *mut ShimEvent) {
        let event = ShimEvent::ShmemComplete;
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
