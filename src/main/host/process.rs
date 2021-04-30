use crate::cshadow;

pub struct Process {
    // Placeholder. We don't actually use this yet.
    _cprocess: *mut cshadow::Process,
}

impl Process {
    /// For now, this should only be called via Worker, to borrow the current
    /// Process. This will ensure there is only one reference to a given Process
    /// in Rust.
    ///
    /// SAFETY: `p` must point to a valid c::Process, to which this Process will
    /// have exclusive access over its lifetime. `p` must outlive the returned object.
    pub unsafe fn borrow_from_c(p: *mut cshadow::Process) -> Self {
        assert!(!p.is_null());
        Process { _cprocess: p }
    }
}
