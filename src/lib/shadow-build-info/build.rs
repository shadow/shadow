// This crate and build script exists to reduce how often we need to run the expensive build script
// in `src/main/`. Since we need to run some build script on every build to get the latest
// timestamp and git details, we do that in this crate instead.
//
// We only get the timestamp and git details here, and not other information like compiler flags or
// optimization levels. This is because crates can be built with different compiler options, so the
// compiler options for this crate might be different than the `src/main/`crate.

use std::process::Command;

fn main() {
    // Hack to make cargo always run the build script. We embed the timestamp and git details in
    // shadow's build info, so we always need to re-run this build script to update them.
    println!("cargo:rerun-if-changed=a-non-existent-path-sdafdmgidegegt3qyn5hw4oinqg");

    if let Some(git_commit_info) = git_commit_info() {
        println!("cargo:rustc-env=SHADOW_GIT_COMMIT_INFO={git_commit_info}");
    }

    if let Some(git_date) = git_date() {
        println!("cargo:rustc-env=SHADOW_GIT_DATE={git_date}");
    }

    if let Some(git_branch) = git_branch() {
        println!("cargo:rustc-env=SHADOW_GIT_BRANCH={git_branch}");
    }

    println!("cargo:rustc-env=SHADOW_TIMESTAMP={}", timestamp());
}

fn timestamp() -> String {
    let date_format = "[year]-[month]-[day]--[hour]:[minute]:[second]";
    let date_format = time::format_description::parse(date_format).unwrap();
    time::OffsetDateTime::now_utc()
        .format(&date_format)
        .unwrap()
}

fn git_commit_info() -> Option<String> {
    // current git commit version and hash
    //
    // `--always` allows us to still get the commit hash and dirty status when tags aren't
    // available, as sometimes happens when building from a shallow clone
    let Ok(git_describe) = Command::new("git")
        .args(["describe", "--always", "--long", "--dirty"])
        .output()
    else {
        return None;
    };

    if !git_describe.status.success() {
        return None;
    }

    let git_describe = std::str::from_utf8(&git_describe.stdout).ok()?.trim();

    Some(git_describe.into())
}

fn git_date() -> Option<String> {
    // current git commit short date
    let Ok(git_date) = Command::new("git")
        .args([
            "log",
            "--pretty=format:%ad",
            "--date=format:%Y-%m-%d--%H:%M:%S",
            "-n",
            "1",
        ])
        .output()
    else {
        return None;
    };

    if !git_date.status.success() {
        return None;
    }

    let git_date = std::str::from_utf8(&git_date.stdout).ok()?.trim();

    Some(git_date.into())
}

fn git_branch() -> Option<String> {
    let Ok(git_rev_parse) = Command::new("git")
        .args(["rev-parse", "--abbrev-ref", "HEAD"])
        .output()
    else {
        return None;
    };

    if !git_rev_parse.status.success() {
        return None;
    }

    let git_rev_parse = std::str::from_utf8(&git_rev_parse.stdout).ok()?.trim();

    Some(git_rev_parse.into())
}
