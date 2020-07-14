// Include headers for which to generate C bindings here.  As we add headers
// here, try to ensure that they don't pull in more external definitions than
// needed. e.g. consider avoiding including glib.h in these by swapping types
// like `gboolean` with standard types like `bool`.

#include "main/host/syscall_types.h"
#include "main/host/thread.h"
