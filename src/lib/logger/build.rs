use std::{
    env,
    path::{Path, PathBuf},
};

use shadow_build_common::{Compiler, ShadowBuildCommon};

fn run_bindgen(build_common: &ShadowBuildCommon) {
    let bindings = build_common
        .bindgen_builder()
        .header("logger.h")
        .allowlist_function("logger_.*")
        // the logger's C functions may call rust functions that do unwind, so I think we need this
        .override_abi(bindgen::Abi::CUnwind, ".*")
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
        .cc_build(Compiler::C)
        .file("log_level.c")
        .file("logger.c")
        .compile("logger_c");
}
