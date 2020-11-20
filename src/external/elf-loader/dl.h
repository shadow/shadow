#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
int dl_lmid_swap_tls (Lmid_t lmid, pthread_t *t1, pthread_t *t2);
int dl_lmid_copy_tls (Lmid_t lmid, pthread_t *t1, pthread_t *t2);
// custom flags

// dl(m)open() flag. Specifies that the loaded file should be placed in load
// order as though it were added via LD_PRELOAD, in all contexts.
#define RTLD_PRELOAD 0x00020
// dl(m)open() flag. Specifies that the loaded file should be placed in load
// order as though it were added via LD_PRELOAD, in this context only.
#define RTLD_INTERPOSE 0x00040

// dlinfo() flag. Populates info field with the size of the currently used
// static TLS.
#define RTLD_DI_STATIC_TLS_SIZE 127

// dl(m)open() flag. Disables running the init function of the loaded file
// as well as any of its dependencies (during the course of this load).
#define RTLD_NOINIT 0x00080

