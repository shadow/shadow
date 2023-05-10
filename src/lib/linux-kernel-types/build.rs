use std::{env, path::PathBuf};

fn run_bindgen() {
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());

    let header_contents = "
            #include <stdint.h>
            #include <stddef.h>
            #include <linux/signal.h>
            #include <sys/syscall.h>
        ";

    // Constants only (allowlist_var).
    // We re-export these with a wildcard in the module to avoid having
    // to enumerate e.g. all the syscall and signal numbers in the module definition.
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
        .allowlist_type("siginfo_t")
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file(out_path.join("types.rs"))
        .expect("Couldn't write bindings!");
}

fn main() {
    run_bindgen();
}
