#ifndef VDL_TLS_H
#define VDL_TLS_H

#include "vdl-list.h"
#include <stdbool.h>

// our own version of TLS for internal elf-loader use
struct LocalTLS
{
  struct Alloc *allocator;
};

// called prior initial relocation processing.
// collect and store tls information about everything
// in g_vdl and each file
void vdl_tls_file_initialize_main (struct VdlList *list);
// allocate a tcb buffer
unsigned long vdl_tls_tcb_allocate (void);
// setup the sysinfo field in tcb
void vdl_tls_tcb_initialize (unsigned long tcb, unsigned long sysinfo);
// allocate a dtv vector and set it in the tcb
void vdl_tls_dtv_allocate (unsigned long tcb);
// The job of this function is to:
//    - initialize each static entry in the dtv to point to the right tls module block
//    - initialize each dynamic entry in the dtv to the UNALLOCATED value (0)
//    - initialize the content of each static tls module block with the associated
//      template
//    - initialize the dtv generation counter
void vdl_tls_dtv_initialize (unsigned long tcb);
// initialize per-file tls information
bool vdl_tls_file_initialize (struct VdlList *files);
void vdl_tls_dtv_deallocate (unsigned long tcb);
void vdl_tls_tcb_deallocate (unsigned long tcb);
struct LocalTLS *vdl_tls_get_local_tls (void);
// no need to call the _fast version with any kind of lock held
unsigned long vdl_tls_get_addr_fast (unsigned long module, unsigned long offset);
// the _slow version needs a lock held
unsigned long vdl_tls_get_addr_slow (unsigned long module, unsigned long offset);

// ensure that the caller dtv is uptodate.
void vdl_tls_dtv_update (void);

void vdl_tls_file_deinitialize (struct VdlList *files);

#endif /* VDL_TLS_H */
