/// Descriptor type, used e.g. as the `tls` parameter to the `clone` syscall.
//
// I initially tried using `bindgen` to get this type from the linux headers ourselves
// as we do for most of our  types, but ran into trouble because *cbindgen* fails
// to parse the bitfield definition created by *bindgen*. This causes cbindgen to fail
// even if I added this type and the generated bitfield type to the ignore list.
//
// Once we're no longer re-exporting our generated kernel bindings out to C, we can
// either generate this definition ourselves (as currently done for our other types),
// or switch over to linux-raw-sys throughout.
#[allow(non_camel_case_types)]
pub type linux_user_desc = linux_raw_sys::general::user_desc;
