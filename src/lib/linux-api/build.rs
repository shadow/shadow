#[cfg(feature = "c_bindings")]
use shadow_build_common::ShadowBuildCommon;

#[cfg(feature = "c_bindings")]
fn run_cbindgen(build_common: &ShadowBuildCommon) {
    let base_config = build_common.cbindgen_base_config();
    let crate_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap();

    let mut config = base_config.clone();
    config.include_guard = Some("linux_kernel_types_h".into());
    config.export = cbindgen::ExportConfig {
        include: vec![
            "linux_sigaction".into(),
            "linux_siginfo_t".into(),
            "linux___kernel_mode_t".into(),
            "linux_stack_t".into(),
        ],
        // Not sure why cbindgen tries to wrap this. The bindings it generates
        // are broken though because the individual Errno values are translated
        // as e.g. `bindings_LINUX_EINVAL` instead of `LINUX_EINVAL`.
        exclude: vec!["Errno".into()],
        ..base_config.export.clone()
    };

    cbindgen::Builder::new()
        .with_crate(crate_dir)
        .with_config(config)
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file("../../../build/src/lib/linux-api/linux-api.h");
}

fn main() {
    #[cfg(feature = "c_bindings")]
    {
        let build_common =
            shadow_build_common::ShadowBuildCommon::new(std::path::Path::new("../../.."), None);
        run_cbindgen(&build_common);
    }
}
