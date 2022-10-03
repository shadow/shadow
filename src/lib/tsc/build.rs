use std::path::Path;

use shadow_build_common::ShadowBuildCommon;

fn main() {
    let build_common = ShadowBuildCommon::new(&Path::new("../../.."), None);
    build_common.cc_build().file("tsc.c").compile("tsc_c");
}
