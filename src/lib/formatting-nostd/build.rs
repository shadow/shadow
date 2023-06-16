use std::path::Path;

use shadow_build_common::ShadowBuildCommon;

fn main() {
    let build_common = ShadowBuildCommon::new(Path::new("../../.."), None);

    build_common
        .cc_build()
        .files(&["src/format_buffer_test_util.c"])
        .compile("test_lib");
}
