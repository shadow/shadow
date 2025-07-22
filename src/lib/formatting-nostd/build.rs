use std::path::Path;

use shadow_build_common::{Compiler, ShadowBuildCommon};

fn main() {
    let build_common = ShadowBuildCommon::new(Path::new("../../.."), None);

    build_common
        .cc_build(Compiler::C)
        .files(&["src/format_buffer_test_util.c"])
        .compile("test_lib");
}
