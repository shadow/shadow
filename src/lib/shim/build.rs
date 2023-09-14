use std::collections::HashMap;
use std::env;
use std::path::{Path, PathBuf};

use shadow_build_common::ShadowBuildCommon;

fn run_bindgen(build_common: &ShadowBuildCommon) {
    let bindings = build_common
        .bindgen_builder()
        .use_core()
        .header("shim.h")
        .allowlist_function("shim_.*")
        .allowlist_function("_shim_.*")
        .header("shim_api_c.h")
        .allowlist_function("shimc_.*")
        .header("shim_sys.h")
        .allowlist_function("shim_sys_get_simtime_nanos")
        .header("shim_syscall.h")
        .header("shim_tls.h")
        // get libc types from libc crate
        .blocklist_type("addrinfo")
        .raw_line("use libc::addrinfo;")
        .blocklist_type("ifaddrs")
        .raw_line("use libc::ifaddrs;")
        .blocklist_type("socklen_t")
        .blocklist_type("__socklen_t")
        // Import instead
        .blocklist_type("ShimShmem.*")
        .raw_line("use shadow_shim_helper_rs::shim_shmem::*;")
        .raw_line("use shadow_shim_helper_rs::shim_shmem::export::*;")
        .blocklist_type("TlsOneThreadStorageAllocation")
        .raw_line("use crate::tls::TlsOneThreadStorageAllocation;")
        // Finish the builder and generate the bindings.
        .generate()
        // Unwrap the Result and panic on failure.
        .expect("Unable to generate bindings");
    // Write the bindings to the $OUT_DIR/bindings.rs file.
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings!");
}

fn run_cbindgen(build_common: &ShadowBuildCommon) {
    let base_config = build_common.cbindgen_base_config();
    let crate_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap();

    let mut config = base_config.clone();
    config.sys_includes = vec![
        "ifaddrs.h".into(),
        "netdb.h".into(),
        "stdarg.h".into(),
        "sys/socket.h".into(),
        "sys/types.h".into(),
        "netdb.h".into(),
    ];
    config.includes = vec![
        "lib/log-c2rust/rustlogger.h".into(),
        "lib/shadow-shim-helper-rs/shim_helper.h".into(),
    ];
    config.after_includes = {
        let mut v = base_config.after_includes.clone().unwrap_or_default();
        // We have to manually create the vararg declaration.
        v.push_str("long shim_api_syscall(long n, ...);\n");
        // We have to define the ALIGNED macro to support aligned structs.
        v.push_str("#define ALIGNED(n) __attribute__((aligned(n)))\n");
        Some(v)
    };
    config.export = cbindgen::ExportConfig {
        rename: HashMap::from([
            ("addrinfo".into(), "struct addrinfo".into()),
            ("ifaddrs".into(), "struct ifaddrs".into()),
        ]),
        exclude: vec![
            // Manual declaration above
            "shim_api_syscall".into(),
        ],
        include: vec!["TlsOneThreadStorageAllocation".into()],
        ..base_config.export.clone()
    };
    config.layout = cbindgen::LayoutConfig {
        aligned_n: Some("ALIGNED".into()),
        ..base_config.layout.clone()
    };
    config.include_guard = Some("shim_shim_api_h".into());

    cbindgen::Builder::new()
        .with_crate(crate_dir)
        .with_config(config)
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file("../../../build/src/lib/shim/shim_api.h");
}

fn main() {
    let deps = system_deps::Config::new().probe().unwrap();
    let build_common = ShadowBuildCommon::new(Path::new("../../.."), Some(deps));

    // The C bindings should be generated first since cbindgen doesn't require
    // the Rust code to be valid, whereas bindgen does require the C code to be
    // valid. If the C bindings are no longer correct, but the Rust bindings are
    // generated first, then there will be no way to correct the C bindings
    // since the Rust binding generation will always fail before the C bindings
    // can be corrected.
    run_cbindgen(&build_common);
    run_bindgen(&build_common);

    build_common
        .cc_build()
        .files(&[
            "patch_vdso.c",
            "shim.c",
            "shim_api_addrinfo.c",
            "shim_api_ifaddrs.c",
            "shim_api_syscall.c",
            "shim_rdtsc.c",
            "shim_seccomp.c",
            "shim_sys.c",
            "shim_syscall.c",
            "shim_tls.c",
        ])
        .compile("shim_c");
}
