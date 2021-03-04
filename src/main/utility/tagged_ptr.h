/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_UTILITY_TAGGED_PTR_H_
#define SRC_MAIN_UTILITY_TAGGED_PTR_H_

#include <stdint.h>

extern const uintptr_t TAG_MASK;

uintptr_t tagPtr(const void* ptr, uintptr_t tag);
void* untagPtr(uintptr_t taggedPtr, uintptr_t* tag);

#endif /* SRC_MAIN_UTILITY_TAGGED_PTR_H_ */
