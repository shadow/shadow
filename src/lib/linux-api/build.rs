use std::{env, path::PathBuf};

fn run_bindgen() {
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());

    // Careful here to include *kernel* headers, and *not* glibc headers,
    // which may have different definitions. A notable example is
    // `<linux/signal.h>`  vs `<signal.h>`.
    let header_contents = "
            #include <stdint.h>
            #include <stddef.h>
            #include <linux/signal.h>
            #include <sys/syscall.h>
        ";

    // Constants only (allowlist_var).
    // Originally I kept this separate to support re-exporting this module publicly
    // without having to enumerate the constants, but that turns out not to work well.
    // TODO: consider merging with other bindings below.
    bindgen::Builder::default()
        .parse_callbacks(Box::new(bindgen::CargoCallbacks))
        // Use ::core instead of ::std, since this crate is no_std.
        .use_core()
        .header_contents("kernel_defs.h", header_contents)
        // syscall numbers
        .allowlist_var("SYS_.*")
        .allowlist_var("SIGRTMIN")
        .allowlist_var("SIGRTMAX")
        .allowlist_var("SS_AUTODISARM")
        // signal numbers
        .allowlist_var("SIG.*")
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file(out_path.join("constants.rs"))
        .expect("Couldn't write bindings!");

    bindgen::Builder::default()
        .parse_callbacks(Box::new(bindgen::CargoCallbacks))
        // Use ::core instead of ::std, since this crate is no_std.
        .use_core()
        .header_contents("kernel_defs.h", header_contents)
        .allowlist_type("sigset_t")
        .allowlist_type("sigaction")
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file(out_path.join("types.rs"))
        .expect("Couldn't write bindings!");
}

fn main() {
    run_bindgen();
}
