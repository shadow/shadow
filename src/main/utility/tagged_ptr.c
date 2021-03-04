/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "main/utility/tagged_ptr.h"
#include "main/utility/utility.h"

// two low-order bits
const uintptr_t TAG_MASK = (1 << 2) - 1;

// store a tag in the unused low-order bits of a pointer
uintptr_t tagPtr(const void* ptr, uintptr_t tag) {
    uintptr_t ptrInt = (uintptr_t)ptr;

    utility_assert((ptrInt & TAG_MASK) == 0);
    utility_assert((tag & ~TAG_MASK) == 0);

    return ptrInt | tag;
}

// remove the tag from a tagged pointer
void* untagPtr(uintptr_t taggedPtr, uintptr_t* tag) {
    if (tag != NULL) {
        *tag = taggedPtr & TAG_MASK;
    }

    return (void*)(taggedPtr & ~TAG_MASK);
}
