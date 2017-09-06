#include "vdl-context.h"
#include "vdl.h"
#include "vdl-utils.h"
#include "vdl-alloc.h"
#include "vdl-log.h"
#include "vdl-unmap.h"
#include "vdl-hashmap.h"
#include "futex.h"

bool
vdl_context_empty (const struct VdlContext *context)
{
  return vdl_list_empty (context->loaded);
}

void
vdl_context_add_lib_remap (struct VdlContext *context,
                           const char *src, const char *dst)
{
  struct VdlContextLibRemapEntry *entry =
    vdl_alloc_new (struct VdlContextLibRemapEntry);
  entry->src = vdl_utils_strdup (src);
  entry->dst = vdl_utils_strdup (dst);
  vdl_list_push_back (context->lib_remaps, entry);
}

void
vdl_context_add_symbol_remap (struct VdlContext *context,
                              const char *src_name,
                              const char *src_ver_name,
                              const char *src_ver_filename,
                              const char *dst_name,
                              const char *dst_ver_name,
                              const char *dst_ver_filename)
{
  struct VdlContextSymbolRemapEntry *entry =
    vdl_alloc_new (struct VdlContextSymbolRemapEntry);
  entry->src_name = vdl_utils_strdup (src_name);
  entry->src_ver_name = vdl_utils_strdup (src_ver_name);
  entry->src_ver_filename = vdl_utils_strdup (src_ver_filename);
  entry->dst_name = vdl_utils_strdup (dst_name);
  entry->dst_ver_name = vdl_utils_strdup (dst_ver_name);
  entry->dst_ver_filename = vdl_utils_strdup (dst_ver_filename);
  vdl_list_push_back (context->symbol_remaps, entry);
}

void
vdl_context_add_callback (struct VdlContext *context,
                          void (*cb) (void *handle, enum VdlEvent event,
                                      void *context), void *cb_context)
{
  struct VdlContextEventCallbackEntry *entry =
    vdl_alloc_new (struct VdlContextEventCallbackEntry);
  entry->fn = cb;
  entry->context = cb_context;
  vdl_list_push_back (context->event_callbacks, entry);
}

void
vdl_context_notify (struct VdlContext *context,
                    struct VdlFile *file, enum VdlEvent event)
{
  void **i;
  for (i = vdl_list_begin (context->event_callbacks);
       i != vdl_list_end (context->event_callbacks);
       i = vdl_list_next (context->event_callbacks, i))
    {
      struct VdlContextEventCallbackEntry *item = *i;
      item->fn (file, event, item->context);
    }
}


const char *
vdl_context_lib_remap (const struct VdlContext *context, const char *name)
{
  VDL_LOG_FUNCTION ("name=%s", name);
  void **i;
  for (i = vdl_list_begin (context->lib_remaps);
       i != vdl_list_end (context->lib_remaps);
       i = vdl_list_next (context->lib_remaps, i))
    {
      struct VdlContextLibRemapEntry *item = *i;
      if (vdl_utils_strisequal (item->src, name))
        {
          return item->dst;
        }
    }
  return name;
}

void
vdl_context_symbol_remap (const struct VdlContext *context,
                          const char **name, const char **ver_name,
                          const char **ver_filename)
{
  VDL_LOG_FUNCTION ("name=%s, ver_name=%s, ver_filename=%s", *name,
                    (ver_name != 0 && *ver_name != 0) ? *ver_name : "",
                    (ver_filename != 0
                     && *ver_filename != 0) ? *ver_filename : "");
  void **i;
  struct VdlContextSymbolRemapEntry *item;
  for (i = vdl_list_begin (context->symbol_remaps);
       i != vdl_list_end (context->symbol_remaps);
       i = vdl_list_next (context->symbol_remaps, i))
    {
      item = *i;
      if (!vdl_utils_strisequal (item->src_name, *name))
        {
          continue;
        }
      else if (item->src_ver_name == 0)
        {
          goto match;
        }
      else if (*ver_name == 0)
        {
          continue;
        }
      else if (!vdl_utils_strisequal (item->src_ver_name, *ver_name))
        {
          continue;
        }
      else if (item->src_ver_filename == 0)
        {
          goto match;
        }
      else if (*ver_filename == 0)
        {
          continue;
        }
      else if (vdl_utils_strisequal (item->src_ver_filename, *ver_filename))
        {
          goto match;
        }
    }
  return;
match:
  *name = item->dst_name;
  if (ver_name != 0)
    {
      *ver_name = item->dst_ver_name;
    }
  if (ver_filename != 0)
    {
      *ver_filename = item->dst_ver_filename;
    }
  return;
}

struct VdlContext *
vdl_context_new (int argc, char **argv, char **envp)
{
  VDL_LOG_FUNCTION ("argc=%d", argc);

  struct VdlContext *context = vdl_alloc_new (struct VdlContext);

  context->lock = rwlock_new ();
  context->loaded = vdl_list_new ();
  context->lib_remaps = vdl_list_new ();
  context->symbol_remaps = vdl_list_new ();
  context->event_callbacks = vdl_list_new ();
  // keep a reference to argc, argv and envp.
  context->argc = argc;
  context->argv = argv;
  context->envp = envp;

  // Store the files from LD_PRELOAD and RTLD_PRELOAD in all contexts.
  // Note that this insertion is of the loaded files as is, not a reloading.
  // Therefore, all symbols found in these files or from these files will be
  // in the context they were originally loaded in, and _not_ this newly
  // created context. (LD_PRELOAD files are loaded in the default context.)
  context->global_scope = vdl_list_copy (g_vdl.preloads);
  context->has_main = 0;

  // these are hardcoded name conversions to ensure that
  // we can replace the libc loader.
  vdl_context_add_lib_remap (context, "/lib/ld-linux.so.2", "ldso");
  vdl_context_add_lib_remap (context, "/lib64/ld-linux-x86-64.so.2", "ldso");
  vdl_context_add_lib_remap (context, "ld-linux.so.2", "ldso");
  vdl_context_add_lib_remap (context, "ld-linux-x86-64.so.2", "ldso");
  vdl_context_add_lib_remap (context, "libdl.so.2", "libvdl.so");
  vdl_context_add_symbol_remap (context,
                                "dl_iterate_phdr", 0, 0,
                                "vdl_dl_iterate_phdr_public", "VDL_DL",
                                "ldso");
  uint32_t hash = vdl_int_hash ((unsigned long) context);
  vdl_hashmap_insert (g_vdl.contexts, hash, context);

  return context;
}

void
vdl_context_delete (struct VdlContext *context)
{
  VDL_LOG_FUNCTION ("context=%p", context);
  // get rid of associated global scope
  vdl_list_delete (context->global_scope);
  context->global_scope = 0;

  vdl_list_delete (context->loaded);
  context->loaded = 0;

  uint32_t hash = vdl_int_hash ((unsigned long) context);
  vdl_hashmap_remove (g_vdl.contexts, hash, context);
  context->argc = 0;
  context->argv = 0;
  context->envp = 0;

  {
    void **i;
    for (i = vdl_list_begin (context->lib_remaps);
         i != vdl_list_end (context->lib_remaps);
         i = vdl_list_next (context->lib_remaps, i))
      {
        struct VdlContextLibRemapEntry *item = *i;
        vdl_alloc_free (item->src);
        vdl_alloc_free (item->dst);
        vdl_alloc_free (item);
      }
    vdl_list_delete (context->lib_remaps);
  }

  {
    void **i;
    for (i = vdl_list_begin (context->symbol_remaps);
         i != vdl_list_end (context->symbol_remaps);
         i = vdl_list_next (context->symbol_remaps, i))
      {
        struct VdlContextSymbolRemapEntry *item = *i;
        vdl_alloc_free (item->src_name);
        vdl_alloc_free (item->src_ver_name);
        vdl_alloc_free (item->src_ver_filename);
        vdl_alloc_free (item->dst_name);
        vdl_alloc_free (item->dst_ver_name);
        vdl_alloc_free (item->dst_ver_filename);
        vdl_alloc_free (item);
      }
    vdl_list_delete (context->symbol_remaps);
  }

  {
    void **i;
    for (i = vdl_list_begin (context->event_callbacks);
         i != vdl_list_end (context->event_callbacks);
         i = vdl_list_next (context->event_callbacks, i))
      {
        struct VdlContextEventCallbackEntry *item = *i;
        vdl_alloc_delete (item);
      }
    vdl_list_delete (context->event_callbacks);
  }

  context->lib_remaps = 0;
  context->symbol_remaps = 0;
  context->event_callbacks = 0;

  rwlock_delete (context->lock);
  context->lock = 0;

  // finally, delete context itself
  vdl_alloc_delete (context);
}

void
vdl_context_add_file (struct VdlContext *context, struct VdlFile *file)
{
  vdl_list_push_back (context->loaded, file);
}

void
vdl_context_remove_file (struct VdlContext *context, struct VdlFile *file)
{
  vdl_list_remove (context->loaded, file);
}
