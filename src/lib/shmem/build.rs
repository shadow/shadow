use std::{
    env,
    path::{Path, PathBuf},
};

use shadow_build_common::ShadowBuildCommon;

fn run_bindgen(build_common: &ShadowBuildCommon) {
    let bindings = build_common
        .bindgen_builder()
        .header("buddy.h")
        .allowlist_function("buddy_.*")
        .header("shmem_allocator.h")
        .allowlist_function("shmemblock_.*")
        .allowlist_function("shmemblockserialized_.*")
        .allowlist_function("shmemallocator_.*")
        .allowlist_function("shmemserializer_.*")
        .header("shmem_file.h")
        .allowlist_function("shmemfile_.*")
        .header("shmem_util.h")
        .allowlist_function("shmem_util_.*")
        // Finish the builder and generate the bindings.
        .generate()
        // Unwrap the Result and panic on failure.
        .expect("Unable to generate bindings");
    // Write the bindings to the $OUT_DIR/bindings.rs file.
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings!");
}

fn main() {
    let build_common = ShadowBuildCommon::new(Path::new("../../.."), None);

    run_bindgen(&build_common);

    build_common
        .cc_build()
        .files(&[
            "buddy.c",
            "shmem_allocator.c",
            "shmem_file.c",
            "shmem_util.c",
        ])
        .compile("shmem_c");
}
