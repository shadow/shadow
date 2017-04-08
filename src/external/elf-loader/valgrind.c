#include "vdl-dl.h"
#include "machine.h"
#include "macros.h"
#include "vdl-lookup.h"
#include "stage1.h"
#include "vdl-log.h"
#include "vdl-list.h"
#include "vdl-context.h"

typedef void (*LibcFreeRes) (void);

EXPORT void libc_freeres_interceptor (void);
void
libc_freeres_interceptor (void)
{
  VDL_LOG_FUNCTION ("");
  // call glibc function
  LibcFreeRes libc_freeres =
    (LibcFreeRes) vdl_dlvsym_with_flags (RTLD_DEFAULT, "__libc_freeres",
                                         0, VDL_LOOKUP_NO_REMAP,
                                         RETURN_ADDRESS);
  if (libc_freeres != 0)
    {
      libc_freeres ();
    }
  stage1_freeres ();
}

void
valgrind_initialize (void)
{
  VDL_LOG_FUNCTION ("");
  // we intercept only in the first context under the assumption that it's this
  // context which is going to trigger the exit_group syscall
  // which is the piece of code which will call __libc_freeres
  vdl_context_add_symbol_remap (g_vdl.main_context,
                                "__libc_freeres", 0, 0,
                                "libc_freeres_interceptor", 0, 0);
}
