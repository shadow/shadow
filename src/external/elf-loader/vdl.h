#ifndef VDL_H
#define VDL_H

#include <stdint.h>
#include <sys/types.h>
#include <stdbool.h>
#include <elf.h>
#include <link.h>

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
enum VdlState {
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
  // the following fields are part of the gdb/libc ABI. Don't touch them.
  int version; // always 1
  struct VdlFile *link_map;
  void (*breakpoint)(void);
  enum VdlState state;
  unsigned long interpreter_load_base;
  // The list of directories to search for binaries
  // in DT_NEEDED entries.
  struct VdlList *search_dirs;
  uint32_t bind_now : 1;
  uint32_t finalized : 1;
  struct VdlFile *ldso;
  struct VdlList *contexts;
  unsigned long tls_gen;
  unsigned long tls_static_total_size;
  unsigned long tls_static_current_size;
  unsigned long tls_static_align;
  unsigned long tls_n_dtv;
  unsigned long tls_next_index;
  struct Futex *futex;
  // holds an entry for each thread which calls one a function
  // which potentially sets the dlerror state.
  struct VdlList *errors;
  // both member variables are used exclusively by vdl_dl_iterate_phdr
  unsigned long n_added;
  unsigned long n_removed;
  // dynamic sized array to cache mappings from tls_index to module
  unsigned long module_map_len;
  struct VdlFile **module_map;
  // preloaded files for inclusion in new contexts
  struct VdlList *preloads;
};

extern struct Vdl g_vdl;

#endif /* VDL_H */
