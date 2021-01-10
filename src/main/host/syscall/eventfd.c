/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/eventfd.h"

#include <errno.h>
#include <stddef.h>
#include <sys/eventfd.h>

#include "main/core/worker.h"
#include "main/host/descriptor/eventd.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/thread.h"
#include "support/logger/logger.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static int _syscallhandler_validateEventFDHelper(SysCallHandler* sys, int efd,
                                               EventD** event_desc_out) {
    /* Check that fd is within bounds. */
    if (efd <= 0) {
        info("descriptor %i out of bounds", efd);
        return -EBADF;
    }

    /* Check if this is a virtual Shadow descriptor. */
    Descriptor* desc = process_getRegisteredDescriptor(sys->process, efd);
    if (desc && event_desc_out) {
        *event_desc_out = (EventD*)desc;
    }

    int errcode = _syscallhandler_validateDescriptor(desc, DT_EVENTD);
    if (errcode) {
        info("descriptor %i is invalid", efd);
        return errcode;
    }

    /* Now we know we have a valid eventd object. */
    return 0;
}

static SysCallReturn _syscallhandler_eventfdHelper(SysCallHandler* sys,
                                           unsigned int initval, int flags) {
    debug("eventfd() called with initval %u and flags %i", initval, flags);

    /* any of 3 values can be bitwise ORed into flags */
    if (flags & ~(EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE)) {
        info("Invalid eventfd flags were given: %i", flags);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* Create the eventd object and double check that it's valid. */
    EventD* eventd = eventd_new(initval, flags & EFD_SEMAPHORE ? 1 : 0);
    int efd = process_registerDescriptor(sys->process, (Descriptor*)eventd);

#ifdef DEBUG
    /* This should always be a valid descriptor. */
    int errcode = _syscallhandler_validateEventFDHelper(sys, efd, NULL);
    if (errcode != 0) {
        error("Unable to find eventfd %i that we just created.", efd);
    }
    utility_assert(errcode == 0);
#endif

    /* Set any options that were given. */
    if (flags & EFD_NONBLOCK) {
        descriptor_addFlags((Descriptor*)eventd, O_NONBLOCK);
    }
    if (flags & EFD_CLOEXEC) {
        descriptor_addFlags((Descriptor*)eventd, O_CLOEXEC);
    }

    debug("eventfd() returning fd %i", efd);

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = efd};
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_eventfd(SysCallHandler* sys,
                                    const SysCallArgs* args) {
    return _syscallhandler_eventfdHelper(sys, args->args[0].as_u64, args->args[1].as_i64);
}

SysCallReturn syscallhandler_eventfd2(SysCallHandler* sys,
                                    const SysCallArgs* args) {
    return _syscallhandler_eventfdHelper(sys, args->args[0].as_u64, args->args[1].as_i64);
}
