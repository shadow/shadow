#include "alloc.h"
#include "vdl.h"
#include "vdl-tls.h"
#include "vdl-config.h"
#include "vdl-log.h"
#include "vdl-utils.h"
#include "vdl-sort.h"
#include "vdl-list.h"
#include "vdl-hashmap.h"
#include "vdl-mem.h"
#include "vdl-alloc.h"
#include "machine.h"
#include "vdl-linkmap.h"
#include "vdl-file.h"

#define TLS_EXTRA_STATIC_ALLOC 1000

static void
file_initialize (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s, initialized=%u", file->name,
                    file->tls_initialized);
  if (file->tls_initialized)
    {
      return;
    }

  ElfW (Phdr) *pt_tls =
    vdl_utils_search_phdr (file->phdr, file->phnum, PT_TLS);
  unsigned long dt_flags = file->dt_flags;
  if (pt_tls == 0)
    {
      file->has_tls = 0;
      file->tls_initialized = 1;
      return;
    }
  file->has_tls = 1;
  file->tls_initialized = 1;
  file->tls_tmpl_start = file->load_base + pt_tls->p_vaddr;
  file->tls_tmpl_size = pt_tls->p_filesz;
  file->tls_init_zero_size = pt_tls->p_memsz - pt_tls->p_filesz;
  file->tls_align = pt_tls->p_align;
  file->tls_index = g_vdl.tls_next_index;
  vdl_hashmap_insert (g_vdl.module_map, file->tls_index, file);
  file->tls_is_static = (dt_flags & DF_STATIC_TLS) ? 1 : 0;
  file->tls_tmpl_gen = g_vdl.tls_gen;
  // XXX: the next_index increment code below is bad for many reasons.
  // Instead, we should try to reuse tls indexes that are not used anymore
  // to ensure that the tls index we use is as small as possible to ensure
  // that the dtv array is as small as possible. we should keep
  // track of all allocated indexes in a global list.
  g_vdl.tls_next_index++;
  g_vdl.tls_gen++;
  g_vdl.tls_n_dtv++;
  VDL_LOG_DEBUG ("file=%s tmpl_size=%lu zero_size=%lu\n",
                 file->name, file->tls_tmpl_size, file->tls_init_zero_size);
}

static void
file_list_initialize (struct VdlList *files)
{
  // The only thing we need to make sure here is that the executable
  // gets assigned tls module id 1 if needed.
  void **cur;
  for (cur = vdl_list_begin (files);
       cur != vdl_list_end (files);
       cur = vdl_list_next (files, cur))
    {
      struct VdlFile *item = *cur;
      if (item->is_executable)
        {
          file_initialize (item);
          break;
        }
    }
  for (cur = vdl_list_begin (files);
       cur != vdl_list_end (files);
       cur = vdl_list_next (files, cur))
    {
      struct VdlFile *item = *cur;
      if (!item->is_executable)
        {
          file_initialize (item);
        }
    }
}

struct static_tls
{
  unsigned long size;
  long align;
};

struct static_tls
initialize_static_tls (struct VdlList *list)
{
  // We calculate the size of the memory needed for the
  // static and local tls model. We also initialize correctly
  // the tls_offset field to be able to perform relocations
  // next (the TLS relocations need the tls_offset field).
  unsigned long tcb_size = g_vdl.tls_static_current_size;
  unsigned long n_dtv = 0;
  unsigned long max_align = g_vdl.tls_static_align;
  void **cur;
  for (cur = vdl_list_begin (list);
       cur != vdl_list_end (list);
       cur = vdl_list_next (list, cur))
    {
      struct VdlFile *file = *cur;
      if (file->has_tls)
        {
          if (file->tls_is_static)
            {
              tcb_size += file->tls_tmpl_size + file->tls_init_zero_size;
              tcb_size = vdl_utils_align_up (tcb_size, file->tls_align);
              file->tls_offset = -tcb_size;
              if (file->tls_align > max_align)
                {
                  max_align = file->tls_align;
                }
            }
          n_dtv++;
        }
    }
  struct static_tls static_tls;
  static_tls.size = tcb_size;
  static_tls.align = max_align;
  return static_tls;
}

bool
vdl_tls_file_initialize (struct VdlList *files)
{
  write_lock (g_vdl.tls_lock);
  file_list_initialize (files);
  struct static_tls static_tls = initialize_static_tls (files);
  if (static_tls.size < g_vdl.tls_static_total_size)
    {
      g_vdl.tls_static_current_size = static_tls.size;
      g_vdl.tls_static_align = static_tls.align;
      write_unlock (g_vdl.tls_lock);
      return true;
    }
  write_unlock (g_vdl.tls_lock);
  return false;
}

static void
file_deinitialize (struct VdlFile *file)
{
  if (!file->tls_initialized)
    {
      return;
    }
  file->tls_initialized = 0;

  if (file->has_tls)
    {
      vdl_hashmap_remove (g_vdl.module_map, file->tls_index, file);
      g_vdl.tls_gen++;
      g_vdl.tls_n_dtv--;
    }
}

void
vdl_tls_file_deinitialize (struct VdlList *files)
{
  write_lock (g_vdl.tls_lock);
  // the deinitialization order here does not matter at all.
  void **cur;
  for (cur = vdl_list_begin (files);
       cur != vdl_list_end (files);
       cur = vdl_list_next (files, cur))
    {
      file_deinitialize (*cur);
    }
  write_unlock (g_vdl.tls_lock);
}

void
vdl_tls_file_initialize_main (struct VdlList *list)
{
  VDL_LOG_FUNCTION ("");
  g_vdl.tls_gen = 1;
  // We gather tls information for each module.
  file_list_initialize (list);
  // then perform initial setup of the static tls area
  struct static_tls static_tls = initialize_static_tls (list);
  g_vdl.tls_static_current_size = static_tls.size;
  g_vdl.tls_static_total_size =
    vdl_utils_align_up (g_vdl.tls_static_total_size +
                        g_vdl.tls_static_current_size +
                        TLS_EXTRA_STATIC_ALLOC, static_tls.align);
  g_vdl.tls_static_align = static_tls.align;
}

unsigned long
vdl_tls_tcb_allocate (void)
{
  // we allocate continuous memory for the set of tls blocks + libpthread TCB
  unsigned long tcb_size = g_vdl.tls_static_total_size;
  unsigned long total_size = tcb_size + CONFIG_TCB_SIZE;        // specific to variant II
  unsigned long buffer = (unsigned long) vdl_alloc_malloc (total_size);
  vdl_memset ((void *) buffer, 0, total_size);
  unsigned long tcb = buffer + tcb_size;
  // complete setup of TCB
  vdl_memcpy ((void *) (tcb + CONFIG_TCB_TCB_OFFSET), &tcb, sizeof (tcb));
  vdl_memcpy ((void *) (tcb + CONFIG_TCB_SELF_OFFSET), &tcb, sizeof (tcb));
  return tcb;
}

void
vdl_tls_tcb_initialize (unsigned long tcb, unsigned long sysinfo)
{
  vdl_memcpy ((void *) (tcb + CONFIG_TCB_SYSINFO_OFFSET), &sysinfo,
              sizeof (sysinfo));
}

// The dtv_t structure needs to be compatible with the one used by the
// glibc loader. Although it's supposed to be opaque to the glibc or
// libpthread, it's not. nptl_db reads it to lookup tls variables (it
// reads dtv[i].value where i >= 1 to find out the address of a target
// tls block) and libpthread reads dtv[-1] to find out the size
// of the dtv array and be able to memset it to zeros.
// dtv[0] is used as glibc/pthreads' generation counter.
// Details depend on the glibc version, see comments below.

struct dtv_meta
{
  unsigned long nptl;
  //ABI < glibc 2.25
  unsigned long is_static:1;
  unsigned long gen:(sizeof (unsigned long) * 8 - 1);
};

struct dtv_ptrs
{
  void *value;
  // ABI >= glibc 2.25
  void *to_free;
};

typedef union dtv
{
  struct dtv_meta meta;
  struct dtv_ptrs ptrs;
} dtv_t;

// some macros to define special values in the dtv
// macros with "ABI" in them are part of the glibc/pthreads ABI
#define DTV_ABI_GEN(dtv) dtv[0].meta.gen
// number of actively used elements
#define DTV_ABI_SIZE(dtv) dtv[-1].meta.nptl
// size of the buffer allocated
#define DTV_MEM_SIZE(dtv) dtv[-1].meta.gen
// tls for elf-loader to use
#define DTV_LOCAL_TLS(dtv) dtv[-2].ptrs.value

// we support two ABIs for the dtv, because of the new layout in glibc 2.25
#if __GLIBC_MINOR__ < 25
// In older versions, there's a single bit is_static flag we can use,
// plus store the generation counter in the slack space of the mem-aligned
// struct. No need for extra structs or functionality.
typedef dtv_t shadowdtv_t;
#define DTV_SHADOW_DTV(dtv) dtv
#define DTV_ALLOCATE_SHADOW(dtv, size)
#define DTV_MIGRATE_SHADOW(old_dtv, new_dtv, module)
#define DTV_FREE_SHADOW(dtv)
#define DTV_ABI_SET_TO_FREE(dtv, module)
#else
// From 25 on, the dtv struct replaces the "is_static" flag with a "to_free"
// field to keep track of the unaligned memory to call free() on.
// This causes two problems.
// One: we no longer have room in the dtv to store our metadata.
// We solve this by adding a "shadow" dtv that stores the fields that used to
// be in the dtv itself.
typedef union shadowdtv
{
  struct
  {
    unsigned long is_static:1; // width isn't for an ABI, it just saves space
    unsigned long gen:(sizeof (unsigned long) * 8 - 1);
  } meta;
} shadowdtv_t;
#define DTV_SHADOW_DTV(dtv) dtv[-2].ptrs.to_free
#define DTV_ALLOCATE_SHADOW(dtv, size)          \
  DTV_SHADOW_DTV(dtv) = vdl_alloc_malloc (size)
#define DTV_FREE_SHADOW(dtv) vdl_alloc_free (DTV_SHADOW_DTV(dtv))
#define DTV_MIGRATE_SHADOW(new_dtv, old_dtv, module)    \
  ((shadowdtv_t *) DTV_SHADOW_DTV(new_dtv))[module] =   \
    ((shadowdtv_t *) DTV_SHADOW_DTV(old_dtv))[module]
// Two: glibc's free() isn't our internal free(), and has an incompatible ABI.
// Since it's us who allocates the TLS, we can't let glibc clean it up.
// We make sure to always set the "to_free" field to 0, which free() ignores.
#define DTV_ABI_SET_TO_FREE(dtv, module)        \
  dtv[module].ptrs.to_free = 0
#endif

static inline dtv_t *
get_current_dtv (unsigned long tp)
{
  dtv_t *dtv;
  vdl_memcpy (&dtv, (void *) (tp + CONFIG_TCB_DTV_OFFSET), sizeof (dtv));
  return dtv;
}

static inline void
set_current_dtv (unsigned long tp, dtv_t *dtv)
{
  vdl_memcpy ((void *) (tp + CONFIG_TCB_DTV_OFFSET), &dtv, sizeof (dtv));
}

void
vdl_tls_dtv_allocate (unsigned long tcb)
{
  VDL_LOG_FUNCTION ("tcb=%lu", tcb);
  dtv_t *new_dtv, *current_dtv = get_current_dtv (tcb);
  // the 3 here is the two entries we put before the dtv, plus a new entry
  unsigned long needed_size = (3 + g_vdl.tls_n_dtv) * sizeof (dtv_t);
  bool migrate = current_dtv && (DTV_MEM_SIZE(current_dtv) < needed_size);
  struct LocalTLS *local_tls;
  if (!current_dtv)
    {
      local_tls = vdl_alloc_new (struct LocalTLS);
      local_tls->allocator = vdl_alloc_new (struct Alloc);
      alloc_initialize (local_tls->allocator);
    }
  else
    {
      local_tls = DTV_LOCAL_TLS(current_dtv);
    }

  if (!current_dtv || migrate)
    {
      // allocate a dtv for twice the set of tls blocks needed now
      new_dtv = vdl_alloc_malloc (2 * needed_size);
      // there are 2 entries before the dtv:
      new_dtv++; // our (elf-loader's) thread-local storage
      new_dtv++; // the metadata used by pthreads
      DTV_LOCAL_TLS(new_dtv) = local_tls;
      DTV_MEM_SIZE(new_dtv) = 2 * needed_size;
      // Must always be the same size as the real dtv.
      DTV_ALLOCATE_SHADOW(new_dtv, 2 * needed_size);
      set_current_dtv (tcb, new_dtv);
    }
  else
    {
      new_dtv = current_dtv;
    }

  DTV_ABI_SIZE(new_dtv) = g_vdl.tls_n_dtv;
  new_dtv[0].meta.nptl = 0;
  DTV_ABI_GEN(new_dtv) = g_vdl.tls_gen;

  if (migrate)
    {
      unsigned long module;
      // copy over the data from the old dtv into the new one.
      for (module = 1; module <= DTV_ABI_SIZE(current_dtv); module++)
        {
          new_dtv[module] = current_dtv[module];
          DTV_MIGRATE_SHADOW(new_dtv, current_dtv, module);
        }
      // clear the old dtv
      vdl_alloc_free (&current_dtv[-2]);
    }
}

void
vdl_tls_dtv_initialize (unsigned long tcb)
{
  VDL_LOG_FUNCTION ("tcb=%lu", tcb);
  dtv_t *dtv = get_current_dtv (tcb);
  shadowdtv_t *shadow_dtv = DTV_SHADOW_DTV(dtv);
  // allocate a dtv for the set of tls blocks needed now

  struct VdlFile *cur;
  for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
    {
      if (cur->has_tls)
        {
          // setup the dtv to point to the tls block
          if (cur->tls_is_static)
            {
              void *dtvi = (void *)tcb + cur->tls_offset;
              dtv[cur->tls_index].ptrs.value = dtvi;
              shadow_dtv[cur->tls_index].meta.is_static = 1;
              // copy the template in the module tls block
              vdl_memcpy ((void *) dtvi, (void *) cur->tls_tmpl_start,
                          cur->tls_tmpl_size);
              vdl_memset ((void *) (dtvi + cur->tls_tmpl_size), 0,
                          cur->tls_init_zero_size);
            }
          else
            {
              dtv[cur->tls_index].ptrs.value = 0;    // unallocated
              shadow_dtv[cur->tls_index].meta.is_static = 0;
            }
          DTV_ABI_SET_TO_FREE(dtv, cur->tls_index);
          shadowdtv_t *shadow_dtv = DTV_SHADOW_DTV(dtv);
          shadow_dtv[cur->tls_index].meta.gen = cur->tls_tmpl_gen;
        }
    }
  // initialize its generation counter
  DTV_ABI_GEN(dtv) = g_vdl.tls_gen;
}

inline struct LocalTLS *
vdl_tls_get_local_tls (void)
{
  if (g_vdl.tp_set)
    {
      unsigned long tp = machine_thread_pointer_get ();
      dtv_t *dtv = get_current_dtv (tp);
      if (dtv)
        {
          return (struct LocalTLS *) DTV_LOCAL_TLS(dtv);
        }
    }
  return 0;
}

int
module_map_compare (const void *module_void, const void *file_void)
{
  const unsigned long *module = (const unsigned long *) module_void;
  const struct VdlFile *file = (const struct VdlFile *) file_void;
  return (file && file->has_tls && file->tls_index == *module);
}

static struct VdlFile *
find_file_by_module (unsigned long module)
{
  struct VdlFile *file = (struct VdlFile *) vdl_hashmap_get (g_vdl.module_map,
                                                             module, &module,
                                                             module_map_compare);
  return file;
}

void
vdl_tls_dtv_deallocate (unsigned long tcb)
{
  VDL_LOG_FUNCTION ("tcb=%lu", tcb);
  dtv_t *dtv = get_current_dtv (tcb);
  shadowdtv_t *shadow_dtv = DTV_SHADOW_DTV(dtv);

  unsigned long dtv_size = DTV_ABI_SIZE(dtv);
  unsigned long module;
  for (module = 1; module <= dtv_size; module++)
    {
      if (dtv[module].ptrs.value == 0)
        {
          // this was an unallocated entry
          continue;
        }
      if (shadow_dtv[module].meta.is_static)
        {
          // this was a static entry so, we don't
          // have anything to free here.
          continue;
        }
      // this was not a static entry
      unsigned long *dtvi = (unsigned long *) dtv[module].ptrs.value;
      vdl_alloc_free (&dtvi[-1]);
    }
  // If we could, we would free the allocator associated with this thread now.
  // But it's possible that it has allocated memory that will be used/freed
  // later, on some other thread, so we can't.
  vdl_alloc_free ((struct LocalTLS *) DTV_LOCAL_TLS(dtv));
  DTV_FREE_SHADOW(dtv);
  vdl_alloc_free (&dtv[-2]);
}

void
vdl_tls_tcb_deallocate (unsigned long tcb)
{
  VDL_LOG_FUNCTION ("tcb=%lu", tcb);
  unsigned long start = tcb - g_vdl.tls_static_total_size;
  vdl_alloc_free ((void *) start);
}

// XXX: There's no way to free dtv modules yet, so this code can't do anything.
// It's also currently implemented as a linear time operation.
// For now, we'll just remove it until we actually need to implement it.
// When we do implement it, move this logic into the deinitialize functions.
static inline void
vdl_tls_dtv_update_current (__attribute__ ((unused)) dtv_t *dtv,
                            __attribute__ ((unused)) unsigned long dtv_size)
{
  /*
  unsigned long module;
  for (module = 1; module <= dtv_size; module++)
    {
      if (dtv[module].value == 0)
        {
          // this is an un-initialized entry so, we leave it alone
          continue;
        }
      struct VdlFile *file;
      file = find_file_by_module (module);
      if (file != 0 && dtv[module].gen == file->tls_tmpl_gen)
        {
          // the entry is uptodate.
          continue;
        }
      // module was unloaded
      if (!dtv[module].is_static)
        {
          // and it was not static so, we free its memory
          unsigned long *dtvi = (unsigned long *) dtv[module].value;
          vdl_alloc_free (&dtvi[-1]);
          dtv[module].value = 0;
        }
      if (file == 0)
        {
          // no new module was loaded so, we are good.
          continue;
        }
      // we have a new module loaded
      // we clear it so that it is initialized later if needed
      dtv[module].value = 0;
      dtv[module].gen = 0;
      dtv[module].is_static = 0;
    }
  */
}

static inline void
vdl_tls_dtv_update_new (dtv_t *new_dtv, unsigned long dtv_size,
                        unsigned long new_dtv_size, signed long tcb)
{
  unsigned long module;
  shadowdtv_t *new_shadow_dtv = DTV_SHADOW_DTV(new_dtv);
  for (module = dtv_size + 1; module <= new_dtv_size; module++)
    {
      new_dtv[module].ptrs.value = 0;
      DTV_ABI_SET_TO_FREE(new_dtv, module);
      new_shadow_dtv[module].meta.gen = 0;
      new_shadow_dtv[module].meta.is_static = 0;
      struct VdlFile *file = find_file_by_module (module);
      if (file == 0)
        {
          // the module has been loaded and then unloaded before
          // we updated our dtv so, well,
          // nothing to do here, just skip this empty entry
          continue;
        }
      if (file->tls_is_static)
        {
          void *dtvi = (void *)tcb + file->tls_offset;
          new_dtv[file->tls_index].ptrs.value = dtvi;
          new_shadow_dtv[file->tls_index].meta.is_static = 1;
          new_shadow_dtv[file->tls_index].meta.gen = file->tls_tmpl_gen;
          // copy the template in the module tls block
          vdl_memcpy (dtvi, (void *) file->tls_tmpl_start,
                      file->tls_tmpl_size);
          vdl_memset (dtvi + file->tls_tmpl_size, 0,
                      file->tls_init_zero_size);
        }
    }
}

static void
vdl_tls_dtv_update_given (unsigned long tp, dtv_t *dtv)
{
  VDL_LOG_FUNCTION ("");
  unsigned long dtv_size = DTV_ABI_SIZE(dtv);

  if (DTV_ABI_GEN(dtv) == g_vdl.tls_gen)
    {
      return;
    }

  // first, we update the currently-available entries of the dtv.
  vdl_tls_dtv_update_current (dtv, dtv_size);

  // now, check the size of the new dtv
  if (g_vdl.tls_n_dtv <= dtv_size)
    {
      // we have a big-enough dtv so, now that it's uptodate,
      // update the generation
      DTV_ABI_GEN(dtv) = g_vdl.tls_gen;
      return;
    }

  // the size of the new dtv is bigger than the
  // current dtv. We need a newly-sized dtv
  vdl_tls_dtv_allocate (tp);
  dtv_t *new_dtv = get_current_dtv (tp);
  unsigned long new_dtv_size = DTV_ABI_SIZE(new_dtv);
  // then, initialize the new area in the new dtv
  vdl_tls_dtv_update_new (new_dtv, dtv_size, new_dtv_size, tp);
  // now that the dtv is updated, update the generation
  DTV_ABI_GEN(new_dtv) = g_vdl.tls_gen;
}

void
vdl_tls_dtv_update (void)
{
  unsigned long tp = machine_thread_pointer_get ();
  read_lock (g_vdl.tls_lock);
  dtv_t *dtv = get_current_dtv (tp);
  vdl_tls_dtv_update_given (tp, dtv);
  read_unlock (g_vdl.tls_lock);
}

unsigned long
vdl_tls_get_addr_fast (unsigned long module, unsigned long offset)
{
  unsigned long tp = machine_thread_pointer_get ();
  dtv_t *dtv = get_current_dtv (tp);
  if (DTV_ABI_GEN(dtv) == g_vdl.tls_gen && dtv[module].ptrs.value != 0)
    {
      // our dtv is really uptodate _and_ the requested module block
      // has been already initialized.
      return dtv[module].ptrs.value + offset;
    }
  // either we need to update the dtv or we need to initialize
  // the dtv entry to point to the requested module block
  return 0;
}

unsigned long
vdl_tls_get_addr_slow (unsigned long module, unsigned long offset)
{
  VDL_LOG_FUNCTION ("module=%lu, offset=%lu", module, offset);
  read_lock (g_vdl.tls_lock);
  unsigned long addr = vdl_tls_get_addr_fast (module, offset);
  if (addr != 0)
    {
      read_unlock (g_vdl.tls_lock);
      return addr;
    }
  unsigned long tp = machine_thread_pointer_get ();
  dtv_t *dtv = get_current_dtv (tp);
  shadowdtv_t *shadow_dtv = DTV_SHADOW_DTV(dtv);
  if (DTV_ABI_GEN(dtv) == g_vdl.tls_gen && dtv[module].ptrs.value == 0)
    {
      // the dtv is uptodate but the requested module block
      // has not been initialized already
      struct VdlFile *file = find_file_by_module (module);
      // first, allocate a new tls block for this module
      unsigned long dtvi_size =
        sizeof (unsigned long) + file->tls_tmpl_size +
        file->tls_init_zero_size;
      unsigned long *dtvi = vdl_alloc_malloc (dtvi_size);
      dtvi[0] = dtvi_size;
      dtvi++;
      // copy the template in the module tls block
      vdl_memcpy (dtvi, (void *) file->tls_tmpl_start, file->tls_tmpl_size);
      vdl_memset ((void *) (((unsigned long) dtvi) + file->tls_tmpl_size),
                  0, file->tls_init_zero_size);
      // finally, update the dtv
      dtv[module].ptrs.value = dtvi;
      DTV_ABI_SET_TO_FREE(dtv, module);
      shadow_dtv[module].meta.gen = file->tls_tmpl_gen;
      shadow_dtv[module].meta.is_static = 0;
      // and return the requested value
      read_unlock (g_vdl.tls_lock);
      return dtv[module].ptrs.value + offset;
    }
  // we know for sure that the dtv is _not_ uptodate now
  vdl_tls_dtv_update_given (tp, dtv);

  // now that the dtv is supposed to be uptodate, attempt to make
  // the request again
  read_unlock (g_vdl.tls_lock);
  return vdl_tls_get_addr_slow (module, offset);
}

struct SwapArgs
{
  unsigned long t1;
  unsigned long t2;
  dtv_t *dtv1;
  dtv_t *dtv2;
};

void *
vdl_tls_swap_file (void *data, void *aux)
{
  struct VdlFile *file = data;
  struct SwapArgs *args = aux;

  if (!file->has_tls)
    {
      return 0;
    }

  if (file->tls_is_static)
    {
      // the TLS is static for this file, so we must swap the contents directly
      unsigned long tls_size = file->tls_tmpl_size + file->tls_init_zero_size;
      void *static_tls1 = (void *) args->t1 + file->tls_offset;
      void *static_tls2 = (void *) args->t2 + file->tls_offset;
      unsigned char tmp_tls[tls_size];
      vdl_memcpy (tmp_tls, static_tls1, tls_size);
      vdl_memcpy (static_tls1, static_tls2, tls_size);
      vdl_memcpy (static_tls2, tmp_tls, tls_size);
      return 0;
    }
  int module;
  // make sure we're not trying to swap the gen counter
  if ((module = file->tls_index) > 0)
    {
      dtv_t tmp_dtv = args->dtv1[module];
      args->dtv1[module] = args->dtv2[module];
      args->dtv2[module] = tmp_dtv;
      // we don't need to swap the shadow dtvs because we should only be
      // swapping after an update, so metadata it stores should be the same
    }
  return 0;
}

void
vdl_tls_swap_context (struct VdlContext *context, unsigned long t1, unsigned long t2)
{
  write_lock (g_vdl.tls_lock);
  dtv_t *dtv1 = get_current_dtv (t1);
  dtv_t *dtv2 = get_current_dtv (t2);
  // make sure we're not copying from/to uninitialized/unallocated memory
  vdl_tls_dtv_update_given (t1, dtv1);
  vdl_tls_dtv_update_given (t2, dtv2);
  dtv1 = get_current_dtv (t1);
  dtv2 = get_current_dtv (t2);
  struct SwapArgs args;
  args.t1 = t1;
  args.t2 = t2;
  args.dtv1 = dtv1;
  args.dtv2 = dtv2;
  vdl_list_search_on (context->loaded, &args, vdl_tls_swap_file);
  write_unlock (g_vdl.tls_lock);
}
