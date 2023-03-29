/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_MAIN_H_
#define SHD_MAIN_H_

#include <stdbool.h>

#include "main/bindings/c/bindings.h"

int main_checkGlibVersion();
void main_printBuildInfo(const ShadowBuildInfo* shadowBuildInfo);
void main_logBuildInfo(const ShadowBuildInfo* shadowBuildInfo);

#endif /* SHD_MAIN_H_ */
