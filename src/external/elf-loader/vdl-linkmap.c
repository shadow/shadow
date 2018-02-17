#include "vdl-linkmap.h"
#include "vdl-list.h"
#include "vdl.h"
#include "vdl-file.h"
#include "vdl-log.h"
#include "vdl-hashmap.h"
#include "vdl-utils.h"
#include "futex.h"
#include "gdb.h"

static void
vdl_linkmap_abi_append (void *data)
{
  struct VdlFile *file = data;
  if (file->in_linkmap)
    {
      return;
    }
  file->in_linkmap = 1;
  if (g_vdl.link_map == 0)
    {
      g_vdl.link_map = file;
      g_vdl.link_map_tail = file;
      return;
    }
  g_vdl.link_map_tail->next = file;
  file->prev = g_vdl.link_map_tail;
  g_vdl.link_map_tail = file;
  file->next = 0;
}

// afaik, gdb doesn't do anything special on removal,
// so we can remove from both linkmaps at the same time
static void
vdl_linkmap_remove_internal (void *data)
{
  struct VdlFile *file = data;
  // first, remove it from the shadow linkmap and other structures
  vdl_list_remove (g_vdl.shadow_link_map, file);
  file->in_shadow_linkmap = 0;
  if (file->has_tls)
    {
      vdl_hashmap_remove (g_vdl.module_map, file->tls_index, file);
    }
  uint32_t hash = vdl_int_hash ((unsigned long) file);
  vdl_hashmap_remove (g_vdl.files, hash, file);
  g_vdl.n_removed++;
  if (!file->in_linkmap)
    {
      return;
    }

  // it was put in the ABI linkmap, remove from that too
  struct VdlFile *next = file->next;
  struct VdlFile *prev = file->prev;
  file->next = 0;
  file->prev = 0;
  file->in_linkmap = 0;
  if (prev == 0)
    {
      g_vdl.link_map = next;
    }
  else
    {
      prev->next = next;
    }
  if (next != 0)
    {
      next->prev = prev;
    }
  else
    {
      g_vdl.link_map_tail = prev;
    }
}

void
vdl_linkmap_remove (struct VdlFile *file)
{
  write_lock (g_vdl.link_map_lock);
  vdl_linkmap_remove_internal (file);
  gdb_notify ();
  write_unlock (g_vdl.link_map_lock);
}

void
vdl_linkmap_remove_list (struct VdlList *list)
{
  write_lock (g_vdl.link_map_lock);
  vdl_list_iterate (list, vdl_linkmap_remove_internal);
  gdb_notify ();
  write_unlock (g_vdl.link_map_lock);
}

struct VdlList *
vdl_linkmap_copy (void)
{
  return vdl_list_copy(g_vdl.shadow_link_map);
}

void
vdl_linkmap_abi_update (void)
{
  write_lock (g_vdl.link_map_lock);
  vdl_list_iterate (g_vdl.shadow_link_map, vdl_linkmap_abi_append);
  gdb_notify ();
  write_unlock (g_vdl.link_map_lock);
}

void
vdl_linkmap_append (struct VdlFile *file)
{
  if (file->in_shadow_linkmap)
    {
      return;
    }
  uint32_t hash = vdl_int_hash ((unsigned long) file);
  vdl_hashmap_insert (g_vdl.files, hash, file);
  vdl_list_push_back (g_vdl.shadow_link_map, file);
  file->in_shadow_linkmap = 1;
  __sync_fetch_and_add (&g_vdl.n_added, 1);
}

int
vdl_linkmap_append_iterator (void *data)
{
  struct VdlFile *file = data;
  if (file->in_shadow_linkmap)
    {
      return 0;
    }
  uint32_t hash = vdl_int_hash ((unsigned long) file);
  vdl_hashmap_insert (g_vdl.files, hash, file);
  file->in_shadow_linkmap = 1;
  __sync_fetch_and_add (&g_vdl.n_added, 1);
  return 1;
}

void
vdl_linkmap_append_list (struct VdlList *list)
{
  struct VdlList *needed = vdl_list_get_all (list, vdl_linkmap_append_iterator);
  vdl_list_append_list (g_vdl.shadow_link_map, needed);
  vdl_list_delete (needed);
}


static void
vdl_linkmap_shadow_print_iterator (void *data)
{
  struct VdlFile *file = data;
  vdl_log_printf (VDL_LOG_PRINT,
                  "load_base=0x%x , file=%s\n",
                  file->load_base, file->filename);
}

// these are intended for debugging (e.g., you can call them from gdb)
// don't commit any code that calls them

void
vdl_linkmap_shadow_print (void)
{
  vdl_list_iterate (g_vdl.shadow_link_map, vdl_linkmap_shadow_print_iterator);
}

void
vdl_linkmap_abi_print (void)
{
  struct VdlFile *cur;
  read_lock (g_vdl.link_map_lock);
  for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
    {
      vdl_log_printf (VDL_LOG_PRINT,
                      "load_base=0x%x , file=%s\n",
                      cur->load_base, cur->filename);
    }
  read_unlock (g_vdl.link_map_lock);
}
