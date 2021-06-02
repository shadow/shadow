use once_cell::unsync::OnceCell;
use std::net::IpAddr;
use std::sync::Arc;

use crate::cshadow;

#[derive(Debug, PartialEq, Eq, Hash, Copy, Clone)]
pub struct HostId(u32);

impl From<u32> for HostId {
    fn from(val: u32) -> Self {
        HostId(val)
    }
}

impl From<HostId> for u32 {
    fn from(val: HostId) -> Self {
        val.0
    }
}

/// Immutable information about the Host.
#[derive(Debug, Clone)]
pub struct HostInfo {
    pub id: HostId,
    pub name: String,
    pub default_ip: IpAddr,
    pub log_level: Option<log::LevelFilter>,
}

/// A simulated Host.
///
/// This is currently an ephemeral proxy object a C Host (cshadow::Host).
/// Eventually cshadow::Host's contents and functionality will be migrated into
/// there though, and this will become the "real" Host object.
pub struct Host {
    chost: *mut cshadow::Host,

    // Store immutable info in an Arc, that we can safely clone into the Worker
    // and ShadowLogger.
    //
    // Created on-demand for now, to avoid building unnecessarily for ephemeral
    // Host objects.
    //
    // TODO: Consider caching a couple copies that we can "lend" out by value,
    // without having to do any atomic operation?
    info: OnceCell<Arc<HostInfo>>,
}

impl Host {
    /// For now, this should only be called via Worker, to borrow the current
    /// Host. This will ensure there is only one reference to a given Host
    /// in Rust.
    ///
    /// SAFETY: `p` must point to a valid c::Host, to which this Host will
    /// have exclusive access over its lifetime. `p` must outlive the returned object.
    pub unsafe fn borrow_from_c(p: *mut cshadow::Host) -> Self {
        assert!(!p.is_null());
        Host {
            chost: p,
            info: OnceCell::new(),
        }
    }

    /// Information about the Host. Made available as an Arc for cheap cloning
    /// into, e.g. Worker and ShadowLogger. When there's no need to clone the
    /// Arc, generally prefer the top-level `Host` methods for accessing this
    /// information, which are likely to be more stable.
    pub fn info(&self) -> &Arc<HostInfo> {
        self.info.get_or_init(|| {
            Arc::new(HostInfo {
                id: self.id(),
                name: self.name().into(),
                default_ip: self.default_ip(),
                log_level: self.log_level(),
            })
        })
    }

    pub fn id(&self) -> HostId {
        HostId(unsafe { cshadow::host_getID(self.chost) })
    }

    pub fn name(&self) -> &str {
        let slice = unsafe { std::ffi::CStr::from_ptr(cshadow::host_getName(self.chost)) };
        slice.to_str().unwrap()
    }

    pub fn default_ip(&self) -> std::net::IpAddr {
        use std::net;
        let addr = unsafe { cshadow::host_getDefaultIP(self.chost) };
        net::IpAddr::V4(net::Ipv4Addr::from(addr.to_le_bytes()))
    }

    pub fn log_level(&self) -> Option<log::LevelFilter> {
        let level = unsafe { cshadow::host_getLogLevel(self.chost) };
        crate::core::logger::log_wrapper::c_to_rust_log_level(level).map(|l| l.to_level_filter())
    }

    pub fn chost(&self) -> *mut cshadow::Host {
        self.chost
    }
}
