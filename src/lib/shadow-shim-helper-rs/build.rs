use std::path::Path;

use shadow_build_common::{CBindgenExt, ShadowBuildCommon};

fn run_cbindgen(build_common: &ShadowBuildCommon) {
    let base_config = build_common.cbindgen_base_config();
    let crate_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap();

    let mut config = base_config.clone();
    config.sys_includes = vec!["signal.h".into()];
    config.include_guard = Some("shim_helpers_h".into());
    config.includes = vec![
        "lib/linux-api/linux-api.h".into(),
        "lib/logger/logger.h".into(),
    ];
    // We typedef `UntypedForeignPtr` to `ForeignPtr<()>` in rust, but cbindgen won't generate
    // bindings for `ForeignPtr<()>` so we need to write our own. This must have the same size,
    // alignment, non-zst fields, and field order as `ForeignPtr<()>`.
    config.after_includes = Some(
        "typedef struct CompatUntypedForeignPtr {\n    \
                 uintptr_t val;\n\
             } UntypedForeignPtr;\n"
            .into(),
    );
    config.export = cbindgen::ExportConfig {
        include: vec![
            "shd_kernel_sigaction".into(),
            "HostId".into(),
            "SyscallArgs".into(),
            "SyscallReg".into(),
            "ManagedPhysicalMemoryAddr".into(),
        ],
        // we provide our own definition for `UntypedForeignPtr` above
        exclude: vec!["UntypedForeignPtr".into()],
        ..base_config.export.clone()
    };

    config.add_opaque_types(&[
        "ShimShmemManager",
        "ShimShmemHost",
        "ShimShmemHostLock",
        "ShimShmemProcess",
        "ShimShmemThread",
        "IPCData",
    ]);

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
}
