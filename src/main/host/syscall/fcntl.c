/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/ioctl.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/file.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/thread.h"
#include "support/logger/logger.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static int _syscallhandler_fcntlHelper(SysCallHandler* sys, File* file, int fd,
                                       unsigned long command,
                                       SysCallReg argReg) {
    int result = 0;

    switch (command) {
        case F_GETFD:
        case F_GETFL:
        case F_GETOWN:
        case F_GETSIG:
        case F_GETLEASE:
        case F_GETPIPE_SZ:
#ifdef F_GET_SEALS
        case F_GET_SEALS:
#endif
        {
            // arg is ignored
            result = file_fcntl(file, command, NULL);
            break;
        }

        case F_SETFD:
        case F_SETFL:
        case F_SETOWN:
        case F_SETSIG:
        case F_SETLEASE:
        case F_NOTIFY:
        case F_SETPIPE_SZ:
#ifdef F_ADD_SEALS
        case F_ADD_SEALS:
#endif
        {
            // arg is an int (we cast to void* here to appease the fcntl api)
            result = file_fcntl(file, command, (void*)argReg.as_i64);
            break;
        }

        case F_GETLK:
#ifdef F_OFD_GETLK
        case F_OFD_GETLK:
#endif
        {
            struct flock* flk = memorymanager_getMutablePtr(
                sys->memoryManager, sys->thread, argReg.as_ptr, sizeof(*flk));
            result = file_fcntl(file, command, (void*)flk);
            break;
        }

        case F_SETLK:
#ifdef F_OFD_SETLK
        case F_OFD_SETLK:
#endif
        case F_SETLKW:
#ifdef F_OFD_SETLKW
        case F_OFD_SETLKW:
#endif
        {
            const struct flock* flk =
                memorymanager_getReadablePtr(sys->memoryManager,sys->thread, argReg.as_ptr, sizeof(*flk));
            result = file_fcntl(file, command, (void*)flk);
            break;
        }

        case F_GETOWN_EX: {
            struct f_owner_ex* foe = memorymanager_getWriteablePtr(sys->memoryManager,
                sys->thread, argReg.as_ptr, sizeof(*foe));
            result = file_fcntl(file, command, foe);
            break;
        }

        case F_SETOWN_EX: {
            const struct f_owner_ex* foe =
                memorymanager_getReadablePtr(sys->memoryManager,sys->thread, argReg.as_ptr, sizeof(*foe));
            result = file_fcntl(file, command, (void*)foe);
            break;
        }

#ifdef F_GET_RW_HINT
        case F_GET_RW_HINT:
#endif
#ifdef F_GET_FILE_RW_HINT
        case F_GET_FILE_RW_HINT:
#endif
        {
            uint64_t* hint = memorymanager_getWriteablePtr(sys->memoryManager,
                sys->thread, argReg.as_ptr, sizeof(*hint));
            result = file_fcntl(file, command, hint);
            break;
        }

#ifdef F_SET_RW_HINT
        case F_SET_RW_HINT:
#endif
#ifdef F_SET_FILE_RW_HINT
        case F_SET_FILE_RW_HINT:
#endif
        {
            const uint64_t* hint = memorymanager_getReadablePtr(sys->memoryManager,
                sys->thread, argReg.as_ptr, sizeof(*hint));
            result = file_fcntl(file, command, (void*)hint);
            break;
        }

        case F_DUPFD:
        case F_DUPFD_CLOEXEC: {
            // TODO: update this once we implement dup
            // no break: fall through to default case
        }

        default: {
            warning("We do not yet handle fcntl command %lu on file %i",
                    command, fd);
            result = -EINVAL; // kernel does not recognize command
            break;
        }
    }

    return result;
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_fcntl(SysCallHandler* sys,
                                   const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    unsigned long command = args->args[1].as_i64;
    SysCallReg argReg = args->args[2]; // type depends on command

    debug("fcntl called on fd %d for command %lu", fd, command);

    Descriptor* desc = process_getRegisteredDescriptor(sys->process, fd);
    int errcode = _syscallhandler_validateDescriptor(desc, DT_NONE);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    int result = 0;
    if (descriptor_getType(desc) == DT_FILE) {
        result =
            _syscallhandler_fcntlHelper(sys, (File*)desc, fd, command, argReg);
    } else {
        /* TODO: add additional support for important operations. */
        switch (command) {
            case F_GETFL: {
                result = descriptor_getFlags(desc);
                break;
            }
            case F_SETFL: {
                descriptor_setFlags(desc, argReg.as_i64);
                break;
            }
            default: {
                warning("We do not support fcntl command %lu on descriptor %i",
                        command, fd);
                result = -EINVAL; // kernel does not recognize command
                break;
            }
        }
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = result};
}
