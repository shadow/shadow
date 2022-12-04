use std::path::Path;

use shadow_build_common::ShadowBuildCommon;

fn run_cbindgen(build_common: &ShadowBuildCommon) {
    let base_config = build_common.cbindgen_base_config();
    let crate_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap();

    let mut config = cbindgen::Config {
        sys_includes: vec!["signal.h".into()],
        include_guard: Some("shim_helpers_h".into()),
        after_includes: Some("".into()),
        export: cbindgen::ExportConfig {
            include: vec!["shd_kernel_sigaction".into(), "HostId".into()],
            ..base_config.export.clone()
        },
        ..base_config
    };

    // Force these types to be opaque. This overrides cbindgen's behavior of
    // making any `repr(C)` type non-opaque.
    // https://github.com/eqrion/cbindgen/issues/104
    for t in [
        "ShimShmemHost",
        "ShimShmemHostLock",
        "ShimShmemProcess",
        "ShimShmemThread",
    ] {
        config
            .after_includes
            .as_mut()
            .unwrap()
            .push_str(&format!("typedef struct {t} {t};\n"));
        config.export.exclude.push(t.into());
    }

    cbindgen::Builder::new()
        .with_crate(crate_dir)
        .with_config(config)
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file("../../../build/src/lib/shadow-shim-helper-rs/shim_helper.h");
}

fn main() {
    let deps = system_deps::Config::new().probe().unwrap();
    let build_common = ShadowBuildCommon::new(Path::new("../../.."), Some(deps));

    run_cbindgen(&build_common);

    build_common
        .cc_build()
        .files(&["shadow_sem.c", "shadow_spinlock.c", "shim_shmem.c"])
        .compile("shim_c");

    build_common
        .cc_build()
        .cpp(true)
        .files(&["binary_spinning_sem.cc", "ipc.cc"])
        .compile("shim_cpp");
}
