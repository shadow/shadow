#ifndef GLIBC_H
#define GLIBC_H

struct VdlList;

// Interfaces needed to make glibc be able to work when
// loaded with this loader.

// set _dl_starting_up to 1. Must be called
// just before calling the executable's entry point.
void glibc_startup_finished (void);

void glibc_initialize (void);

void glibc_patch (struct VdlList *files);

void glibc_set_stack_end (void * addr);

#endif /* GLIBC_H */
