use std::{env, path::PathBuf};

use shadow_build_common::ShadowBuildCommon;

fn run_bindgen() {
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());

    // Careful here to include *kernel* headers, and *not* glibc headers,
    // which may have different definitions. A notable example is
    // `<linux/signal.h>`  vs `<signal.h>`.
    let header_contents = "
            #include <stdint.h>
            #include <stddef.h>
            #include <linux/signal.h>
            #include <sys/syscall.h>
        ";

    // Constants only (allowlist_var).
    // Originally I kept this separate to support re-exporting this module publicly
    // without having to enumerate the constants, but that turns out not to work well.
    // TODO: consider merging with other bindings below.
    bindgen::Builder::default()
        .parse_callbacks(Box::new(bindgen::CargoCallbacks))
        // Use ::core instead of ::std, since this crate is no_std.
        .use_core()
        .header_contents("kernel_defs.h", header_contents)
        // syscall numbers
        .allowlist_var("SYS_.*")
        .allowlist_var("SIGRTMIN")
        .allowlist_var("SIGRTMAX")
        .allowlist_var("SS_AUTODISARM")
        // signal numbers
        .allowlist_var("SIG.*")
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file(out_path.join("constants.rs"))
        .expect("Couldn't write bindings!");

    bindgen::Builder::default()
        .parse_callbacks(Box::new(bindgen::CargoCallbacks))
        // Use ::core instead of ::std, since this crate is no_std.
        .use_core()
        .header_contents("kernel_defs.h", header_contents)
        .allowlist_type("sigset_t")
        .allowlist_type("sigaction")
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file(out_path.join("types.rs"))
        .expect("Couldn't write bindings!");
}

fn run_cbindgen(build_common: &ShadowBuildCommon) {
    let base_config = build_common.cbindgen_base_config();
    let crate_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap();

    let config = cbindgen::Config {
        include_guard: Some("linux_kernel_types_h".into()),
        export: cbindgen::ExportConfig {
            include: vec!["shd_kernel_sigaction".into()],
            ..base_config.export.clone()
        },
        ..base_config
    };

    cbindgen::Builder::new()
        .with_crate(crate_dir)
        .with_config(config)
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file("../../../build/src/lib/linux-api/linux-api.h");
}

fn main() {
    let build_common =
        shadow_build_common::ShadowBuildCommon::new(std::path::Path::new("../../.."), None);
    run_cbindgen(&build_common);

    // We *don't* use ShadowBuildCommon here, since we don't want the defaults used
    // in other places. Since we're generating bindings for kernel definitions, we need
    // to be careful to avoid conflicts with libc definitions.
    run_bindgen();
}
