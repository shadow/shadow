#include "vdl.h"
#include "vdl-tls.h"
#include "vdl-config.h"
#include "vdl-log.h"
#include "vdl-utils.h"
#include "vdl-sort.h"
#include "vdl-list.h"
#include "vdl-mem.h"
#include "vdl-alloc.h"
#include "machine.h"
#include "vdl-linkmap.h"
#include "vdl-file.h"

#define TLS_EXTRA_STATIC_ALLOC 1000

static void
file_initialize (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s, initialized=%u", file->name, file->tls_initialized);
  if (file->tls_initialized)
    {
      return;
    }

  ElfW(Phdr) *pt_tls = vdl_utils_search_phdr (file->phdr, file->phnum, PT_TLS);
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
  file->tls_is_static = (dt_flags & DF_STATIC_TLS)?1:0;
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
		 file->name, file->tls_tmpl_size, 
		 file->tls_init_zero_size);
}

static void
file_list_initialize (struct VdlList *files)
{
  // The only thing we need to make sure here is that the executable 
  // gets assigned tls module id 1 if needed.
  void **cur;
  for (cur = vdl_list_begin (files); 
       cur != vdl_list_end (files); 
       cur = vdl_list_next (cur))
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
       cur = vdl_list_next (cur))
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
  long size;
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
       cur = vdl_list_next (cur))
    {
      struct VdlFile *file = *cur;
      if (file->has_tls)
	{
	  if (file->tls_is_static)
	    {
	      tcb_size += file->tls_tmpl_size + file->tls_init_zero_size;
	      tcb_size = vdl_utils_align_up (tcb_size, file->tls_align);
	      file->tls_offset = - tcb_size;
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
  file_list_initialize (files);
  struct static_tls static_tls = initialize_static_tls (files);
  if (static_tls.size < g_vdl.tls_static_total_size)
    {
      g_vdl.tls_static_current_size = static_tls.size;
      g_vdl.tls_static_align = static_tls.align;
      return true;
    }
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
      g_vdl.tls_gen++;
      g_vdl.tls_n_dtv--;
    }
}

void vdl_tls_file_deinitialize (struct VdlList *files)
{
  // the deinitialization order here does not matter at all.
  void **cur;
  for (cur = vdl_list_begin (files); 
       cur != vdl_list_end (files); 
       cur = vdl_list_next (cur))
    {
      file_deinitialize (*cur);
    }  
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
  g_vdl.tls_static_total_size = vdl_utils_align_up (g_vdl.tls_static_total_size + g_vdl.tls_static_current_size + TLS_EXTRA_STATIC_ALLOC,
						    static_tls.align);
  g_vdl.tls_static_align = static_tls.align;
}

unsigned long
vdl_tls_tcb_allocate (void)
{
  // we allocate continuous memory for the set of tls blocks + libpthread TCB
  unsigned long tcb_size = g_vdl.tls_static_total_size;
  unsigned long total_size = tcb_size + CONFIG_TCB_SIZE; // specific to variant II
  unsigned long buffer = (unsigned long) vdl_alloc_malloc (total_size);
  vdl_memset ((void*)buffer, 0, total_size);
  unsigned long tcb = buffer + tcb_size;
  // complete setup of TCB
  vdl_memcpy ((void*)(tcb+CONFIG_TCB_TCB_OFFSET), &tcb, sizeof (tcb));
  vdl_memcpy ((void*)(tcb+CONFIG_TCB_SELF_OFFSET), &tcb, sizeof (tcb));
  return tcb;
}

void
vdl_tls_tcb_initialize (unsigned long tcb, unsigned long sysinfo)
{
  vdl_memcpy ((void*)(tcb+CONFIG_TCB_SYSINFO_OFFSET), &sysinfo, sizeof (sysinfo));
}

// This dtv structure needs to be compatible with the one used by the 
// glibc loader. Although it's supposed to be opaque to the glibc or 
// libpthread, it's not. nptl_db reads it to lookup tls variables (it
// reads dtv[i].value where i >= 1 to find out the address of a target
// tls block) and libpthread reads dtv[-1].value to find out the size
// of the dtv array and be able to memset it to zeros.
// The only leeway we have is in the glibc static field which we reuse
// to store a per-dtvi generation counter.
struct dtv_t
{
  unsigned long value;
  unsigned long is_static : 1;
  unsigned long gen : (sizeof(unsigned long) * 8 - 1);
};

void
vdl_tls_dtv_allocate (unsigned long tcb)
{
  VDL_LOG_FUNCTION ("tcb=%lu", tcb);
  // allocate a dtv for the set of tls blocks needed now
  struct dtv_t *dtv = vdl_alloc_malloc ((2+g_vdl.tls_n_dtv) * sizeof (struct dtv_t));
  dtv[0].value = g_vdl.tls_n_dtv;
  dtv[0].gen = 0;
  dtv++;
  dtv[0].value = 0;
  dtv[0].gen = g_vdl.tls_gen;
  vdl_memcpy ((void*)(tcb+CONFIG_TCB_DTV_OFFSET), &dtv, sizeof (dtv));
}

void
vdl_tls_dtv_initialize (unsigned long tcb)
{
  VDL_LOG_FUNCTION ("tcb=%lu", tcb);
  struct dtv_t *dtv;
  vdl_memcpy (&dtv, (void*)(tcb+CONFIG_TCB_DTV_OFFSET), sizeof (dtv));
  // allocate a dtv for the set of tls blocks needed now

  struct VdlFile *cur;
  for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
    {
      if (cur->has_tls)
	{
	  // setup the dtv to point to the tls block
	  if (cur->tls_is_static)
	    {
	      signed long dtvi = tcb + cur->tls_offset;
	      dtv[cur->tls_index].value = dtvi;
	      dtv[cur->tls_index].is_static = 1;
	      // copy the template in the module tls block
	      vdl_memcpy ((void*)dtvi, (void*)cur->tls_tmpl_start, cur->tls_tmpl_size);
	      vdl_memset ((void*)(dtvi + cur->tls_tmpl_size), 0, cur->tls_init_zero_size);
	    }
	  else
	    {
	      dtv[cur->tls_index].value = 0; // unallocated
	      dtv[cur->tls_index].is_static = 0;
	    }
	  dtv[cur->tls_index].gen = cur->tls_tmpl_gen;
	}
    }
  // initialize its generation counter
  dtv[0].gen = g_vdl.tls_gen;
}

static struct VdlFile *
find_file_by_module (unsigned long module)
{
  struct VdlFile *cur;
  for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
    {
      if (cur->has_tls && cur->tls_index == module)
	{
	  return cur;
	}
    }
  return 0;
}

void
vdl_tls_dtv_deallocate (unsigned long tcb)
{
  VDL_LOG_FUNCTION ("tcb=%lu", tcb);
  struct dtv_t *dtv;
  vdl_memcpy (&dtv, (void*)(tcb+CONFIG_TCB_DTV_OFFSET), sizeof (dtv));

  unsigned long dtv_size = dtv[-1].value;
  unsigned long module;
  for (module = 1; module <= dtv_size; module++)
    {
      if (dtv[module].value == 0)
	{
	  // this was an unallocated entry
	  continue;
	}
      if (dtv[module].is_static)
	{
	  // this was a static entry so, we don't
	  // have anything to free here.
	  continue;
	}
      // this was not a static entry
      unsigned long *dtvi = (unsigned long *)dtv[module].value;
      vdl_alloc_free (&dtvi[-1]);
    }
  vdl_alloc_free (&dtv[-1]);
}

void
vdl_tls_tcb_deallocate (unsigned long tcb)
{
  VDL_LOG_FUNCTION ("tcb=%lu", tcb);
  unsigned long start = tcb - g_vdl.tls_static_total_size;
  vdl_alloc_free ((void*)start);
}
static struct dtv_t *
get_current_dtv (void)
{
  // get the thread pointer for the current calling thread
  unsigned long tp = machine_thread_pointer_get ();
  // extract the dtv from it
  struct dtv_t *dtv;
  vdl_memcpy (&dtv, (void*)(tp+CONFIG_TCB_DTV_OFFSET), sizeof (dtv));
  return dtv;
}
void
vdl_tls_dtv_update (void)
{
  VDL_LOG_FUNCTION ("");
  unsigned long tp = machine_thread_pointer_get ();
  struct dtv_t *dtv = get_current_dtv ();
  unsigned long dtv_size = dtv[-1].value;
  if (dtv[0].gen == g_vdl.tls_gen)
    {
      return;
    }

  // first, we update the currently-available entries of the dtv.
  {
      unsigned long module;
      for (module = 1; module <= dtv_size; module++)
	{
	  if (dtv[module].value == 0)
	    {
	      // this is an un-initialized entry so, we leave it alone
	      continue;
	    }
	  struct VdlFile *file = find_file_by_module (module);
	  if (file != 0 && dtv[module].gen == file->tls_tmpl_gen)
	    {
	      // the entry is uptodate.
	      continue;
	    }
	  // module was unloaded
	  if (!dtv[module].is_static)
	    {
	      // and it was not static so, we free its memory
	      unsigned long *dtvi = (unsigned long *)dtv[module].value;
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
  }

  // now, check the size of the new dtv
  if (g_vdl.tls_n_dtv <= dtv_size)
    {
      // we have a big-enough dtv so, now that it's uptodate,
      // update the generation
      dtv[0].gen = g_vdl.tls_gen;
      return;
    }

  // the size of the new dtv is bigger than the 
  // current dtv. We need a newly-sized dtv
  vdl_tls_dtv_allocate (tp);
  struct dtv_t *new_dtv = get_current_dtv ();
  unsigned long new_dtv_size = new_dtv[-1].value;
  unsigned long module;
  // first, copy over the data from the old dtv into the new one.
  for (module = 1; module <= dtv_size; module++)
    {
      new_dtv[module] = dtv[module];
    }
  // then, initialize the new area in the new dtv
  for (module = dtv_size+1; module <= new_dtv_size; module++)
    {
      new_dtv[module].value = 0;
      new_dtv[module].gen = 0;
      new_dtv[module].is_static = 0;
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
	  signed long tcb = machine_thread_pointer_get ();
	  signed long dtvi = tcb + file->tls_offset;
	  new_dtv[file->tls_index].value = dtvi;
	  new_dtv[file->tls_index].is_static = 1;
	  new_dtv[file->tls_index].gen = file->tls_tmpl_gen;
	  // copy the template in the module tls block
	  vdl_memcpy ((void*)dtvi, (void*)file->tls_tmpl_start, file->tls_tmpl_size);
	  vdl_memset ((void*)(dtvi + file->tls_tmpl_size), 0, file->tls_init_zero_size);
	}
    }
  // now that the dtv is updated, update the generation
  new_dtv[0].gen = g_vdl.tls_gen;
  // finally, clear the old dtv
  vdl_alloc_free (&dtv[-1]);
}
unsigned long vdl_tls_get_addr_fast (unsigned long module, unsigned long offset)
{
  struct dtv_t *dtv = get_current_dtv ();
  if (dtv[0].gen == g_vdl.tls_gen && dtv[module].value != 0)
    {
      // our dtv is really uptodate _and_ the requested module block
      // has been already initialized.
      return dtv[module].value + offset;
    }
  // either we need to update the dtv or we need to initialize
  // the dtv entry to point to the requested module block
  return 0;
}
unsigned long vdl_tls_get_addr_slow (unsigned long module, unsigned long offset)
{
  VDL_LOG_FUNCTION ("module=%lu, offset=%lu", module, offset);
  unsigned long addr = vdl_tls_get_addr_fast (module, offset);
  if (addr != 0)
    {
      return addr;
    }
  struct dtv_t *dtv = get_current_dtv ();
  if (dtv[0].gen == g_vdl.tls_gen && dtv[module].value == 0)
    {
      // the dtv is uptodate but the requested module block 
      // has not been initialized already
      struct VdlFile *file = find_file_by_module (module);
      // first, allocate a new tls block for this module
      unsigned long dtvi_size = sizeof(unsigned long) + file->tls_tmpl_size + file->tls_init_zero_size;
      unsigned long *dtvi = vdl_alloc_malloc (dtvi_size);
      dtvi[0] = dtvi_size;
      dtvi++;
      // copy the template in the module tls block
      vdl_memcpy (dtvi, (void*)file->tls_tmpl_start, file->tls_tmpl_size);
      vdl_memset ((void*)(((unsigned long)dtvi) + file->tls_tmpl_size), 
			0, file->tls_init_zero_size);
      // finally, update the dtv
      dtv[module].value = (unsigned long)dtvi;
      dtv[module].gen = file->tls_tmpl_gen;
      dtv[module].is_static = 0;
      // and return the requested value
      return dtv[module].value + offset;
    }
  // we know for sure that the dtv is _not_ uptodate now
  vdl_tls_dtv_update ();

  // now that the dtv is supposed to be uptodate, attempt to make
  // the request again
  return vdl_tls_get_addr_slow (module, offset);
}
