#include "system.h"
#include "vdl.h"
#include "vdl-utils.h"
#include "vdl-alloc.h"
#include "vdl-log.h"
#include "vdl-list.h"
#include "vdl-reloc.h"
#include "vdl-dl.h"
#include "glibc.h"
#include "valgrind.h"
#include "gdb.h"
#include "machine.h"
#include "stage2.h"
#include "futex.h"
#include "vdl-gc.h"
#include "vdl-tls.h"
#include "vdl-sort.h"
#include "vdl-context.h"
#include "vdl-linkmap.h"
#include "vdl-map.h"
#include "vdl-file.h"
#include "vdl-unmap.h"
#include "vdl-init.h"
#include "vdl-fini.h"
#include <stdbool.h>


static unsigned long
get_entry_point (unsigned long load_base,
                 unsigned long phnum, ElfW (Phdr) * phdr)
{
  ElfW (Phdr) * pt_load = vdl_utils_search_phdr (phdr, phnum, PT_LOAD);
  if (pt_load == 0)
    {
      // should never happen: there should always be a PT_LOAD entry
      return 0;
    }
  if (pt_load->p_offset > 0 || pt_load->p_filesz < sizeof (ElfW (Ehdr)))
    {
      // should never happen: the elf header should always be mapped
      // within the first PT_LOAD entry.
      return 0;
    }
  ElfW (Ehdr) * header = (ElfW (Ehdr) *) (pt_load->p_vaddr + load_base);
  return header->e_entry + load_base;
}

static char *
get_pt_interp (unsigned long main_load_base,
               unsigned long phnum, ElfW (Phdr) * phdr)
{
  // will not work when main exec is loader itself
  ElfW (Phdr) * pt_interp = vdl_utils_search_phdr (phdr, phnum, PT_INTERP);
  if (pt_interp == 0)
    {
      return 0;
    }
  return (char *) (main_load_base + pt_interp->p_vaddr);
}


static struct VdlMapResult
interpreter_map (unsigned long load_base,
                 const char *pt_interp, struct VdlContext *context)
{
  /* We make many assumptions here:
   *   - The loader is an ET_DYN
   *   - The loader has been compile-time linked at base address 0
   *   - The first PT_LOAD map of the interpreter contains the elf header
   *     and program headers.
   *
   * Consequently, we can infer that the load_base in fact points to
   * the first PT_LOAD map of the interpreter which means that load_base
   * in fact points to the elf header itself.
   */
  ElfW (Ehdr) * header = (ElfW (Ehdr) *) load_base;
  ElfW (Phdr) * phdr = (ElfW (Phdr) *) (header->e_phoff + load_base);
  struct VdlMapResult result;
  // It's important to initialize the filename of the interpreter
  // entry in the linkmap to the PT_INTERP of the main binary for
  // gdb. gdb initializes its first linkmap with an entry which describes
  // the loader with a filename equal to PT_INTERP so, if we don't use
  // the same value, gdb will incorrectly believe that the loader entry
  // has been removed which can lead to certain bad things to happen
  // in the first call to r_debug_state.
  result = vdl_map_from_memory (load_base, header->e_phnum, phdr,
                                pt_interp, LDSO_SONAME, context);
  if (result.requested == 0)
    {
      goto error;
    }
  // the interpreter has already been reloced during stage1, so,
  // we must be careful to not relocate it twice.
  result.requested->reloced = 1;
error:
  return result;
}

static bool
ld_preload_lists (struct VdlList *preload_files, struct VdlList *preload_deps,
                  struct VdlContext *context, const char **envp)
{
  bool retval = true;
  const char *preload_env = vdl_utils_getenv (envp, "LD_PRELOAD");
  struct VdlList *preload_names = vdl_utils_strsplit (preload_env, ':');
  void **cur;
  for (cur = vdl_list_begin (preload_names);
       cur != vdl_list_end (preload_names);
       cur = vdl_list_next (cur))
    {
      char *filename = *cur;
      if (filename[0] == 0)
        {
          continue;
        }
      struct VdlMapResult result = vdl_map_from_filename (context, filename);
      if (result.requested == 0)
        {
          VDL_LOG_ERROR ("Could not map LD_PRELOAD file %s: %s\n", filename,
                         result.error_string);
          retval = false;
          goto error;
        }
      result.requested->count++;
      result.requested->is_interposer = 1;
      vdl_list_push_back (preload_files, result.requested);
      vdl_list_insert_range (preload_deps, vdl_list_end (preload_deps),
                             vdl_list_begin (result.newly_mapped),
                             vdl_list_end (result.newly_mapped));
      vdl_list_delete (result.newly_mapped);
    }

  // save preload list for searching when in other contexts
  g_vdl.preloads = preload_files;
error:
  vdl_utils_str_list_delete (preload_names);
  return retval;
}


// this function is just used to set up the immediate results of the env var,
// the actual initialization of static TLS happens later
static void
setup_static_tls_extra (const char *static_tls_extra)
{
  if (static_tls_extra == 0)
    {
      g_vdl.tls_static_total_size = 0;
      return;
    }

  unsigned long static_tls_size = vdl_utils_strtoul (static_tls_extra);
  g_vdl.tls_static_total_size = static_tls_size;

  // static TLS is stored in the same map as the stack, so to accommodate
  // the extra, we increase the default stack allocation size
  struct rlimit rl;
  int result = system_getrlimit (RLIMIT_STACK, &rl);
  if (result != 0)
    {
      VDL_LOG_ERROR
        ("Failed to increase stack size to accommodate LD_STATIC_TLS_EXTRA: getrlimit returned %d\n",
         result);
      return;
    }
  unsigned long new_stack_size = rl.rlim_cur + static_tls_size;
  if (new_stack_size > rl.rlim_max)
    {
      VDL_LOG_ERROR ("\
WARNING: The hard limit for stack size is too small to add the given\n\
LD_STATIC_TLS_EXTRA. Use a smaller value, or increase the hard limit\n\
(e.g., in bash, run 'ulimit -Hs [larger number]' as root).\n\
Attempting to run with maximum permitted stack size.\n\
");
      new_stack_size = rl.rlim_max;
    }
  rl.rlim_cur = new_stack_size;
  result = system_setrlimit (RLIMIT_STACK, &rl);
  if (result != 0)
    {
      VDL_LOG_ERROR
        ("Failed to increase stack size to accommodate LD_STATIC_TLS_EXTRA: setrlimit returned %d\n",
         result);
    }
}

static void
setup_env_vars (const char **envp)
{
  // populate search_dirs from LD_LIBRARY_PATH
  const char *ld_lib_path = vdl_utils_getenv (envp, "LD_LIBRARY_PATH");
  struct VdlList *list = vdl_utils_splitpath (ld_lib_path);
  vdl_list_insert_range (g_vdl.search_dirs,
                         vdl_list_begin (g_vdl.search_dirs),
                         vdl_list_begin (list), vdl_list_end (list));
  vdl_list_delete (list);

  // setup logging from LD_LOG
  const char *ld_log = vdl_utils_getenv (envp, "LD_LOG");
  vdl_log_set (ld_log);

  // setup bind_now from LD_BIND_NOW
  const char *bind_now = vdl_utils_getenv (envp, "LD_BIND_NOW");
  if (bind_now != 0)
    {
      g_vdl.bind_now = 1;
    }

  // get additional static TLS size from LD_STATIC_TLS_EXTRA
  const char *static_tls_extra =
    vdl_utils_getenv (envp, "LD_STATIC_TLS_EXTRA");
  setup_static_tls_extra (static_tls_extra);
}

struct Stage2Output
stage2_initialize (struct Stage2Input input)
{
  struct Stage2Output output = {.entry_point = 0 };

  setup_env_vars ((const char **) input.program_envp);

  // The load base of the main program is easy to calculate as the difference
  // between the PT_PHDR vaddr and its real address in memory.
  unsigned long main_load_base =
    ((unsigned long) input.program_phdr) - input.program_phdr->p_vaddr;

  struct VdlContext *context = vdl_context_new (input.program_argc,
                                                input.program_argv,
                                                input.program_envp);

  // First, let's make sure we have an entry for the loader
  const char *pt_interp = get_pt_interp (main_load_base,
                                         input.program_phnum,
                                         input.program_phdr);
  struct VdlMapResult interp_result;
  interp_result = interpreter_map (input.interpreter_load_base,
                                   pt_interp, context);
  struct VdlFile *interp = interp_result.requested;
  VDL_LOG_ASSERT (interp != 0,
                  "Could not map loader %s: %s", pt_interp,
                  interp_result.error_string);
  vdl_list_delete (interp_result.newly_mapped); // there are no deps
  interp->count++;
  g_vdl.ldso = interp;

  // Add the LD_PRELOAD binaries if they are specified somewhere.
  // We must do this _before_ adding the dependencies of the main
  // binary to the link map to ensure that the symbol scope of
  // the main binary is correct, that is, that symbols are
  // resolved first within the LD_PRELOAD binaries, before every
  // other library, but after the main binary itself.
  // However, the dependencies of the LD_PRELOAD binaries should
  // be added _after_ the dependencies of the main binary,
  // to maintain the behavior of glibc.
  struct VdlList *ld_preload = vdl_list_new ();
  struct VdlList *preload_deps = vdl_list_new ();
  bool ok = ld_preload_lists (ld_preload, preload_deps, context,
                              (const char **) input.program_envp);
  if (!ok)
    {
      goto error;
    }

  // Now, Let's do the main binary.
  struct VdlMapResult main_result;
  main_result = vdl_map_from_memory (main_load_base,
                                     input.program_phnum, input.program_phdr,
                                     // the filename for the main exec is "" for gdb.
                                     "", input.program_argv[0], context);
  VDL_LOG_ASSERT (main_result.requested != 0,
                  "unable to map main binary (%s) and dependencies: %s\n",
                  input.program_argv[0], main_result.error_string);
  struct VdlFile *main_file = main_result.requested;
  main_file->count++;
  main_file->is_executable = 1;
  main_file->is_interposer = 1;
  context->has_main = 1;

  // Now, we setup our public linkmap.
  // We need to be careful to insert first the main file,
  // then, the interpreter, then, the ld preload entries,
  // then, the dependencies from the main file,
  // then, the dependencies from the ld preload entries
  vdl_linkmap_append (main_file);
  vdl_linkmap_append (interp);
  vdl_linkmap_append_range (vdl_list_begin (ld_preload),
                            vdl_list_end (ld_preload));
  vdl_linkmap_append_range (vdl_list_begin (main_result.newly_mapped),
                            vdl_list_end (main_result.newly_mapped));
  vdl_linkmap_append_range (vdl_list_begin (preload_deps),
                            vdl_list_end (preload_deps));
  vdl_list_delete (main_result.newly_mapped);
  main_result.newly_mapped = 0;

  // Now, prepare the global scope of the main context
  // The global scope is the same as the public linkmap except that
  // it does not contain the interpreter (unless, of course, it
  // is a dependency of the main binary or one of the ld_preloaded
  // binaries).
  vdl_list_push_back (context->global_scope, main_file);
  // of course, the ld_preload binaries must be in there if needed.
  vdl_list_insert_range (context->global_scope,
                         vdl_list_end (context->global_scope),
                         vdl_list_begin (ld_preload),
                         vdl_list_end (ld_preload));
  struct VdlList *all_deps = vdl_sort_deps_breadth_first (main_file);
  vdl_list_insert_range (context->global_scope,
                         vdl_list_end (context->global_scope),
                         vdl_list_begin (all_deps), vdl_list_end (all_deps));
  vdl_list_insert_range (context->global_scope,
                         vdl_list_end (context->global_scope),
                         vdl_list_begin (preload_deps),
                         vdl_list_end (preload_deps));
  vdl_list_delete (all_deps);
  vdl_list_unicize (context->global_scope);

  vdl_list_delete (preload_deps);

  gdb_initialize (main_file);

  // We need to do this before relocation because the TLS-type relocations
  // need tls information.
  vdl_tls_file_initialize_main (context->loaded);

  // We either setup the GOT for lazy symbol resolution
  // or we perform binding for all symbols now if LD_BIND_NOW is set
  vdl_reloc (context->loaded, g_vdl.bind_now);

  // Once relocations are done, we can initialize the tls blocks
  // and the dtv. We need to wait post-reloc because the tls
  // template area used to initialize the tls blocks is likely
  // to be modified during relocation processing.
  unsigned long tcb = vdl_tls_tcb_allocate ();
  vdl_tls_tcb_initialize (tcb, input.sysinfo);
  vdl_tls_dtv_allocate (tcb);
  vdl_tls_dtv_initialize (tcb);
  // configure the current thread to use this TCB as a thread pointer
  machine_thread_pointer_set (tcb);

  // Note that we must invoke this method to notify gdb that we have
  // a valid linkmap only _after_ relocations have been done (if you do
  // it before, gdb gets confused) and _before_ the initializers are
  // run (to allow the user to debug the initializers).
  gdb_notify ();

  // patch glibc functions which need to be overriden.
  // This is really a hack I am not very proud of.
  glibc_patch (context->loaded);

  // glibc-specific crap to avoid segfault in initializer
  glibc_initialize (input.clktck);

  valgrind_initialize ();

  // Finally, call init functions
  struct VdlList *call_init = vdl_sort_call_init (context->loaded);
  vdl_init_call (call_init);
  vdl_list_delete (call_init);

  unsigned long entry = get_entry_point (main_load_base,
                                         input.program_phnum,
                                         input.program_phdr);
  if (entry == 0)
    {
      VDL_LOG_ERROR ("Zero entry point: nothing to do in %s\n",
                     main_file->name);
      goto error;
    }
  glibc_startup_finished ();

  output.entry_point = entry;
  return output;
error:
  system_exit (-6);
  return output;                // quiet compiler
}

void
stage2_freeres (void)
{
  VDL_LOG_FUNCTION ("");
  // We know, that we will _not_ be called again after we return
  // from this function so, we can cleanup everything _except_ for the
  // code/data memory mappings because the caller code segment would be
  // unmapped and that would trigger interesting crashes upon return
  // from this function. When we return, the caller is going to call
  // the exit_group syscall.

  struct VdlList *link_map = vdl_linkmap_copy ();
  vdl_unmap (link_map, false);
  vdl_list_delete (link_map);

  unsigned long tcb = machine_thread_pointer_get ();
  vdl_tls_dtv_deallocate (tcb);
  vdl_tls_tcb_deallocate (tcb);
}

#ifdef DEBUG
inline static void
file_list_print (struct VdlList *l)
{
  void **cur;
  for (cur = vdl_list_begin (l); cur != vdl_list_end (l);
       cur = vdl_list_next (cur))
    {
      struct VdlFile *file = *cur;
      VDL_LOG_DEBUG ("file=%p/\"%s\"\n", file, file->filename);
    }
}
#else
#define file_list_print(l)
#endif

void
stage2_finalize (void)
{
  // Our job here is to invoke the destructors of all still-loaded
  // objects. This is tricky since:
  //   - must handle all namespaces
  //   - must handle still-running code in other threads
  futex_lock (g_vdl.global_futex);
  struct VdlList *link_map = vdl_linkmap_copy ();
  struct VdlList *call_fini = vdl_sort_call_fini (link_map);
  struct VdlList *locked = vdl_fini_lock (call_fini);
  vdl_list_delete (call_fini);
  vdl_list_delete (link_map);

  futex_unlock (g_vdl.global_futex);
  vdl_fini_call (locked);
  futex_lock (g_vdl.global_futex);

  vdl_list_delete (locked);
  futex_unlock (g_vdl.global_futex);
}
