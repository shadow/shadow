#ifndef DISABLE_ASLR_H_
#define DISABLE_ASLR_H_

/*
 * Disable address space layout randomization of processes under simulation.
 * Forked processes inherit this personality trait, so this can be called from
 * a parent process that's forking simulated processes. Logs a warning if the
 * routine fails.
 *
 * THREAD SAFETY: thread-safe.
 */
void disable_aslr();

#endif // DISABLE_ASLR_H_
