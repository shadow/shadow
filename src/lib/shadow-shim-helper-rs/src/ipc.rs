use vasi::VirtualAddressSpaceIndependent;
use vasi_sync::scchannel::SelfContainedChannel;

use crate::shim_event::ShimEvent;

#[derive(VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct IPCData {
    shadow_to_plugin: SelfContainedChannel<ShimEvent>,
    plugin_to_shadow: SelfContainedChannel<ShimEvent>,
}

impl IPCData {
    pub fn new() -> Self {
        Self {
            shadow_to_plugin: SelfContainedChannel::new(),
            plugin_to_shadow: SelfContainedChannel::new(),
        }
    }

    pub fn to_plugin(&self) -> &SelfContainedChannel<ShimEvent> {
        &self.shadow_to_plugin
    }

    pub fn to_shadow(&self) -> &SelfContainedChannel<ShimEvent> {
        &self.plugin_to_shadow
    }

    pub fn from_plugin(&self) -> &SelfContainedChannel<ShimEvent> {
        &self.plugin_to_shadow
    }

    pub fn from_shadow(&self) -> &SelfContainedChannel<ShimEvent> {
        &self.shadow_to_plugin
    }
}

impl Default for IPCData {
    fn default() -> Self {
        Self::new()
    }
}

mod export {
    use vasi_sync::scchannel::SelfContainedChannelError;

    use crate::shim_event::{ShimEventData, ShimEventID};

    use super::*;

    #[no_mangle]
    pub unsafe extern "C" fn ipcData_init(ipc_data: *mut IPCData, spin_max: i64) {
        unsafe { ipc_data.write(IPCData::new()) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn ipcData_destroy(ipc_data: *mut IPCData) {
        unsafe { std::ptr::drop_in_place(ipc_data) }
    }

    // After calling this function, the next (or current) call to
    // `shimevent_recvEventFromPlugin` or `shimevent_tryRecvEventFromPlugin` will
    // return SHD_SHIM_EVENT_PROCESS_DEATH.
    //
    // This function is thread-safe, and is safe to call at any point in this APIs
    // state-machine, e.g. even if the last method called was
    // `shimevent_sendEventToShadow`.
    #[no_mangle]
    pub unsafe extern "C" fn ipcData_markPluginExited(ipc_data: *const IPCData) {
        let ipc_data = unsafe { ipc_data.as_ref().unwrap() };
        ipc_data.from_plugin().close_writer()
    }

    #[no_mangle]
    pub unsafe extern "C" fn ipcData_nbytes() -> usize {
        std::mem::size_of::<IPCData>()
    }

    #[no_mangle]
    pub unsafe extern "C" fn shimevent_sendEventToShadow(
        ipc_data: *const IPCData,
        ev: *const ShimEvent,
    ) {
        let ipc_data = unsafe { ipc_data.as_ref().unwrap() };
        let ev = unsafe { ev.as_ref().unwrap() };
        ipc_data.to_shadow().send(*ev)
    }
    #[no_mangle]
    pub unsafe extern "C" fn shimevent_sendEventToPlugin(
        ipc_data: *const IPCData,
        ev: *const ShimEvent,
    ) {
        let ipc_data = unsafe { ipc_data.as_ref().unwrap() };
        let ev = unsafe { ev.as_ref().unwrap() };
        ipc_data.to_plugin().send(*ev)
    }
    #[no_mangle]
    pub unsafe extern "C" fn shimevent_recvEventFromShadow(
        ipc_data: *const IPCData,
        ev: *mut ShimEvent,
        spin: bool,
    ) {
        let ipc_data = unsafe { ipc_data.as_ref().unwrap() };
        let event = ipc_data.from_shadow().receive().unwrap();
        unsafe { ev.write(event) };
    }
    #[no_mangle]
    pub unsafe extern "C" fn shimevent_recvEventFromPlugin(
        ipc_data: *const IPCData,
        ev: *mut ShimEvent,
    ) {
        let ipc_data = unsafe { ipc_data.as_ref().unwrap() };
        let event = match ipc_data.from_plugin().receive() {
            Ok(e) => e,
            Err(SelfContainedChannelError::WriterIsClosed) => ShimEvent {
                event_id: ShimEventID::ProcessDeath,
                event_data: ShimEventData { none: () },
            },
        };
        unsafe { ev.write(event) };
    }
}
