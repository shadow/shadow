use std::{env, path::PathBuf};

use shadow_build_common::{ShadowBuildCommon, CBindgenExt};

fn run_bindgen() {
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());

    let header_contents = "
            #include <stdint.h>
            #include <stddef.h>
            #include <linux/signal.h>
            #include <sys/syscall.h>
        ";

    // Constants only (allowlist_var).
    // We re-export these with a wildcard in the module to avoid having
    // to enumerate e.g. all the syscall and signal numbers in the module definition.
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
        .allowlist_type("siginfo_t")
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file(out_path.join("types.rs"))
        .expect("Couldn't write bindings!");
}

fn run_cbindgen(build_common: &ShadowBuildCommon) {
    let base_config = build_common.cbindgen_base_config();
    let crate_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap();

    let mut config = cbindgen::Config {
        include_guard: Some("linux_kernel_types_h".into()),
        export: cbindgen::ExportConfig {
            include: vec![
                "linux_sigset_t".into(),
            ],
            ..base_config.export.clone()
        },
        ..base_config
    };
    config.add_opaque_types(&["linux_sigaction"]);

    cbindgen::Builder::new()
        .with_crate(crate_dir)
        .with_config(config)
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file("../../../build/src/lib/linux-kernel-types/linux_kernel_types.h");
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
