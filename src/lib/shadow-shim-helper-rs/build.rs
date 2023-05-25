use std::path::Path;

use shadow_build_common::{CBindgenExt, ShadowBuildCommon};

fn run_cbindgen(build_common: &ShadowBuildCommon) {
    let base_config = build_common.cbindgen_base_config();
    let crate_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap();

    let mut config = cbindgen::Config {
        include_guard: Some("shim_helpers_h".into()),
        export: cbindgen::ExportConfig {
            include: vec![
                "shd_kernel_sigaction".into(),
                "ShimEventToShadow".into(),
                "ShimEventToShim".into(),
                "HostId".into(),
                "SysCallArgs".into(),
                "SysCallReg".into(),
                "ManagedPhysicalMemoryAddr".into(),
            ],
            // we provide our own definition for `UntypedForeignPtr` above
            exclude: vec!["UntypedForeignPtr".into()],
            ..base_config.export.clone()
        },
        ..base_config
    };

    config.add_includes_as_raw(&[
        "lib/linux-api/linux-api.h",
        "lib/logger/logger.h",
        "lib/shmem/shmem_allocator.h",
    ]);

    config.add_opaque_types(&[
        "ShimShmemManager",
        "ShimShmemHost",
        "ShimShmemHostLock",
        "ShimShmemProcess",
        "ShimShmemThread",
        "IPCData",
    ]);

    // We typedef `UntypedForeignPtr` to `ForeignPtr<()>` in rust, but cbindgen won't generate
    // bindings for `ForeignPtr<()>` so we need to write our own. This must have the same size,
    // alignment, non-zst fields, and field order as `ForeignPtr<()>`.
    config.add_after_includes(concat!(
        "typedef struct CompatUntypedForeignPtr {\n",
        "    uintptr_t val;\n",
        "} UntypedForeignPtr;\n"
    ));

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
        .files(&["shadow_sem.c", "shadow_spinlock.c"])
        .compile("shim_c");
}
