/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "main/utility/tagged_ptr.h"

#include "lib/logger/logger.h"
#include "main/utility/utility.h"

// three low-order bits
const uintptr_t TAG_MASK = (1 << 3) - 1;

// store a tag in the unused low-order bits of a pointer
uintptr_t tagPtr(const void* ptr, uintptr_t tag) {
    uintptr_t ptrInt = (uintptr_t)ptr;

    if ((ptrInt & TAG_MASK) != 0) {
        utility_panic("Low-order bits of pointer are in use");
    }
    if ((tag & ~TAG_MASK) != 0) {
        utility_panic("Tag has high-order bits set");
    }

    return ptrInt | tag;
}

// remove the tag from a tagged pointer
void* untagPtr(uintptr_t taggedPtr, uintptr_t* tag) {
    if (tag != NULL) {
        *tag = taggedPtr & TAG_MASK;
    }

    return (void*)(taggedPtr & ~TAG_MASK);
}
