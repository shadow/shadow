mod bindings {
    #![allow(non_upper_case_globals)]
    #![allow(non_camel_case_types)]
    #![allow(non_snake_case)]
    // https://github.com/rust-lang/rust/issues/66220
    #![allow(improper_ctypes)]
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

// For legacy compatibility.
// TODO: wrap or replace with an idiomatic rust api.
pub use bindings::*;
