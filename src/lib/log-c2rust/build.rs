use std::path::Path;

use shadow_build_common::{Compiler, ShadowBuildCommon};

// Generate rustlogger.h, exposing this crate's implementation of a rustlogger
// to this crate's C code.
fn run_cbindgen(build_common: &ShadowBuildCommon) {
    let base_config = build_common.cbindgen_base_config();
    let crate_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap();

    let mut config = base_config.clone();
    config.include_guard = Some("log_c2rust_rustlogger_h".into());
    config.includes = vec!["lib/logger/logger.h".into()];
    config.export = cbindgen::ExportConfig {
        // Don't re-export LogLevel; we get the definition from logger.h
        exclude: vec!["LogLevel".into()],
        ..base_config.export.clone()
    };

    cbindgen::Builder::new()
        .with_crate(crate_dir)
        .with_config(config)
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file("../../../build/src/lib/log-c2rust/rustlogger.h");
}

fn main() {
    let build_common = ShadowBuildCommon::new(Path::new("../../.."), None);

    run_cbindgen(&build_common);

    // Our Rust code doesn't call *into* this library. It implements the C wrapper code
    // that client code calls.
    build_common
        .cc_build(Compiler::C)
        .files(&["log-c2rust.c"])
        .compile("log_c2rust");
}
