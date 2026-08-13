/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/handler/ioctl.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/regular_file.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static int _syscallhandler_fcntlHelper(SyscallHandler* sys, RegularFile* file, int fd,
                                       unsigned long command, SyscallReg argReg) {
    int result = 0;

    switch (command) {
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
            result = regularfile_fcntl(file, command, NULL);
            break;
        }

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
            result = regularfile_fcntl(file, command, (void*)argReg.as_i64);
            break;
        }

        case F_GETLK:
#ifdef F_OFD_GETLK
        case F_OFD_GETLK:
#endif
        {
            struct flock flk = {0};
            result = process_readPtr(
                rustsyscallhandler_getProcess(sys), &flk, argReg.as_ptr, sizeof(flk));
            if (result < 0) {
                break;
            }
            result = regularfile_fcntl(file, command, &flk);
            if (result < 0) {
                break;
            }
            int write_result = process_writePtr(
                rustsyscallhandler_getProcess(sys), argReg.as_ptr, &flk, sizeof(flk));
            if (write_result < 0) {
                result = write_result;
            }
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
            struct flock flk = {0};
            result = process_readPtr(
                rustsyscallhandler_getProcess(sys), &flk, argReg.as_ptr, sizeof(flk));
            if (result < 0) {
                break;
            }
            result = regularfile_fcntl(file, command, &flk);
            break;
        }

        case F_GETOWN_EX: {
            struct f_owner_ex foe = {0};
            result = regularfile_fcntl(file, command, &foe);
            if (result < 0) {
                break;
            }
            int write_rv = process_writePtr(
                rustsyscallhandler_getProcess(sys), argReg.as_ptr, &foe, sizeof(foe));
            if (write_rv < 0) {
                result = write_rv;
            }
            break;
        }

        case F_SETOWN_EX: {
            struct f_owner_ex foe = {0};
            result = process_readPtr(
                rustsyscallhandler_getProcess(sys), &foe, argReg.as_ptr, sizeof(foe));
            result = regularfile_fcntl(file, command, &foe);
            break;
        }

#ifdef F_GET_RW_HINT
        case F_GET_RW_HINT:
#endif
#ifdef F_GET_FILE_RW_HINT
        case F_GET_FILE_RW_HINT:
#endif
        {
            uint64_t hint = 0;
            result = regularfile_fcntl(file, command, &hint);
            if (result < 0) {
                break;
            }
            int write_rv = process_writePtr(
                rustsyscallhandler_getProcess(sys), argReg.as_ptr, &hint, sizeof(hint));
            if (write_rv < 0) {
                result = write_rv;
                break;
            }
            break;
        }

#ifdef F_SET_RW_HINT
        case F_SET_RW_HINT:
#endif
#ifdef F_SET_FILE_RW_HINT
        case F_SET_FILE_RW_HINT:
#endif
        {
            uint64_t hint = 0;
            result = process_readPtr(
                rustsyscallhandler_getProcess(sys), &hint, argReg.as_ptr, sizeof(hint));
            if (result < 0) {
                break;
            }
            result = regularfile_fcntl(file, command, (void*)hint);
            break;
        }

        case F_GETFD:
        case F_SETFD:
        case F_DUPFD:
        case F_DUPFD_CLOEXEC: {
            warning("F_GETFD or F_SETFD or F_DUPFD or F_DUPFD_CLOEXEC (%lu) on file %i should have "
                    "been handled by the rust fcntl syscall handler",
                    command, fd);
            result = -EINVAL;
            break;
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

SyscallReturn syscallhandler_fcntl(SyscallHandler* sys, const SyscallArgs* args) {
    int fd = args->args[0].as_i64;
    unsigned long command = args->args[1].as_i64;
    SyscallReg argReg = args->args[2]; // type depends on command

    trace("fcntl called on fd %d for command %lu", fd, command);

    LegacyFile* desc = thread_getRegisteredLegacyFile(rustsyscallhandler_getThread(sys), fd);
    int errcode = _syscallhandler_validateLegacyFile(desc, DT_NONE);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    int result = 0;
    if (legacyfile_getType(desc) == DT_FILE) {
        result = _syscallhandler_fcntlHelper(sys, (RegularFile*)desc, fd, command, argReg);
    } else {
        /* TODO: add additional support for important operations. */
        switch (command) {
            case F_GETFL: {
                result = legacyfile_getFlags(desc);
                break;
            }
            case F_SETFL: {
                legacyfile_setFlags(desc, argReg.as_i64);
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

    return syscallreturn_makeDoneI64(result);
}
