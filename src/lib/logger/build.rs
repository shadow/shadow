use std::path::Path;

use shadow_build_common::ShadowBuildCommon;

#[cfg(feature = "bindings")]
fn run_bindgen(build_common: &ShadowBuildCommon) {
    let bindings = build_common
        .bindgen_builder()
        .header("logger.h")
        .allowlist_function("logger_.*")
        // Finish the builder and generate the bindings.
        .generate()
        // Unwrap the Result and panic on failure.
        .expect("Unable to generate bindings");
    bindings.write_to_file("src/bindings.rs").unwrap();
}

fn main() {
    let build_common = ShadowBuildCommon::new(&Path::new("../../.."), None);

    #[cfg(feature = "bindings")]
    run_bindgen(&build_common);

    build_common
        .cc_build()
        .file("log_level.c")
        .file("logger.c")
        .compile("logger_c");
}
