/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "main/bindings/c/bindings.h"
#include "shd-build-info.h"

int main(int argc, char* argv[]) {
    ShadowBuildInfo info = {
        .version = SHADOW_VERSION_STRING,
        .build = SHADOW_BUILD_STRING,
        .info = SHADOW_INFO_STRING,
    };
    return main_runShadow(&info, argc, (const char**)argv);
}
