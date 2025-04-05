use core::{ffi::c_ulong, ptr};

use linux_api::auxvec::AuxVecTag;

/// Analogous to libc's `getauxval(3)`, but reads from `/proc/self/auxv` (see
/// `proc(5)`) instead of using libc.
fn getauxval(tag: AuxVecTag) -> Option<c_ulong> {
    let r = rustix::fs::open(
        "/proc/self/auxv",
        rustix::fs::OFlags::RDONLY,
        rustix::fs::Mode::empty(),
    );
    let Ok(auxv_file) = r else {
        log::warn!("Couldn't open /proc/self/auxv: {r:?}");
        return None;
    };
    // The auxv data are (tag, value) pairs of core::ffi::c_ulong.
    // Experimentally, on my system this is 368 bytes (`wc -c /proc/self/auxv`).
    // Leave some room for future growth.
    let mut auxv_data = [0u8; 368 * 2];
    let r = rustix::io::read(&auxv_file, &mut auxv_data);
    let Ok(bytes_read) = r else {
        log::warn!("Couldn't read /proc/self/auxv: {r:?}");
        return None;
    };
    // Intentionally shadow array with a slice of the initd part.
    let auxv_data = &auxv_data[..bytes_read];
    // We should have gotten it all in one read, so we should get 0 bytes here.
    let r = rustix::io::read(auxv_file, &mut [0; 1]);
    if r != Ok(0) {
        log::warn!("Expected EOF reading /proc/self/auxv. Instead got: {r:?}");
        return None;
    };
    let mut tag_val_iter = auxv_data
        .chunks_exact(2 * size_of::<c_ulong>())
        .map(|chunk| {
            let tag = c_ulong::from_ne_bytes(chunk[..size_of::<c_ulong>()].try_into().unwrap());
            let value = c_ulong::from_ne_bytes(chunk[size_of::<c_ulong>()..].try_into().unwrap());
            (tag, value)
        });
    tag_val_iter
        .find(|(this_tag, _val)| *this_tag == u64::from(tag))
        .map(|(_tag, value)| value)
}

/// Returns a pointer to the `AT_RANDOM` data as provided in the auxiliary
/// vector.  We locate this data via `/proc/self/auxv` (see proc(5)). For more
/// about this data itself see `getauxval(3)`.
fn get_auxvec_random() -> *mut [u8; 16] {
    let Some(val) = getauxval(AuxVecTag::AT_RANDOM) else {
        log::warn!("Couldn't find AT_RANDOM");
        return ptr::null_mut();
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
/// * The data must be writable. (This isn't explicitly guaranteed by the Linux
///   docs, but seems to be the case).
/// * Overwriting this process-global value must not violate safety requirements
///   of other code running in the same address-space, such as they dynamic
///   linker/loader and other dynamically linked libraries. The best way to ensure
///   this is to call this before other such code gets a chance to run.
///
/// Because this data is a process-wide global initialized by the kernel, code
/// outside of this library may access it. The above safety requirements likely
/// apply to that code as well. Additionally, changing this data after some code
/// has already read it might violate assumptions in that code.
pub unsafe fn reinit_auxvec_random(data: &[u8; 16]) {
    let auxv = get_auxvec_random();
    if auxv.is_null() {
        log::warn!(
            "Couldn't find auxvec AT_RANDOM to overwrite. May impact simulation determinism."
        );
    } else {
        unsafe { get_auxvec_random().write(*data) }
    }
}

#[cfg(test)]
mod test {
    use linux_api::auxvec::AuxVecTag;

    #[test]
    // Can't call libc::getauxval from miri
    #[cfg(not(miri))]
    fn test_getauxvec() {
        // Test consistency with libc
        assert_eq!(super::getauxval(AuxVecTag::AT_RANDOM).unwrap(), unsafe {
            libc::getauxval(libc::AT_RANDOM)
        });
        for value in 0..100 {
            if value == libc::AT_HWCAP || value == libc::AT_HWCAP2 {
                // libc handles AT_HWCAP and AT_HWCAP2 differently
                continue;
            }
            if let Ok(tag) = AuxVecTag::try_from(value) {
                assert_eq!(
                    super::getauxval(tag).unwrap_or(0),
                    unsafe { libc::getauxval(value) },
                    "value mismatch for type {tag:?}",
                );
            }
        }
    }
}
