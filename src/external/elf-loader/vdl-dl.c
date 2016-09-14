#include "vdl.h"
#include "vdl-log.h"
#include "vdl-utils.h"
#include "vdl-list.h"
#include "gdb.h"
#include "glibc.h"
#include "vdl-dl.h"
#include "vdl-gc.h"
#include "vdl-reloc.h"
#include "vdl-lookup.h"
#include "vdl-tls.h"
#include "machine.h"
#include "futex.h"
#include "macros.h"
#include "vdl-sort.h"
#include "vdl-context.h"
#include "vdl-alloc.h"
#include "vdl-linkmap.h"
#include "vdl-file.h"
#include "vdl-map.h"
#include "vdl-unmap.h"
#include "vdl-init.h"
#include "vdl-fini.h"

// reuse glibc flag.
#define __RTLD_OPENEXEC 0x20000000

static struct VdlError *find_error (void)
{
  unsigned long thread_pointer = machine_thread_pointer_get ();
  void **i;
  for (i = vdl_list_begin (g_vdl.errors);
       i != vdl_list_end (g_vdl.errors);
       i = vdl_list_next (i))
    {
      struct VdlError *error = *i;
      if (error->thread_pointer == thread_pointer)
	{
	  return error;
	}
    }
  struct VdlError * error = vdl_alloc_new (struct VdlError);
  error->thread_pointer = thread_pointer;
  error->error = 0;
  error->prev_error = 0;
  vdl_list_push_back (g_vdl.errors, error);
  return error;
}

static void set_error (const char *str, ...)
{
  va_list list;
  va_start (list, str);
  char *error_string = vdl_utils_vprintf (str, list);
  va_end (list);
  struct VdlError *error = find_error ();
  vdl_alloc_free (error->prev_error);
  vdl_alloc_free (error->error);
  error->prev_error = 0;
  error->error = error_string;
}

static struct VdlFile *
addr_to_file (unsigned long caller)
{
  struct VdlFile *cur;
  for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
    {
      void **i;
      for (i = vdl_list_begin (cur->maps); i != vdl_list_end (cur->maps); i = vdl_list_next (i))
	{
	  struct VdlFileMap *map = *i;
	  if (caller >= map->mem_start_align &&
	      caller <= map->mem_start_align + map->mem_size_align)
	    {
	      return cur;
	    }
	}
    }
  return 0;
}

static struct VdlContext *search_context (struct VdlContext *context)
{
  void **i;
  for (i = vdl_list_begin (g_vdl.contexts);
       i != vdl_list_end (g_vdl.contexts); 
       i = vdl_list_next (i))
    {
      if (context == *i)
	{
	  return context;
	}
    }
  set_error ("Can't find requested lmid %p", context);
  return 0;
}

static struct VdlFile *search_file (void *handle)
{
  struct VdlFile *cur;
  for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
    {
      if (cur == handle)
	{
	  return cur;
	}
    }
  set_error ("Can't find requested file 0x%x", handle);
  return 0;
}

static ElfW(Sym) *update_match (unsigned long addr,
				struct VdlFile *file,
				ElfW(Sym) *candidate, ElfW(Sym) *match)
{
  if (ELFW_ST_BIND (candidate->st_info) != STB_WEAK &&
      ELFW_ST_BIND (candidate->st_info) != STB_GLOBAL)
    {
      // not an acceptable match
      return match;
    }
  if (ELFW_ST_TYPE (candidate->st_info) == STT_TLS)
    {
      // tls symbols do not have an address
      return match;
    }
  if (candidate->st_shndx == SHN_UNDEF
      || candidate->st_value == 0)
    {
      // again, symbol does not have an address
      return match;
    }
  unsigned long start = file->load_base + candidate->st_value;
  unsigned long end = start + candidate->st_size;
  if (addr < start || addr >= end)
    {
      // address does not match
      return match;
    }
  // this symbol includes the target address
  // is it better than the current match ?
  if (match != 0 || (match->st_size < candidate->st_size))
    {
      // not better.
      return match;
    }
  return candidate;
}


// assumes caller has lock
static void *dlopen_with_context (struct VdlContext *context, const char *filename, int flags)
{
  VDL_LOG_FUNCTION ("filename=%s, flags=0x%x", filename, flags);

  if (filename == 0)
    {
      void **cur;
      for (cur = vdl_list_begin (context->global_scope); 
	   cur != vdl_list_end (context->global_scope); 
	   cur = vdl_list_next (cur))
	{
	  struct VdlFile *item = *cur;
	  if (item->is_executable)
	    {
	      item->count++;
	      return *cur;
	    }
	}
      VDL_LOG_ASSERT (false, "Could not find main executable within linkmap");
    }

  struct VdlMapResult map = vdl_map_from_filename (context, filename);
  if (map.requested == 0)
    {
      VDL_LOG_DEBUG ("Unable to load requested %s: %s", filename, map.error_string);
      set_error ("Unable to load: \"%s\"", filename);
      goto error;
    }

  if (flags & __RTLD_OPENEXEC)
    {
      map.requested->is_executable = 1;
    }

  /* from _dl_map_object_from_fd() of glibc/elf/dl-load.c (glibc-2.20) */
  /* This object is loaded at a fixed address.  This must never
     happen for objects loaded with dlopen.  */
  if ((map.requested->e_type != ET_DYN) &&
      (map.requested->is_executable == 0))
    {
      VDL_LOG_DEBUG ("Unable to load requested %s: %s", filename, map.error_string);
      set_error ("Unable to load: \"%s\"", filename);
      goto error;
    }

  bool ok = vdl_tls_file_initialize (map.newly_mapped);

  if (!ok)
    {
      // damn-it, one of the files we loaded
      // has indeed a static tls block. we don't know
      // how to handle them because that would require
      // adding space to the already-allocated static tls
      // which, by definition, can't be deallocated.
      set_error ("Attempting to dlopen a file with a static tls block which is bigger than the space available");
      goto error;
    }

  // from now on, no errors are possible.

  map.requested->count++;

  struct VdlList *scope = vdl_sort_deps_breadth_first (map.requested);
  if (flags & RTLD_GLOBAL)
    {
      // add this object as well as its dependencies to the global scope.
      // Note that it's not a big deal if the file has already been
      // added to the global scope in the past. We call unicize so
      // any duplicate entries appended here will be removed immediately.
      vdl_list_insert_range (context->global_scope,
			     vdl_list_end (context->global_scope),
			     vdl_list_begin (scope),
			     vdl_list_end (scope));
      vdl_list_unicize (context->global_scope);
    }

  // setup the local scope of each newly-loaded file.
  void **cur;
  for (cur = vdl_list_begin (map.newly_mapped); 
       cur != vdl_list_end (map.newly_mapped); 
       cur = vdl_list_next (cur))
    {
      struct VdlFile *item = *cur;
      vdl_list_insert_range (item->local_scope,
			     vdl_list_end (item->local_scope),
			     vdl_list_begin (scope),
			     vdl_list_end (scope));
      if (flags & RTLD_DEEPBIND)
	{
	  item->lookup_type = FILE_LOOKUP_LOCAL_GLOBAL;
	}
      else
	{
	  item->lookup_type = FILE_LOOKUP_GLOBAL_LOCAL;
	}
    }
  vdl_list_delete (scope);


  vdl_reloc (map.newly_mapped, g_vdl.bind_now || flags & RTLD_NOW);

  vdl_linkmap_append_range (vdl_list_begin (map.newly_mapped),
			    vdl_list_end (map.newly_mapped));

  // now, we want to update the dtv of _this_ thread.
  // i.e., we can't touch the dtv of the other threads 
  // because of locking issues so, if the code we loaded
  // uses the tls direct model to access the static block
  // and if any of the other threads try to call in this code
  // and if it tries to access the static tls block directly,
  // BOOOOM. nasty. anyway, we protect the caller if it tries to
  // access these tls static blocks by updating the dtv forcibly here
  // this indirectly initializes the content of the tls static area.
  vdl_tls_dtv_update ();

  gdb_notify ();

  glibc_patch (map.newly_mapped);

  struct VdlList *call_init = vdl_sort_call_init (map.newly_mapped);

  // we need to release the lock before calling the initializers 
  // to avoid a deadlock if one of them calls dlopen or
  // a symbol resolution function
  futex_unlock (g_vdl.futex);
  vdl_init_call (call_init);
  futex_lock (g_vdl.futex);

  // must hold the lock to call free
  vdl_list_delete (call_init);
  vdl_list_delete (map.newly_mapped);

  return map.requested;

 error:
  {
    // we don't need to call_fini here because we have not yet
    // called call_init.
    struct VdlGcResult gc = vdl_gc_run ();

    vdl_tls_file_deinitialize (gc.unload);

    vdl_unmap (gc.unload, true);

    vdl_list_delete (gc.unload);
    vdl_list_delete (gc.not_unload);

    gdb_notify ();
  }
  return 0;
}
void *vdl_dlopen (const char *filename, int flags)
{
  VDL_LOG_FUNCTION ("filename=%s", filename);
  futex_lock (g_vdl.futex);
  // map it in memory using the normal context, that is, the
  // first context in the context list.
  struct VdlContext *context = vdl_list_front (g_vdl.contexts);
  void *handle = dlopen_with_context (context, filename, flags);
  futex_unlock (g_vdl.futex);
  return handle;
}

void *vdl_dlsym (void *handle, const char *symbol, unsigned long caller)
{
  VDL_LOG_FUNCTION ("handle=0x%llx, symbol=%s, caller=0x%llx", handle, symbol, caller);
  return vdl_dlvsym (handle, symbol, 0, caller);
}

static void 
remove_from_scopes (struct VdlList *files, struct VdlFile *file)
{
  // remove from the local scope maps of all
  // those who have potentially a reference to us
  void **cur;
  for (cur = vdl_list_begin (files); 
       cur != vdl_list_end (files); 
       cur = vdl_list_next (cur))
    {
      struct VdlFile *item = *cur;
      vdl_list_remove (item->local_scope, file);
    }  

  // finally, remove from the global scope map
  vdl_list_remove (file->context->global_scope, file);
}

int vdl_dlclose (void *handle)
{
  VDL_LOG_FUNCTION ("handle=0x%llx", handle);
  futex_lock (g_vdl.futex);

  struct VdlFile *file = search_file (handle);
  if (file == 0)
    {
      futex_unlock (g_vdl.futex);
      return -1;
    }
  file->count--;

  // first, we gather the list of all objects to unload/delete
  struct VdlGcResult gc = vdl_gc_run ();

  // Then, we clear them from the scopes of all other files. 
  // so that no one can resolve symbols within them but they 
  // can resolve symbols among themselves and into others.
  // It's obviously important to do this before calling the 
  // finalizers
  {
    void **cur;
    for (cur = vdl_list_begin (gc.unload); 
	 cur != vdl_list_end (gc.unload); 
	 cur = vdl_list_next (cur))
      {
	remove_from_scopes (gc.not_unload, *cur);
      }
  }

  struct VdlList *call_fini = vdl_sort_call_fini (gc.unload);
  struct VdlList *locked = vdl_fini_lock(call_fini);
  vdl_list_delete(call_fini);
  call_fini = locked;

  // must not hold the lock to call fini
  futex_unlock (g_vdl.futex);
  vdl_fini_call (call_fini);
  futex_lock (g_vdl.futex);

  vdl_tls_file_deinitialize (call_fini);

  // update the linkmap before unmapping
  vdl_linkmap_remove_range (vdl_list_begin (call_fini),
			    vdl_list_end (call_fini));

  // now, unmap
  vdl_unmap (call_fini, true);

  vdl_list_delete (call_fini);
  vdl_list_delete (gc.unload);
  vdl_list_delete (gc.not_unload);

  gdb_notify ();

  futex_unlock (g_vdl.futex);
  return 0;
}

char *vdl_dlerror (void)
{
  VDL_LOG_FUNCTION ("", 0);
  futex_lock (g_vdl.futex);
  struct VdlError *error = find_error ();
  char *error_string = error->error;
  vdl_alloc_free (error->prev_error);
  error->prev_error = error->error;
  // clear the error we are about to report to the user
  error->error = 0;
  futex_unlock (g_vdl.futex);
  return error_string;
}

int vdl_dladdr1 (const void *addr, Dl_info *info, 
		 void **extra_info, int flags)
{
  VDL_LOG_FUNCTION ("", 0);
  futex_lock (g_vdl.futex);
  struct VdlFile *file = addr_to_file ((unsigned long)addr);
  if (file == 0)
    {
      set_error ("No object contains 0x%lx", addr);
      goto error;
    }
  if (info == 0)
    {
      set_error ("Invalid input data: null info pointer");
      goto error;
    }
  // ok, we have a containing object file
  if (vdl_utils_strisequal (file->filename, "") &&
      file->is_executable)
    {
      // This is the main executable
      info->dli_fname = file->name;
    }
  else
    {
      info->dli_fname = file->filename;
    }
  info->dli_fbase = (void*)file->load_base;
  if (flags == RTLD_DL_LINKMAP)
    {
      struct link_map **plink_map = (struct link_map **)extra_info;
      *plink_map = (struct link_map *)file;
    }


  // now, we try to find the closest symbol
  // For this, we simply iterate over the symbol table of the file.
  ElfW(Sym) *match = 0;
  const char *dt_strtab = file->dt_strtab;
  ElfW(Sym) *dt_symtab = file->dt_symtab;
  ElfW(Word) *dt_hash = file->dt_hash;
  uint32_t *dt_gnu_hash = file->dt_gnu_hash;
  if (dt_symtab != 0 && dt_strtab != 0)
    {
      if (dt_hash != 0)
	{
	  // this is a standard elf hash table
	  // the number of symbol table entries is equal to the number of hash table
	  // chain entries which is indicated by nchain
	  ElfW(Word) nchain = dt_hash[1];
	  ElfW(Word) i;
	  for (i = 0; i < nchain; i++)
	    {
	      ElfW(Sym) *cur = &dt_symtab[i];
	      match = update_match ((unsigned long)addr, file, cur, match);
	    }
	}
      if (dt_gnu_hash != 0)
	{
	  // this is a gnu hash table.
	  uint32_t nbuckets = dt_gnu_hash[0];
	  uint32_t symndx = dt_gnu_hash[1];
	  uint32_t maskwords = dt_gnu_hash[2];
	  ElfW(Addr) *bloom = (ElfW(Addr)*)(dt_gnu_hash + 4);
	  uint32_t *buckets = (uint32_t *)(((unsigned long)bloom) + maskwords * sizeof (ElfW(Addr)));
	  uint32_t *chains = &buckets[nbuckets];

	  // first, iterate over all buckets in the hash table
	  uint32_t i;
	  for (i = 0; i < nbuckets; i++)
	    {
	      if (buckets[i] == 0)
		{
		  continue;
		}
	      // now, iterate over the chain of this bucket
	      uint32_t j = buckets[i];
	      do {
		match = update_match ((unsigned long)addr, file, &dt_symtab[j], match);
		j++;
	      } while ((chains[j-symndx] & 0x1) != 0x1);
	    }
	}
    }

  // ok, now we finally set the fields of the info structure 
  // from the result of the symbol lookup.
  if (match == 0)
    {
      info->dli_sname = 0;
      info->dli_saddr = 0;
    }
  else
    {
      info->dli_sname = dt_strtab + match->st_name;
      info->dli_saddr = (void*)(file->load_base + match->st_value);
    }
  if (flags == RTLD_DL_SYMENT)
    {
      const ElfW(Sym) **sym = (const ElfW(Sym) **)extra_info;
      *sym = match;
    }
  else 
  futex_unlock (g_vdl.futex);
  return 1;
 error:
  futex_unlock (g_vdl.futex);
  return 0;
}
int vdl_dladdr (const void *addr, Dl_info *info)
{
  return vdl_dladdr1 (addr, info, 0, 0);
}
void *vdl_dlvsym (void *handle, const char *symbol, const char *version, 
		  unsigned long caller)
{
  return vdl_dlvsym_with_flags (handle, symbol, version, 0, caller);
}
void *vdl_dlvsym_with_flags (void *handle, const char *symbol, const char *version, 
			     unsigned long flags,
			     unsigned long caller)
{
  VDL_LOG_FUNCTION ("handle=0x%llx, symbol=%s, version=%s, caller=0x%llx", 
		    handle, symbol, (version==0)?"":version, caller);
  futex_lock (g_vdl.futex);
  struct VdlList *scope;
  struct VdlFile *caller_file = addr_to_file (caller);
  struct VdlContext *context;
  if (caller_file == 0)
    {
      set_error ("Can't find caller");
      goto error;
    }
  if (handle == RTLD_DEFAULT)
    {
      scope = vdl_list_new ();
      vdl_list_insert_range (scope,
			     vdl_list_begin (scope),
			     vdl_list_begin (caller_file->context->global_scope),
			     vdl_list_end (caller_file->context->global_scope));
      context = caller_file->context;
    }
  else if (handle == RTLD_NEXT)
    {
      context = caller_file->context;
      // skip all objects before the caller object
      void **cur = vdl_list_find (caller_file->context->global_scope,
				  caller_file);
      if (cur != vdl_list_end (caller_file->context->global_scope))
	{
	  // go to the next object
	  scope = vdl_list_new ();
	  vdl_list_insert_range (scope,
				 vdl_list_end (scope),
				 vdl_list_next (cur),
				 vdl_list_end (caller_file->context->global_scope));
	}
      else
	{
	  set_error ("Can't find caller in current local scope");
	  goto error;
	}
    }
  else
    {
      struct VdlFile *file = search_file (handle);
      if (file == 0)
	{
	  goto error;
	}
      scope = vdl_sort_deps_breadth_first (file);
      context = file->context;
    }
  struct VdlLookupResult result;
  
  result = vdl_lookup_with_scope (context, symbol, version, 0, flags, scope);
  if (!result.found)
    {
      set_error ("Could not find requested symbol \"%s\"", symbol);
      goto error;
    }
  vdl_list_delete (scope);
  futex_unlock (g_vdl.futex);
  return (void*)(result.file->load_base + result.symbol->st_value);
 error:
  vdl_list_delete (scope);
  futex_unlock (g_vdl.futex);
  return 0;
}
int vdl_dl_iterate_phdr (int (*callback) (struct dl_phdr_info *info,
						  size_t size, void *data),
			 void *data,
			 unsigned long caller)
{
  VDL_LOG_FUNCTION ("", 0);
  int ret = 0;
  futex_lock (g_vdl.futex);
  struct VdlFile *file = addr_to_file (caller);

  // report all objects within the global scope/context of the caller
  void **cur;
  for (cur = vdl_list_begin (file->context->global_scope); 
       cur != vdl_list_end (file->context->global_scope); 
       cur = vdl_list_next (cur))
    {
      struct VdlFile *item = *cur;
      struct dl_phdr_info info;
      info.dlpi_addr = item->load_base;
      info.dlpi_name = item->name;
      info.dlpi_phdr = item->phdr;
      info.dlpi_phnum = item->phnum;
      info.dlpi_adds = g_vdl.n_added;
      info.dlpi_subs = g_vdl.n_removed;
      if (item->has_tls)
	{
	  info.dlpi_tls_modid = item->tls_index;
	  info.dlpi_tls_data = (void*)vdl_tls_get_addr_fast (item->tls_index, 0);
	}
      else
	{
	  info.dlpi_tls_modid = 0;
	  info.dlpi_tls_data = 0;
	}
      futex_unlock (g_vdl.futex);
      ret = callback (&info, sizeof (struct dl_phdr_info), data);
      futex_lock (g_vdl.futex);
      if (ret != 0)
	{
	  break;
	}
    }
  futex_unlock (g_vdl.futex);
  return ret;
}
void *vdl_dlmopen (Lmid_t lmid, const char *filename, int flag)
{
  VDL_LOG_FUNCTION ("", 0);
  futex_lock (g_vdl.futex);
  struct VdlContext *context;
  if (lmid == LM_ID_BASE)
    {
      context = vdl_list_front (g_vdl.contexts);
    }
  else if (lmid == LM_ID_NEWLM)
    {
      context = vdl_list_front (g_vdl.contexts);
      context = vdl_context_new (context->argc,
				 context->argv,
				 context->envp);
    }
  else
    {
      context = (struct VdlContext *) lmid;
      if (search_context (context) == 0)
	{
	  return 0;
	}
    }
  void *handle = dlopen_with_context (context, filename, flag);
  futex_unlock (g_vdl.futex);
  return handle;
}
int vdl_dlinfo (void *handle, int request, void *p)
{
  VDL_LOG_FUNCTION ("", 0);
  futex_lock (g_vdl.futex);
  struct VdlFile *file = search_file (handle);
  if (file == 0)
    {
      goto error;
    }
  if (request == RTLD_DI_LMID)
    {
      Lmid_t *plmid = (Lmid_t*)p;
      *plmid = (Lmid_t)file->context;
    }
  else if (request == RTLD_DI_LINKMAP)
    {
      struct link_map **pmap = (struct link_map **)p;
      *pmap = (struct link_map*) file;
    }
  else if (request == RTLD_DI_TLS_MODID)
    {
      size_t *pmodid = (size_t *)p;
      if (file->has_tls)
	{
	  *pmodid = file->tls_index;
	}
      else
	{
	  *pmodid = 0;
	}
    }
  else if (request == RTLD_DI_TLS_DATA)
    {
      void **ptls = (void**)p;
      if (file->has_tls)
	{
	  *ptls = (void*)vdl_tls_get_addr_fast (file->tls_index, 0);
	}
      else
	{
	  *ptls = 0;
	}
    }
  else if (request ==  RTLD_DI_TLS_SIZE)
    {
      *(unsigned long *)p = g_vdl.tls_static_current_size;
    }
  else
    {
      set_error ("dlinfo: unsupported request=%u", request);
      goto error;
    }
  
  futex_unlock (g_vdl.futex);
  return 0;
 error:
  futex_unlock (g_vdl.futex);
  return -1;
}
Lmid_t vdl_dl_lmid_new (int argc, char **argv, char **envp)
{
  VDL_LOG_FUNCTION ("", 0);
  futex_lock (g_vdl.futex);
  struct VdlContext *context = vdl_context_new (argc, argv, envp);
  Lmid_t lmid = (Lmid_t) context;
  futex_unlock (g_vdl.futex);
  return lmid;
}
void vdl_dl_lmid_delete (Lmid_t lmid)
{
  VDL_LOG_FUNCTION ("", 0);
  futex_lock (g_vdl.futex);
  struct VdlContext *context = (struct VdlContext *) lmid;
  if (search_context (context) == 0)
    {
      goto out;
    }
  if (vdl_list_empty (context->loaded))
    {
      vdl_context_delete (context);
      goto out;
    }
  // XXX: why do we do this here ?
  vdl_tls_file_deinitialize (context->loaded);

  // update the linkmap before unmapping
  vdl_linkmap_remove_range (vdl_list_begin (context->loaded),
			    vdl_list_end (context->loaded));
  // need to make a copy because the context might disappear from
  // under our feet while we unmap if we unmap its remaining files.
  struct VdlList *copy = vdl_list_copy (context->loaded);
  vdl_unmap (copy, true);
  vdl_list_delete (copy);

  // no need to call vdl_context_delete because the last file
  // to be unmapped by vdl_unmap will trigger the deletion of
  // the associated context.

  gdb_notify ();
 out:
  futex_unlock (g_vdl.futex);
}
int vdl_dl_lmid_add_callback (Lmid_t lmid, 
			      void (*cb) (void *handle, int event, void *context),
			      void *cb_context)
{
  VDL_LOG_FUNCTION ("", 0);
  futex_lock (g_vdl.futex);
  struct VdlContext *context = (struct VdlContext *)lmid;
  if (search_context (context) == 0)
    {
      goto error;
    }
  vdl_context_add_callback (context, 
			    (void (*) (void *, enum VdlEvent, void *))cb, 
			    cb_context);
  futex_unlock (g_vdl.futex);
  return 0;
 error:
  futex_unlock (g_vdl.futex);
  return -1;
}
int
vdl_dl_lmid_add_lib_remap (Lmid_t lmid, const char *src, const char *dst)
{
  VDL_LOG_FUNCTION ("", 0);
  futex_lock (g_vdl.futex);
  struct VdlContext *context = (struct VdlContext *)lmid;
  if (search_context (context) == 0)
    {
      goto error;
    }
  vdl_context_add_lib_remap (context, src, dst);
  futex_unlock (g_vdl.futex);
  return 0;
 error:
  futex_unlock (g_vdl.futex);
  return -1;
}
int vdl_dl_lmid_add_symbol_remap (Lmid_t lmid,
				  const char *src_name, 
				  const char *src_ver_name, 
				  const char *src_ver_filename, 
				  const char *dst_name,
				  const char *dst_ver_name,
				  const char *dst_ver_filename)
{
  VDL_LOG_FUNCTION ("", 0);
  futex_lock (g_vdl.futex);
  struct VdlContext *context = (struct VdlContext *)lmid;
  if (search_context (context) == 0)
    {
      goto error;
    }
  vdl_context_add_symbol_remap (context,
				src_name, src_ver_name, src_ver_filename,
				dst_name, dst_ver_name, dst_ver_filename);
  futex_unlock (g_vdl.futex);
  return 0;
 error:
  futex_unlock (g_vdl.futex);
  return -1;
}
