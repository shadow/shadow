#ifndef FORK_PROXY_H
#define FORK_PROXY_H

#include <sys/types.h>

// An object for forking processes on a separate thread.
//
// ForkProxy itself is *not* thread safe.
typedef struct _ForkProxy ForkProxy;

// Creates a new ForkProxy.
ForkProxy* forkproxy_new(pid_t (*do_fork_exec)(const char* file, char* const argv[],
                                               char* const envp[], const char* working_dir));

// Calls the provided `do_fork_exec` on ForkProxy's thread.
pid_t forkproxy_forkExec(ForkProxy* forkproxy, const char* file, char* const argv[],
                         char* const envp[], const char* working_dir);

#endif
