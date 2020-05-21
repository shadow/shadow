/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <sys/utsname.h>
#include <stdio.h>

#include "main/host/syscall_handler.h"
#include "main/host/syscall/protected.h"

SysCallReturn syscallhandler_getpid(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    // We can't handle this natively in the plugin if we want determinism
    guint pid = process_getProcessID(sys->process);
    return (SysCallReturn){
        .state = SYSCALL_RETURN_DONE, .retval.as_i64 = (int64_t)pid};
}

SysCallReturn syscallhandler_uname(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    struct utsname* buf = NULL;
    buf = thread_getWriteablePtr(sys->thread, args->args[0].as_ptr, sizeof(*buf));

    const gchar* hostname = host_getName(sys->host);

    snprintf(buf->sysname, _UTSNAME_SYSNAME_LENGTH, "shadowsys");
    snprintf(buf->nodename, _UTSNAME_NODENAME_LENGTH, "%s", hostname);
    snprintf(buf->release, _UTSNAME_RELEASE_LENGTH, "shadowrelease");
    snprintf(buf->version, _UTSNAME_VERSION_LENGTH, "shadowversion");
    snprintf(buf->machine, _UTSNAME_MACHINE_LENGTH, "shadowmachine");

    return (SysCallReturn){.state = SYSCALL_RETURN_DONE, .retval.as_i64 = 0};
}
