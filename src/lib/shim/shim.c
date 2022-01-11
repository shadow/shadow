#include "lib/shim/shim.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <search.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "lib/shim/ipc.h"
#include "lib/shim/shadow_sem.h"
#include "lib/shim/shadow_spinlock.h"
#include "lib/shim/shim_event.h"
#include "lib/shim/shim_logger.h"
#include "lib/shim/shim_rdtsc.h"
#include "lib/shim/shim_seccomp.h"
#include "lib/shim/shim_sys.h"
#include "lib/shim/shim_syscall.h"
#include "lib/shim/shim_tls.h"
#include "main/host/syscall_numbers.h" // for SYS_shadow_* defs

// Whether Shadow is using preload-based interposition.
static bool _using_interpose_preload = false;

// Whether Shadow is using ptrace-based interposition.
static bool _using_interpose_ptrace = false;

// Whether Shadow is using the shim-side syscall handler optimization.
static bool _using_shim_syscall_handler = true;

// This thread's IPC block, for communication with Shadow.
static ShMemBlock* _shim_ipcDataBlk() {
    static ShimTlsVar v = {0};
    return shimtlsvar_ptr(&v, sizeof(ShMemBlock));
}
struct IPCData* shim_thisThreadEventIPC() {
    return _shim_ipcDataBlk()->p;
}

// Per-thread state shared with Shadow.
static ShMemBlock* _shim_shared_mem_blk() {
    static ShimTlsVar v = {0};
    return shimtlsvar_ptr(&v, sizeof(ShMemBlock));
}
static ShimSharedMem* _shim_shared_mem() {
    if (_shim_shared_mem_blk() == NULL) {
        return NULL;
    }
    return _shim_shared_mem_blk()->p;
}

// We disable syscall interposition when this is > 0.
static int* _shim_disable_interposition() {
    static ShimTlsVar v = {0};
    return shimtlsvar_ptr(&v, sizeof(int));
}

static void _shim_set_allow_native_syscalls(bool is_allowed) {
    if (_shim_shared_mem()) {
        _shim_shared_mem()->ptrace_allow_native_syscalls = is_allowed;
        trace("%s native-syscalls via shmem %p", is_allowed ? "allowing" : "disallowing",
              _shim_shared_mem);
    } else {
        // Ptrace will intercept the native syscall and handle this from within Shadow.
        shim_native_syscall(SYS_shadow_set_ptrace_allow_native_syscalls, is_allowed);
        trace("%s native-syscalls via custom syscall", is_allowed ? "allowing" : "disallowing");
    }
}

// Held from the time of starting to initialize _startThread, to being done with
// it. i.e. ensure we don't try to start more than one thread at once.
//
// For example, this prevents the child thread, after having initialized itself
// and released the parent via the childInitd semaphore, from starting another
// clone itself until after the parent has woken up and released this lock.
static shadow_spinlock_t _startThreadLock = SHADOW_SPINLOCK_STATICALLY_INITD;
static struct {
    ShMemBlock childIpcBlk;
    shadow_sem_t childInitd;
} _startThread;

void shim_newThreadStart(ShMemBlockSerialized* block) {
    if (shadow_spin_lock(&_startThreadLock)) {
        panic("shadow_spin_lock: %s", strerror(errno));
    };
    if (shadow_sem_init(&_startThread.childInitd, 0, 0)) {
        panic("shadow_sem_init: %s", strerror(errno));
    }
    _startThread.childIpcBlk = shmemserializer_globalBlockDeserialize(block);
}

void shim_newThreadChildInitd() {
    if (shadow_sem_post(&_startThread.childInitd)) {
        panic("shadow_sem_post: %s", strerror(errno));
    }
}

void shim_newThreadFinish() {
    // Wait for child to initialize itself.
    while (shadow_sem_trywait(&_startThread.childInitd)) {
        if (errno != EAGAIN) {
            panic("shadow_sem_trywait: %s", strerror(errno));
        }
        if (shim_native_syscall(SYS_sched_yield)) {
            panic("shim_native_syscall(SYS_sched_yield): %s", strerror(errno));
        }
    }

    // Release the global clone lock.
    if (shadow_spin_unlock(&_startThreadLock)) {
        panic("shadow_spin_unlock: %s", strerror(errno));
    }
}

void shim_disableInterposition() {
    if (++*_shim_disable_interposition() == 1) {
        if (_using_interpose_ptrace) {
            // We can't prevent there being a ptrace-stop on a syscall, but we
            // can signal Shadow to allow syscalls to execute natively.
            _shim_set_allow_native_syscalls(true);
        }
    }
}

void shim_enableInterposition() {
    assert(_shim_disable_interposition > 0);
    if (--*_shim_disable_interposition() == 0) {
        if (_using_interpose_ptrace) {
            _shim_set_allow_native_syscalls(false);
        }
    }
}

bool shim_interpositionEnabled() {
    return !*_shim_disable_interposition();
}

bool shim_use_syscall_handler() { return _using_shim_syscall_handler; }

// Figure out what interposition mechanism we're using, based on environment
// variables.  This is called before disabling interposition, so should be
// careful not to make syscalls.
static void _set_interpose_type() {
    // If we're not running under Shadow, return. This can be useful
    // for testing the libc parts of the shim.
    if (!getenv("SHADOW_SPAWNED")) {
        return;
    }

    const char* interpose_method = getenv("SHADOW_INTERPOSE_METHOD");
    assert(interpose_method);
    if (!strcmp(interpose_method, "PRELOAD")) {
        // Uses library preloading to intercept syscalls.
        _using_interpose_preload = true;
        return;
    }
    if (!strcmp(interpose_method, "PTRACE")) {
        // From the shim's point of view, behave as if it's not running under
        // Shadow, and let all control happen via ptrace.
        _using_interpose_ptrace = true;
        return;
    }
    abort();
}

static void _set_use_shim_syscall_handler() {
    const char* shim_syscall_str = getenv("SHADOW_DISABLE_SHIM_SYSCALL");
    if (shim_syscall_str && !strcmp(shim_syscall_str, "TRUE")) {
        _using_shim_syscall_handler = false;
    } else {
        _using_shim_syscall_handler = true;
    }
}

static void _shim_parent_init_logging() {
    // Set logger start time from environment variable.
    {
        const char* logger_start_time_string = getenv("SHADOW_LOG_START_TIME");
        if (!logger_start_time_string) {
            panic("Missing SHADOW_LOG_START_TIME");
        }
        int64_t logger_start_time;
        if (sscanf(logger_start_time_string, "%" PRId64, &logger_start_time) != 1) {
            panic("Couldn't parse logger start time string %s", logger_start_time_string);
        };
        logger_set_global_start_time_micros(logger_start_time);
    }

    // Redirect logger to specified log file.
    {
        const char* name = getenv("SHADOW_LOG_FILE");
        FILE* log_file = fopen(name, "w");
        if (log_file == NULL) {
            panic("fopen: %s", strerror(errno));
        }
        logger_setDefault(shimlogger_new(log_file));
    }

    // Set log level
    {
        const char* level_string = getenv("SHADOW_LOG_LEVEL");
        if (!level_string) {
            panic("Missing SHADOW_LOG_LEVEL");
        }
        int level;
        if (sscanf(level_string, "%d", &level) != 1) {
            panic("Couldn't parse log level %s", level_string);
        };
        logger_setLevel(logger_getDefault(), level);
    }
}

/*
 * If we can parse it from the env, check that Shadow's PID is my parent and
 * exit otherwise.
 */
static void _verify_parent_pid_or_exit() {
    unsigned long long shadow_pid = 0;
    bool valid_parse_pid = false;
    const char* shadow_pid_str = getenv("SHADOW_PID");
    if (!shadow_pid_str) {
        panic("SHADOW_PID not set");
    }

    if (sscanf(shadow_pid_str, "%llu", &shadow_pid) != 1) {
        panic("SHADOW_PID does not contain an unsigned: %s", shadow_pid_str);
    }

    if (getppid() != shadow_pid) { // Validate that Shadow is still alive.
        error("Shadow exited.");
        exit(EXIT_FAILURE);
    }

    trace("Plugin verified Shadow is still running as parent.");
}

static void _shim_parent_init_death_signal() {
    // Ensure that the child process exits when Shadow does.  Shadow
    // ought to have already tried to terminate the child via SIGTERM
    // before shutting down (though see
    // https://github.com/shadow/shadow/issues/903), so now we jump all
    // the way to SIGKILL.
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0) {
        warning("prctl: %s", strerror(errno));
    }

    _verify_parent_pid_or_exit();
}

static void _shim_parent_init_shm() {
    assert(_using_interpose_ptrace);

    const char* shm_blk_buf = getenv("SHADOW_SHM_BLK");
    assert(shm_blk_buf);

    bool err = false;
    ShMemBlockSerialized shm_blk_serialized = shmemblockserialized_fromString(shm_blk_buf, &err);

    *_shim_shared_mem_blk() = shmemserializer_globalBlockDeserialize(&shm_blk_serialized);
    assert(_shim_shared_mem());
}

static void _shim_child_init_shm() {
    assert(_using_interpose_ptrace);

    assert(!_shim_shared_mem());
    ShMemBlockSerialized shm_blk_serialized;

    // We execute this natively and ptrace will intercept and handle from within Shadow.
    int rv = shim_native_syscall(SYS_shadow_get_shm_blk, &shm_blk_serialized);
    if (rv != 0) {
        panic("shadow_get_shm_blk: %s", strerror(errno));
        abort();
    }

    *_shim_shared_mem_blk() = shmemserializer_globalBlockDeserialize(&shm_blk_serialized);
    assert(_shim_shared_mem());
}

static void _shim_parent_init_ipc() {
    assert(_using_interpose_preload);

    const char* ipc_blk_buf = getenv("SHADOW_IPC_BLK");
    assert(ipc_blk_buf);
    bool err = false;
    ShMemBlockSerialized ipc_blk_serialized = shmemblockserialized_fromString(ipc_blk_buf, &err);
    assert(!err);

    *_shim_ipcDataBlk() = shmemserializer_globalBlockDeserialize(&ipc_blk_serialized);
    assert(shim_thisThreadEventIPC());
}

static void _shim_preload_only_child_init_ipc() {
    assert(_using_interpose_preload);
    assert(!_using_interpose_ptrace);

    *_shim_ipcDataBlk() = _startThread.childIpcBlk;
}

static void _shim_preload_only_child_ipc_wait_for_start_event() {
    assert(_using_interpose_preload);
    assert(shim_thisThreadEventIPC());

    ShimEvent event;
    trace("waiting for start event on %p", shim_thisThreadEventIPC);

    // We're returning control to the parent thread here, who is going to switch
    // back to their own TLS.
    struct IPCData* ipc = shim_thisThreadEventIPC();

    // Releases parent thread, who switches back to their own TLS.  i.e. Don't
    // use TLS between here and when we can switch back to our own after
    // receiving the start event.
    shim_newThreadChildInitd();

    shimevent_recvEventFromShadow(ipc, &event, /* spin= */ true);
    assert(event.event_id == SHD_SHIM_EVENT_START);
    shim_sys_set_simtime_nanos(event.event_data.start.simulation_nanos);
}

static void _shim_ipc_wait_for_start_event() {
    assert(_using_interpose_preload);
    assert(shim_thisThreadEventIPC());

    ShimEvent event;
    trace("waiting for start event on %p", shim_thisThreadEventIPC);
    shimevent_recvEventFromShadow(shim_thisThreadEventIPC(), &event, /* spin= */ true);
    assert(event.event_id == SHD_SHIM_EVENT_START);
    shim_sys_set_simtime_nanos(event.event_data.start.simulation_nanos);
}

static void _shim_parent_init_ptrace() {
    shim_disableInterposition();

    _shim_parent_init_logging();
    _shim_parent_init_shm();

    shim_enableInterposition();
}

static void _shim_parent_init_seccomp() {
    shim_seccomp_init();
}

static void _shim_parent_init_rdtsc_emu() {
    //shim_rdtsc_init();
}

static void _shim_parent_init_preload() {
    shim_disableInterposition();

    // The shim logger internally disables interposition while logging, so we open the log
    // file with interposition disabled too to get a native file descriptor.
    _shim_parent_init_logging();
    _shim_parent_init_ipc();
    _shim_parent_init_death_signal();
    _shim_ipc_wait_for_start_event();
    _shim_parent_init_rdtsc_emu();
    if (getenv("SHADOW_USE_SECCOMP") != NULL) {
        _shim_parent_init_seccomp();
    }

    shim_enableInterposition();
}

static void _shim_child_init_ptrace() {
    shim_disableInterposition();

    _shim_child_init_shm();

    shim_enableInterposition();
}

static void _shim_child_init_preload() {
    shim_disableInterposition();

    _shim_preload_only_child_init_ipc();
    _shim_preload_only_child_ipc_wait_for_start_event();

    shim_enableInterposition();
}

// This function should be called before any wrapped syscall. We also use the
// constructor attribute to be completely sure that it's called before main.
__attribute__((constructor)) void _shim_load() {
    static bool did_global_pre_init = false;
    if (!did_global_pre_init) {
        // Early init; must not make any syscalls.

        did_global_pre_init = true;

        // Avoid logging until we've set up the shim logger.
        logger_setLevel(logger_getDefault(), LOGLEVEL_WARNING);

        _set_interpose_type();
        _set_use_shim_syscall_handler();
    }

    // Now we can use thread-local storage.
    static ShimTlsVar started_thread_init_var = {0};
    bool* started_thread_init =
        shimtlsvar_ptr(&started_thread_init_var, sizeof(*started_thread_init));
    if (*started_thread_init) {
        // Avoid deadlock when _shim_global_init's syscalls caused this function to be
        // called recursively.  In the uninitialized state,
        // `shim_interpositionEnabled` returns false, allowing _shim_global_init's
        // syscalls to execute natively.
        return;
    }
    *started_thread_init = true;

    static bool did_global_init = false;
    if (!did_global_init) {
        if (_using_interpose_ptrace) {
            _shim_parent_init_ptrace();
        } else if (_using_interpose_preload) {
            _shim_parent_init_preload();
        }
        did_global_init = true;
        trace("Finished shim parent init");
    } else {
        if (_using_interpose_ptrace) {
            _shim_child_init_ptrace();
        } else if (_using_interpose_preload) {
            _shim_child_init_preload();
        }
        trace("Finished shim child init");
    }
}

void shim_ensure_init() { _shim_load(); }

struct timespec* shim_get_shared_time_location() {
    if (_shim_shared_mem() == NULL) {
        return NULL;
    } else {
        return &_shim_shared_mem()->sim_time;
    }
}
