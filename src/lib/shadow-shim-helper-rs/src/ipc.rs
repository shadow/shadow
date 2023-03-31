use vasi::VirtualAddressSpaceIndependent;
use vasi_sync::scchannel::SelfContainedChannel;

use crate::shim_event::ShimEvent;

/// Manages communication between the Shadow process and the shim library
/// running inside Shadow managed threads.
#[derive(VirtualAddressSpaceIndependent)]
#[repr(C)]
// In the microbenchmarks introduced in
// https://github.com/shadow/shadow/pull/2791, adding a large alignment to
// the mock IPC helped make some measurement artifacts go away by ensuring
// the two channels are on the same cache line.
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

    /// Returns a reference to the "Shadow to Plugin" channel.
    pub fn to_plugin(&self) -> &SelfContainedChannel<ShimEvent> {
        &self.shadow_to_plugin
    }

    /// Returns a reference to the "Plugin to Shadow" channel.
    pub fn to_shadow(&self) -> &SelfContainedChannel<ShimEvent> {
        &self.plugin_to_shadow
    }

    /// Returns a reference to the "Plugin to Shadow" channel.
    pub fn from_plugin(&self) -> &SelfContainedChannel<ShimEvent> {
        &self.plugin_to_shadow
    }

    /// Returns a reference to the "Shadow to Plugin" channel.
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

    use super::*;
    use crate::notnull::notnull_mut;

    #[no_mangle]
    pub unsafe extern "C" fn ipcData_init(ipc_data: *mut IPCData) {
        unsafe { ipc_data.write(IPCData::new()) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn ipcData_destroy(ipc_data: *mut IPCData) {
        unsafe { std::ptr::drop_in_place(notnull_mut(ipc_data)) }
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
            Err(SelfContainedChannelError::WriterIsClosed) => ShimEvent::ProcessDeath,
        };
        unsafe { ev.write(event) };
    }
}
