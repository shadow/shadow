use std::collections::HashMap;
use std::env;
use std::path::{Path, PathBuf};

use shadow_build_common::ShadowBuildCommon;

fn run_bindgen(build_common: &ShadowBuildCommon) {
    let bindings = build_common
        .bindgen_builder()
        .header("shim.h")
        .allowlist_function("shim_.*")
        .header("shim_api_c.h")
        .allowlist_function("shimc_.*")
        // get libc types from libc crate
        .blocklist_type("addrinfo")
        .raw_line("use libc::addrinfo;")
        .blocklist_type("ifaddrs")
        .raw_line("use libc::ifaddrs;")
        .blocklist_type("socklen_t")
        .blocklist_type("__socklen_t")
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

    let config = cbindgen::Config {
        sys_includes: vec![
            "ifaddrs.h".into(),
            "netdb.h".into(),
            "stdarg.h".into(),
            "sys/socket.h".into(),
            "sys/types.h".into(),
            "netdb.h".into(),
        ],
        after_includes: {
            let mut v = base_config.after_includes.clone().unwrap_or_default();
            // We have to manually create the vararg declaration.
            v.push_str("long shim_api_syscall(long n, ...);\n");
            Some(v)
        },
        export: cbindgen::ExportConfig {
            rename: HashMap::from([
                ("addrinfo".into(), "struct addrinfo".into()),
                ("ifaddrs".into(), "struct ifaddrs".into()),
            ]),
            exclude: vec![
                // Manual declaration above
                "shim_api_syscall".into(),
            ],
            ..base_config.export.clone()
        },
        include_guard: Some("shim_shim_api_h".into()),
        ..base_config
    };

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
            "shim_logger.c",
            "shim_rdtsc.c",
            "shim_seccomp.c",
            "shim_signals.c",
            "shim_sys.c",
            "shim_syscall.c",
            "shim_tls.c",
        ])
        .compile("shim_c");
}
