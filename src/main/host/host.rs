use crate::cshadow;

pub struct Host {
    chost: *mut cshadow::Host,
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
        Host { chost: p }
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
}
