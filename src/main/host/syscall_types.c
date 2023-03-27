#include "main/host/syscall_types.h"

#include "main/utility/utility.h"

const char* syscallreturnstate_str(SyscallReturn_Tag s) {
    switch (s) {
        case SYSCALL_RETURN_DONE: return "DONE";
        case SYSCALL_RETURN_BLOCK: return "BLOCK";
        case SYSCALL_RETURN_NATIVE: return "NATIVE";
    }
    return "UNKNOWN";
}