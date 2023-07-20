use vasi::VirtualAddressSpaceIndependent;
use vasi_sync::scchannel::SelfContainedChannel;

use crate::shim_event::{ShimEventToShadow, ShimEventToShim};

/// Manages communication between the Shadow process and the shim library
/// running inside Shadow managed threads.
#[derive(VirtualAddressSpaceIndependent)]
#[repr(C)]
// In the microbenchmarks introduced in
// https://github.com/shadow/shadow/pull/2791, adding a large alignment to
// the mock IPC helped make some measurement artifacts go away by ensuring
// the two channels are on the same cache line.
pub struct IPCData {
    shadow_to_plugin: SelfContainedChannel<ShimEventToShim>,
    plugin_to_shadow: SelfContainedChannel<ShimEventToShadow>,
}

impl IPCData {
    pub fn new() -> Self {
        Self {
            shadow_to_plugin: SelfContainedChannel::new(),
            plugin_to_shadow: SelfContainedChannel::new(),
        }
    }

    /// Returns a reference to the "Shadow to Plugin" channel.
    pub fn to_plugin(&self) -> &SelfContainedChannel<ShimEventToShim> {
        &self.shadow_to_plugin
    }

    /// Returns a reference to the "Plugin to Shadow" channel.
    pub fn to_shadow(&self) -> &SelfContainedChannel<ShimEventToShadow> {
        &self.plugin_to_shadow
    }

    /// Returns a reference to the "Plugin to Shadow" channel.
    pub fn from_plugin(&self) -> &SelfContainedChannel<ShimEventToShadow> {
        &self.plugin_to_shadow
    }

    /// Returns a reference to the "Shadow to Plugin" channel.
    pub fn from_shadow(&self) -> &SelfContainedChannel<ShimEventToShim> {
        &self.shadow_to_plugin
    }
}

impl Default for IPCData {
    fn default() -> Self {
        Self::new()
    }
}
