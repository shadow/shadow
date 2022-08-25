// Include headers for which to generate C bindings here.  As we add headers
// here, try to ensure that they don't pull in more external definitions than
// needed. e.g. consider avoiding including glib.h in these by swapping types
// like `gboolean` with standard types like `bool`.

// Don't forget to whitelist functions/types/vars in CMakeLists.txt

#include "main/core/logger/log_wrapper.h"
#include "main/core/main.h"
#include "main/core/scheduler/scheduler.h"
#include "main/core/support/config_handlers.h"
#include "main/core/worker.h"
#include "main/host/affinity.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/host.h"
#include "main/host/process.h"
#include "main/host/status.h"
#include "main/host/status_listener.h"
#include "main/host/syscall/fcntl.h"
#include "main/host/syscall/ioctl.h"
#include "main/host/syscall/socket.h"
#include "main/host/syscall/unistd.h"
#include "main/host/syscall_condition.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"
#include "main/routing/packet.h"
#include "main/utility/rpath.h"
#include "main/utility/utility.h"
