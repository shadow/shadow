/// Returns a pointer to the `AT_RANDOM` data as provided in the auxiliary vector.
/// See `getauxval(3)`.
fn get_at_random() -> *mut [u8; 16] {
    let r = rustix::fs::open(
        "/proc/self/auxv",
        rustix::fs::OFlags::RDONLY,
        rustix::fs::Mode::empty(),
    );
    let Ok(auxv_file) = r else {
        log::warn!("Couldn't read /proc/self/auxv: {r:?}");
        return core::ptr::null_mut();
    };
    // The auxv data are (tag, value) pairs of core::ffi::c_ulong.
    // Experimentally, on my system this is 368 bytes (`wc -c /proc/self/auxv`).
    // Leave some room for future growth.
    let mut auxv_data = [0u8; 368 * 2];
    let r = rustix::io::read(&auxv_file, &mut auxv_data);
    if r.is_err() {
        log::warn!("Couldn't read /proc/self/auxv: {r:?}");
        return core::ptr::null_mut();
    }
    // We should have gotten it all in one read, so we should get 0 bytes here.
    let r = rustix::io::read(auxv_file, &mut [0; 1]);
    if r != Ok(0) {
        log::warn!("Unexpectedly not at EOF of /proc/self/auxv. Read result: {r:?}");
        return core::ptr::null_mut();
    };
    let mut tag_val_iter = auxv_data
        .chunks_exact(2 * core::mem::size_of::<core::ffi::c_ulong>())
        .map(|chunk| {
            let tag = core::ffi::c_ulong::from_le_bytes(
                chunk[..core::mem::size_of::<core::ffi::c_ulong>()]
                    .try_into()
                    .unwrap(),
            );
            let value = core::ffi::c_ulong::from_le_bytes(
                chunk[core::mem::size_of::<core::ffi::c_ulong>()..]
                    .try_into()
                    .unwrap(),
            );
            (tag, value)
        });
    let Some((_tag, val)) =
        tag_val_iter.find(|(tag, _val)| *tag == u64::from(linux_api::auxvec::AuxVecTag::AT_RANDOM))
    else {
        log::warn!("Couldn't find AT_RANDOM");
        return core::ptr::null_mut();
    };
    val as *mut [u8; 16]
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
