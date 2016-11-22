#include "vdl-gc.h"
#include "vdl-utils.h"
#include "vdl-list.h"
#include "vdl.h"
#include "vdl-log.h"
#include "vdl-linkmap.h"
#include "vdl-file.h"

enum
{
  VDL_GC_BLACK = 0,
  VDL_GC_GREY = 1,
  VDL_GC_WHITE = 2
};

// return the white subset of the input set of files
static struct VdlList *
vdl_gc_white_list_new (struct VdlList *list)
{
  struct VdlList *grey = vdl_list_new ();

  // perform the initial sweep: mark all objects as white
  // except for the roots which are marked as grey and keep
  // track of all roots.
  {
    void **cur;
    for (cur = vdl_list_begin (list); cur != vdl_list_end (list);
         cur = vdl_list_next (cur))
      {
        struct VdlFile *item = *cur;
        if (item->count > 0)
          {
            item->gc_color = VDL_GC_GREY;
            vdl_list_push_front (grey, item);
          }
        else
          {
            item->gc_color = VDL_GC_WHITE;
          }
      }
  }

  // for each element in the grey list, 'blacken' it by
  // marking grey all the objects it references.
  while (!vdl_list_empty (grey))
    {
      struct VdlFile *first = vdl_list_front (grey);
      vdl_list_pop_front (grey);
      void **cur;
      for (cur = vdl_list_begin (first->gc_symbols_resolved_in);
           cur != vdl_list_end (first->gc_symbols_resolved_in);
           cur = vdl_list_next (cur))
        {
          struct VdlFile *item = *cur;
          if (item->gc_color == VDL_GC_WHITE)
            {
              // move referenced objects which are white to the grey list.
              // by inserting them at the front of the list.
              item->gc_color = VDL_GC_GREY;
              vdl_list_push_front (grey, item);
            }
        }
      for (cur = vdl_list_begin (first->deps);
           cur != vdl_list_end (first->deps); cur = vdl_list_next (cur))
        {
          struct VdlFile *item = *cur;
          if (item->gc_color == VDL_GC_WHITE)
            {
              // move referenced objects which are white to the grey list.
              // by inserting them at the front of the list.
              item->gc_color = VDL_GC_GREY;
              vdl_list_push_front (grey, item);
            }
        }
      // finally, mark our grey object as black.
      first->gc_color = VDL_GC_BLACK;
    }
  vdl_list_delete (grey);

  // finally, gather the list of white objects.
  struct VdlList *white = vdl_list_new ();
  {
    void **cur;
    for (cur = vdl_list_begin (list); cur != vdl_list_end (list);
         cur = vdl_list_next (cur))
      {
        struct VdlFile *item = *cur;
        if (item->gc_color == VDL_GC_WHITE)
          {
            vdl_list_push_back (white, item);
          }
      }
  }

  return white;
}

struct VdlGcResult
vdl_gc_run (void)
{
  struct VdlList *global = vdl_linkmap_copy ();
  struct VdlList *unload = vdl_list_new ();
  struct VdlList *white = vdl_gc_white_list_new (global);
  while (!vdl_list_empty (white))
    {
      // copy white files into unload list
      vdl_list_insert_range (unload, vdl_list_end (unload),
                             vdl_list_begin (white), vdl_list_end (white));
      void **cur;
      for (cur = vdl_list_begin (white);
           cur != vdl_list_end (white); cur = vdl_list_next (cur))
        {
          // now, we remove that file from the global list to ensure
          // that the next call to vdl_gc_get_white won't return it again
          vdl_list_remove (global, *cur);
        }

      // Now, try to see if some of the deps will have to be unloaded
      vdl_list_delete (white);
      white = vdl_gc_white_list_new (global);
    }
  vdl_list_delete (white);
  // copy global files left into not_unload list
  struct VdlGcResult result;
  result.unload = unload;
  result.not_unload = global;
  return result;
}
