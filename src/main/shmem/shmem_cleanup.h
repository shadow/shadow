#ifndef SHD_SHMEM_CLEANUP_H_
#define SHD_SHMEM_CLEANUP_H_

#include <stdbool.h>

/*
 * Public function.
 *
 * Cleans up orphaned shared memory files that are no longer mapped by a
 * shadow process. This function should never fail or crash, but is not
 * guaranteed to reclaim all possible orphans.
 *
 * Set use_shadow_logging to true if the logging module is initialized;
 * otherwise, logging will go straight to stderr.
 */
void shmemcleanup_tryCleanup(bool use_shadow_logging);

#endif // SHD_SHMEM_CLEANUP_H_
