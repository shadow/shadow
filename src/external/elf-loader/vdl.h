#ifndef VDL_H
#define VDL_H

#include <stdint.h>
#include <sys/types.h>
#include <stdbool.h>
#include <elf.h>
#include <link.h>
#include "vdl-rbtree.h"

#if __ELF_NATIVE_CLASS == 32
#define ELFW_R_SYM ELF32_R_SYM
#define ELFW_R_TYPE ELF32_R_TYPE
#define ELFW_ST_BIND(val) ELF32_ST_BIND(val)
#define ELFW_ST_TYPE(val) ELF32_ST_TYPE(val)
#define ELFW_ST_INFO(bind, type) ELF32_ST_INFO(bind,type)
#else
#define ELFW_R_SYM ELF64_R_SYM
#define ELFW_R_TYPE ELF64_R_TYPE
#define ELFW_ST_BIND(val) ELF64_ST_BIND(val)
#define ELFW_ST_TYPE(val) ELF64_ST_TYPE(val)
#define ELFW_ST_INFO(bind, type) ELF64_ST_INFO(bind,type)
#endif

struct Futex;

// the numbers below must match the declarations from svs4
enum VdlState
{
  VDL_CONSISTENT = 0,
  VDL_ADD = 1,
  VDL_DELETE = 2
};

struct VdlError
{
  char *error;
  char *prev_error;
  unsigned long thread_pointer;
};

struct Vdl
{
  // The following fields are part of the gdb/libc ABI.
  // The alignment of them must match those given in the r_debug struct in
  // elf/dl-debug.c in the glibc source tree
  int version;                  // always 1
  struct VdlFile *link_map;
  void (*breakpoint) (void);
  enum VdlState state;
  unsigned long interpreter_load_base;
  // end ABI-compatible fields

  // we keep our internal link map here, then populate the ABI one when needed for gdb
  struct VdlList *shadow_link_map;
  struct VdlFile *link_map_tail;
  struct RWLock *link_map_lock;
  // The list of directories to search for binaries in DT_NEEDED entries.
  struct VdlList *search_dirs;
  uint32_t bind_now:1;
  uint32_t finalized:1;
  // the TCB has been set as the thread pointer
  uint32_t tp_set:1;
  struct VdlFile *ldso;
  struct VdlContext *main_context;
  // these hashmaps are just used for set-membership testing to detect errors,
  // they could probably be replaced with something like a bloom filter
  struct VdlHashMap *contexts;
  struct VdlHashMap *files;

  // to use this lock:
  // read lock when all modified fields must be from the executing thread
  // write lock when potentially modifying global or another thread's fields
  struct RWLock *tls_lock;
  unsigned long tls_gen;
  unsigned long tls_static_total_size;
  unsigned long tls_static_current_size;
  unsigned long tls_static_align;
  unsigned long tls_n_dtv;
  unsigned long tls_next_index;
  // The original single futex for everything.
  // The goal is to replace this with more specific locks.
  struct RWLock *global_lock;
  // holds an entry for each thread which calls one a function
  // which potentially sets the dlerror state.
  struct VdlList *errors;
  // both member variables are used exclusively by vdl_dl_iterate_phdr
  unsigned long n_added;
  unsigned long n_removed;
  // dynamic sized array to cache mappings from tls_index to module
  struct VdlHashMap *module_map;
  // preloaded files for inclusion in new contexts
  struct VdlList *preloads;
  // tree for mapping address -> map containing address -> file containing map
  vdl_rbtree_t *address_ranges;
  // hash map of readonly file sections (e.g. .text) to their mappings for reuse
  struct VdlHashMap *readonly_cache;
  // futex for the readonly cache
  struct Futex *ro_cache_futex;
  // the unique ephemeral path we use for our shared memory mappings
  char* shm_path;
  // list of thread-local allocators for cleanup
  struct VdlList *allocators;
};

extern struct Vdl g_vdl;

#endif /* VDL_H */
