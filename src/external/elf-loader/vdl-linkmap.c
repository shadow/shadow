#include "vdl-linkmap.h"
#include "vdl-list.h"
#include "vdl.h"
#include "vdl-file.h"
#include "vdl-log.h"
#include "vdl-hashmap.h"
#include "vdl-utils.h"
#include "futex.h"

void
vdl_linkmap_append (struct VdlFile *file)
{
  if (file->in_linkmap)
    {
      return;
    }
  uint32_t hash = vdl_int_hash ((unsigned long) file);
  vdl_hashmap_insert (g_vdl.files, hash, file);
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
  g_vdl.n_added++;
}

void
vdl_linkmap_append_range (struct VdlList *list, void **begin, void **end)
{
  void **i;
  write_lock (g_vdl.link_map_lock);
  for (i = begin; i != end; i = vdl_list_next (list, i))
    {
      vdl_linkmap_append (*i);
    }
  write_unlock (g_vdl.link_map_lock);
}

static void
vdl_linkmap_remove_internal (struct VdlFile *file)
{
  // first, remove them from the global link_map
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
  if (file->has_tls)
    {
      vdl_hashmap_remove (g_vdl.module_map, file->tls_index, file);
    }
  uint32_t hash = vdl_int_hash ((unsigned long) file);
  vdl_hashmap_remove (g_vdl.files, hash, file);
  g_vdl.n_removed++;
}

void
vdl_linkmap_remove (struct VdlFile *file)
{
  write_lock (g_vdl.link_map_lock);
  vdl_linkmap_remove_internal (file);
  write_unlock (g_vdl.link_map_lock);
}

void
vdl_linkmap_remove_range (struct VdlList *list, void **begin, void **end)
{
  void **i;
  write_lock (g_vdl.link_map_lock);
  for (i = begin; i != end; i = vdl_list_next (list, i))
    {
      vdl_linkmap_remove_internal (*i);
    }
  write_unlock (g_vdl.link_map_lock);
}

struct VdlList *
vdl_linkmap_copy (void)
{
  struct VdlList *list = vdl_list_new ();
  struct VdlFile *cur;
  read_lock (g_vdl.link_map_lock);
  for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
    {
      vdl_list_push_back (list, cur);
    }
  read_unlock (g_vdl.link_map_lock);
  return list;
}

void
vdl_linkmap_print (void)
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
