#include "vdl-init.h"
#include "vdl-utils.h"
#include "vdl-log.h"
#include "vdl-context.h"
#include "vdl-file.h"
#include "vdl-list.h"
#include "machine.h"


// the glibc elf loader passes all 3 arguments
// to the initialization functions and the libc initializer
// function makes use of these arguments to initialize
// __libc_argc, __libc_argv, and, __environ so, we do the
// same for compatibility purposes.
typedef void (*init_function) (int, char **, char **);

static void
call_init (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s", file->name);

  VDL_LOG_ASSERT (!file->init_called, "file has already been initialized");

  file->init_called = 1;

  if (file->is_executable)
    {
      // The constructors of the main executable are
      // run by the libc initialization code which has
      // been linked into the binary by the compiler.
      // If we run them here, they will be run twice which
      // is not good. So, we just return.
      return;
    }

  machine_reloc_irelative (file);


  // Gather information from the .dynamic section
  // First, invoke the old-style DT_INIT function.
  // The address of the function to call is stored in
  // the DT_INIT tag, here: dt_init.
  if (file->dt_init != 0)
    {
      DtInit dt_init = (DtInit) (file->load_base + file->dt_init);
      dt_init (file->context->argc, file->context->argv, file->context->envp);
    }

  // Then, invoke the newer DT_INIT_ARRAY functions.
  // The address of the functions to call is stored as
  // an array of pointers pointed to by DT_INIT_ARRAY
  if (file->dt_init_array != 0 &&
      file->dt_init_arraysz != 0)
    {
      DtInit *dt_init_array = (DtInit *) (file->load_base + file->dt_init_array);
      int i;
      int n = file->dt_init_arraysz / sizeof (DtInit);
      for (i = 0; i < n; i++)
	{
	  (dt_init_array[i]) (file->context->argc, file->context->argv, file->context->envp);
	}
    }

  vdl_context_notify (file->context, file, VDL_EVENT_CONSTRUCTED);
}

void vdl_init_call (struct VdlList *files)
{
  vdl_list_iterate (files, (void(*)(void*))call_init);
}
