#ifndef SHD_SHMEM_CLEANUP_H_
#define SHD_SHMEM_CLEANUP_H_

/*
 * Intended to be a public function.
 *
 * Cleans up orphaned shared memory files that are no longer mapped by a
 * shadow process. This function should never fail or crash, but is not
 * guaranteed to reclaim all possible orphans.
 */
void shmemcleanup_tryCleanup();

#endif // SHD_SHMEM_CLEANUP_H_
