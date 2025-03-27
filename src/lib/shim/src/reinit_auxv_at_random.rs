/// Returns a pointer to the `AT_RANDOM` data as provided in the auxiliary vector.
/// See `getauxval(3)`.
fn get_at_random() -> *mut [u8; 16] {
    // XXX: don't use libc
    // SAFETY: We can only assume that libc gives us a valid pointer here.
    unsafe { libc::getauxval(libc::AT_RANDOM) as *mut [u8; 16] }
}

/// (Re)initialize the 16 random "`AT_RANDOM`" bytes that the kernel provides
/// via the auxiliary vector.  See `getauxval(3)`
///
/// # Safety
///
/// There must be no concurrent access to the `AT_RANDOM` data, including:
///
/// * There must be no live rust reference to that data.
/// * This function must not be called in parallel, e.g. from another thread.
pub unsafe fn reinit_auxv_at_random(data: &[u8; 16]) {
    let auxv = get_at_random();
    if auxv.is_null() {
        log::warn!("Couldn't find auxvec AT_RANDOM to overwrite");
    } else {
        unsafe { get_at_random().write(*data) }
    }
}

mod export {
    /// (Re)initialize the 16 random "`AT_RANDOM`" bytes that the kernel provides
    /// via the auxiliary vector.  See `getauxval(3)`
    ///
    /// # Safety
    ///
    /// There must be no concurrent access to the `AT_RANDOM` data, including:
    ///
    /// * There must be no live rust reference to that data.
    /// * This function must not be called in parallel, e.g. from another thread.
    #[unsafe(no_mangle)]
    pub unsafe extern "C-unwind" fn reinit_auxv_at_random(data: &[u8; 16]) {
        unsafe { super::reinit_auxv_at_random(data) }
    }
}
