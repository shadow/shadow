use shadow_build_common::{CBindgenExt, ShadowBuildCommon};

fn run_cbindgen(build_common: &ShadowBuildCommon) {
    let base_config = build_common.cbindgen_base_config();
    let crate_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap();

    let mut config = cbindgen::Config {
        // This is the only way to prevent including <stdlib.h>, which
        // conflicts with kernel headers.
        no_includes: true,
        include_guard: Some("linux_kernel_types_h".into()),
        export: cbindgen::ExportConfig {
            include: vec!["linux_sigaction".into(), "linux_siginfo_t".into()],
            exclude: vec!["Errno".into(), "timespec".into(), "timeval".into()],
            ..base_config.export.clone()
        },
        ..base_config
    };
    config.add_after_includes("#include \"lib/linux-api/bindings-wrapper.h\"\n");
    config.add_after_includes("typedef int32_t LinuxSigActionFlags;\n");

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
}
