use std::path::Path;

use shadow_build_common::ShadowBuildCommon;

#[cfg(feature = "bindings")]
fn run_bindgen(build_common: &ShadowBuildCommon) {
    let bindings = build_common
        .bindgen_builder()
        .header("buddy.h")
        .allowlist_function("buddy_.*")
        .header("shmem_allocator.h")
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
    bindings
        .write_to_file("src/bindings.rs")
        .expect("Couldn't write bindings!");
}

fn main() {
    let build_common = ShadowBuildCommon::new(&Path::new("../../.."), None);

    #[cfg(feature = "bindings")]
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
