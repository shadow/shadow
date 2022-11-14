use std::path::Path;

use shadow_build_common::ShadowBuildCommon;

fn run_cbindgen(build_common: &ShadowBuildCommon) {
    let base_config = build_common.cbindgen_base_config();
    let config = cbindgen::Config {
        include_guard: Some("tsc_rs_h".into()),
        ..base_config
    };
    let crate_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap();
    cbindgen::Builder::new()
        .with_crate(crate_dir.clone())
        .with_config(config)
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file("../../../build/src/lib/tsc/tsc_rs.h");
}

fn main() {
    let build_common = ShadowBuildCommon::new(&Path::new("../../.."), None);
    run_cbindgen(&build_common);

    build_common.cc_build().file("tsc.c").compile("tsc_c");
}
