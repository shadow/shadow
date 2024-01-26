// https://github.com/rust-lang/rfcs/blob/master/text/2585-unsafe-block-in-unsafe-fn.md
#![deny(unsafe_op_in_unsafe_fn)]

// this exposes information from the build script so that shadow can access it

pub const BUILD_TIMESTAMP: &str = env!("SHADOW_TIMESTAMP");

pub const GIT_COMMIT_INFO: Option<&str> = option_env!("SHADOW_GIT_COMMIT_INFO");
pub const GIT_DATE: Option<&str> = option_env!("SHADOW_GIT_DATE");
pub const GIT_BRANCH: Option<&str> = option_env!("SHADOW_GIT_BRANCH");
