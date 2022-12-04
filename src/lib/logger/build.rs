use std::{
    env,
    path::{Path, PathBuf},
};

use shadow_build_common::ShadowBuildCommon;

fn run_bindgen(build_common: &ShadowBuildCommon) {
    let bindings = build_common
        .bindgen_builder()
        .header("logger.h")
        .allowlist_function("logger_.*")
        // Finish the builder and generate the bindings.
        .generate()
        // Unwrap the Result and panic on failure.
        .expect("Unable to generate bindings");
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .unwrap();
}

fn main() {
    let build_common = ShadowBuildCommon::new(Path::new("../../.."), None);

    run_bindgen(&build_common);

    build_common
        .cc_build()
        .file("log_level.c")
        .file("logger.c")
        .compile("logger_c");
}
