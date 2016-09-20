#include "vdl-unmap.h"
#include "vdl-context.h"
#include "vdl-file.h"
#include "vdl-utils.h"
#include "vdl-log.h"
#include "vdl-alloc.h"
#include "system.h"


static void
file_delete (struct VdlFile *file, bool mapping)
{
  vdl_context_remove_file (file->context, file);

  if (mapping)
    {
      void **i;
      for (i = vdl_list_begin (file->maps); i != vdl_list_end (file->maps); i = vdl_list_next (i))
	{
	  struct VdlFileMap *map = *i;
	  int status = system_munmap ((void*)map->mem_start_align, 
				      map->mem_size_align);
	  if (status == -1)
	    {
	      VDL_LOG_ERROR ("unable to unmap map 0x%lx[0x%lx] for \"%s\"\n", 
			     map->mem_start_align, map->mem_size_align,
			     file->filename);
	    }
	}
    }

  if (vdl_context_empty (file->context))
    {
      vdl_context_delete (file->context);
    }

  vdl_list_delete (file->deps);
  vdl_list_delete (file->local_scope);
  vdl_list_delete (file->gc_symbols_resolved_in);
  vdl_alloc_free (file->name);
  vdl_alloc_free (file->filename);
  vdl_alloc_free (file->phdr);
  vdl_list_iterate (file->maps, vdl_alloc_free);
  vdl_list_delete (file->maps);


  file->deps = 0;
  file->local_scope = 0;
  file->gc_symbols_resolved_in = 0;
  file->name = 0;
  file->filename = 0;
  file->context = 0;
  file->phdr = 0;
  file->phnum = 0;
  file->maps = 0;

  vdl_alloc_delete (file);
}

void vdl_unmap (struct VdlList *files, bool mapping)
{
  void **i;
  for (i = vdl_list_begin (files); i != vdl_list_end (files); i = vdl_list_next (i))
    {
      file_delete (*i, mapping);
    }
}

