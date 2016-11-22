#include "vdl-sort.h"
#include "vdl-list.h"
#include "vdl-utils.h"
#include "vdl-file.h"
#include <stdint.h>

static uint32_t
get_max_depth (struct VdlList *files)
{
  uint32_t max_depth = 0;
  void **cur;
  for (cur = vdl_list_begin (files);
       cur != vdl_list_end (files); cur = vdl_list_next (cur))
    {
      struct VdlFile *file = *cur;
      max_depth = vdl_utils_max (file->depth, max_depth);
    }
  return max_depth;
}

struct VdlList *
vdl_sort_increasing_depth (struct VdlList *files)
{
  uint32_t max_depth = get_max_depth (files);

  struct VdlList *output = vdl_list_new ();

  uint32_t i;
  for (i = 0; i <= max_depth; i++)
    {
      // find files with matching depth and output them
      void **cur;
      for (cur = vdl_list_begin (files);
           cur != vdl_list_end (files); cur = vdl_list_next (cur))
        {
          struct VdlFile *file = *cur;
          if (file->depth == i)
            {
              vdl_list_push_back (output, file);
            }
        }
    }
  return output;
}

struct VdlList *
vdl_sort_deps_breadth_first (struct VdlFile *file)
{
  struct VdlList *sorted = vdl_list_new ();
  vdl_list_push_back (sorted, file);

  void **i;
  for (i = vdl_list_begin (sorted);
       i != vdl_list_end (sorted); i = vdl_list_next (i))
    {
      struct VdlFile *item = *i;
      void **j;
      for (j = vdl_list_begin (item->deps);
           j != vdl_list_end (item->deps); j = vdl_list_next (j))
        {
          if (vdl_list_find (sorted, *j) == vdl_list_end (sorted))
            {
              // not found
              vdl_list_push_back (sorted, *j);
            }
        }
    }

  return sorted;
}

struct VdlList *
vdl_sort_call_init (struct VdlList *files)
{
  struct VdlList *sorted = vdl_sort_increasing_depth (files);
  vdl_list_reverse (sorted);
  return sorted;
}

struct VdlList *
vdl_sort_call_fini (struct VdlList *files)
{
  struct VdlList *sorted = vdl_sort_increasing_depth (files);
  return sorted;
}
