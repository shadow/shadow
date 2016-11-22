#include "vdl-fini.h"
#include "vdl-utils.h"
#include "vdl-log.h"
#include "vdl-context.h"
#include "vdl-file.h"
#include "vdl-list.h"

typedef void (*fini_function) (void);

static void
call_fini (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s", file->name);

  VDL_LOG_ASSERT (!file->fini_called, "file has already been deinitialized");
  if (!file->init_called)
    {
      // if we were never initialized properly no need to do any work
      return;
    }
  // mark the file as finalized
  file->fini_called = 1;
  // Gather information from the .dynamic section

  // First, invoke the newer DT_FINI_ARRAY functions.
  // The address of the functions to call is stored as
  // an array of pointers pointed to by DT_FINI_ARRAY
  if (file->dt_fini_array != 0)
    {
      DtFini *dt_fini_array =
        (DtFini *) (file->load_base + file->dt_fini_array);
      int n = (file->dt_fini_arraysz / sizeof (DtFini));
      int i;
      // Be careful to iterate the array in reverse order
      for (i = 0; i < n; i++)
        {
          (dt_fini_array[n - 1 - i]) ();
        }
    }

  // Then, invoke the old-style DT_FINI function.
  // The address of the function to call is stored in
  // the DT_FINI tag, here: dt_fini.
  if (file->dt_fini != 0)
    {
      DtFini dt_fini = (DtFini) file->load_base + file->dt_fini;
      dt_fini ();
    }

  vdl_context_notify (file->context, file, VDL_EVENT_DESTROYED);
}

struct VdlList *
vdl_fini_lock (struct VdlList *files)
{
  // Make sure that we have not already planed to call fini
  // on these files
  struct VdlList *locked = vdl_list_new ();
  {
    void **cur;
    for (cur = vdl_list_begin (files);
         cur != vdl_list_end (files); cur = vdl_list_next (cur))
      {
        struct VdlFile *file = *cur;
        if (file->fini_call_lock == 1)
          {
            // already locked. ignore
            continue;
          }
        file->fini_call_lock = 1;
        vdl_list_push_back (locked, file);
      }
  }
  return locked;
}


void
vdl_fini_call (struct VdlList *files)
{
  vdl_list_iterate (files, (void (*)(void *)) call_fini);
}
