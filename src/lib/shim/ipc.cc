#include "ipc.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

#include <atomic>
#include <mutex>
#include <new>
#include <unordered_map>
#include <unordered_set>

#include "lib/shim/binary_spinning_sem.h"

extern "C" {
#include "lib/logger/logger.h"
}

struct IPCData {
    IPCData(ssize_t spin_max) : xfer_ctrl_to_plugin(spin_max), xfer_ctrl_to_shadow(spin_max) {
        this->plugin_died.store(false, std::memory_order_relaxed);
    }
    ShimEvent plugin_to_shadow, shadow_to_plugin;
    BinarySpinningSem xfer_ctrl_to_plugin, xfer_ctrl_to_shadow;
    pid_t plugin_pid = 0;
    std::atomic<bool> plugin_died;
};

extern "C" {

void ipcData_init(IPCData* ipc_data, ssize_t spin_max) {
    new (ipc_data) IPCData(spin_max);
}

// Map of plugin (managed process) PID (thread group leader PID) to all of
// its threads' IPCDatas.
//
// Protected via _pidToDataMapLock() and _pidToDataMapUnlock().
static std::unordered_map<pid_t, std::unordered_set<struct IPCData*>>& _pidToDataMap() {
    // Using a function-scoped static instead of global static sidesteps some
    // subtle global constructor and destructor issues. See
    // https://google.github.io/styleguide/cppguide.html#Static_and_Global_Variables
    static auto& m = *new std::unordered_map<pid_t, std::unordered_set<struct IPCData*>>();
    return m;
}

static std::mutex& _pidToDataMapMutex() {
    // Using a function-scoped static instead of global static sidesteps some
    // subtle global constructor and destructor issues. See
    // https://google.github.io/styleguide/cppguide.html#Static_and_Global_Variables
    static auto& m = *new std::mutex();
    return m;
}

// Locks the _pidToDataMap() and blocks the SIGCHLD signal, since the handler
// needs to access the map and would otherwise deadlock if the handler was
// running on a thread already holding the lock.
static void _pidToDataMapLock() {
    sigset_t sigchild_set;
    if (sigemptyset(&sigchild_set)) {
        panic("sigemptyset: %s", strerror(errno));
    }
    if (sigaddset(&sigchild_set, SIGCHLD)) {
        panic("sigaddset: %s", strerror(errno));
    }
    if (sigprocmask(SIG_BLOCK, &sigchild_set, NULL)) {
        panic("sigprocmask: %s", strerror(errno));
    }
    _pidToDataMapMutex().lock();
}

// Unblocks SIGCHLD and unlocks the _pidToDataMap().
static void _pidToDataMapUnlock() {
    _pidToDataMapMutex().unlock();
    sigset_t sigchild_set;
    if (sigemptyset(&sigchild_set)) {
        panic("sigemptyset: %s", strerror(errno));
    }
    if (sigaddset(&sigchild_set, SIGCHLD)) {
        panic("sigaddset: %s", strerror(errno));
    }
    if (sigprocmask(SIG_UNBLOCK, &sigchild_set, NULL)) {
        panic("sigprocmask: %s", strerror(errno));
    }
}

void ipcData_destroy(struct IPCData* ipc_data) {
    if (ipc_data->plugin_pid) {
        // Remove this pointer from the map.
        _pidToDataMapLock();
        auto map_it = _pidToDataMap().find(ipc_data->plugin_pid);
        assert(map_it != _pidToDataMap().end());
        auto& ipc_ptr_set = map_it->second;
        ipc_ptr_set.erase(ipc_data);
        if (ipc_ptr_set.size() == 0) {
            _pidToDataMap().erase(map_it);
        }
        _pidToDataMapUnlock();
    }
    // Call any C++ destructors.
    ipc_data->~IPCData();
}

// Handler for SIGCHLD. When a managed (plugin) process dies, this simulates as
// if every thread in that process had sent a SHD_SHIM_EVENT_STOP, which wakes
// up any corresponding blocked Shadow threads.
static void _sigchld_sigaction(int signum, siginfo_t* info, void* ucontext) {
    trace("Received SIGCHLD for pid %d", info->si_pid);
    // Lock the raw mutex. SIGCHLD is already blocked while this handler is running,
    // so don't need to use the wrapper.
    std::lock_guard<std::mutex> lock(_pidToDataMapMutex());
    const pid_t pid = info->si_pid;
    auto it = _pidToDataMap().find(pid);
    if (it == _pidToDataMap().end()) {
        return;
    }
    for (IPCData* ipc_data : it->second) {
        // We can't write to the event in ipc_data unless we protected it with a
        // lock AND blocked SIGCHLD whenever we accessed it. Instead we use
        // an auxiliary atomic bool.
        //
        // We can use memory_order_relaxed for this bool, since the semaphore
        // will already force correct synchronization.
        ipc_data->plugin_died.store(true, std::memory_order_relaxed);
        // We *can* post to the semaphore, which is thread safe. This ensures
        // that if a shadow thread is already blocked on the semaphore, it will
        // be woken up.
        ipc_data->xfer_ctrl_to_shadow.post();
    }
}

void ipcData_registerPluginPid(struct IPCData* ipc_data, pid_t pid) {
    _pidToDataMapLock();
    assert(!ipc_data->plugin_pid);
    ipc_data->plugin_pid = pid;
    static bool did_global_init = false;
    if (!did_global_init) {
        struct sigaction old_action = {0};
        struct sigaction action = {0};
        action.sa_sigaction = _sigchld_sigaction;
        action.sa_flags = SA_SIGINFO | SA_NOCLDSTOP | SA_RESTART;
        if (sigaction(SIGCHLD, &action, &old_action)) {
            panic("sigaction: %s", strerror(errno));
        }
        if (old_action.sa_handler != SIG_DFL && old_action.sa_handler != SIG_IGN) {
            // Some other code already installed a SIGCHLD handler, and we just
            // clobbered it. Better sort it out.
            panic("We clobbered sigchld handler %p", old_action.sa_handler);
        }
        did_global_init = true;
    }
    if (_pidToDataMap().find(pid) == _pidToDataMap().end()) {
        _pidToDataMap().insert({pid, std::unordered_set<IPCData*>()});
    }
    _pidToDataMap().at(pid).insert(ipc_data);
    _pidToDataMapUnlock();
}

size_t ipcData_nbytes() { return sizeof(IPCData); }

void shimevent_sendEventToShadow(struct IPCData* data, const ShimEvent* e) {
    data->plugin_to_shadow = *e;
    data->xfer_ctrl_to_shadow.post();
}

void shimevent_sendEventToPlugin(struct IPCData* data, const ShimEvent* e) {
    data->shadow_to_plugin = *e;
    data->xfer_ctrl_to_plugin.post();
}

void shimevent_recvEventFromShadow(struct IPCData* data, ShimEvent* e, bool spin) {
    data->xfer_ctrl_to_plugin.wait(spin);
    *e = data->shadow_to_plugin;
}

void shimevent_recvEventFromPlugin(struct IPCData* data, ShimEvent* e) {
    data->xfer_ctrl_to_shadow.wait();
    if (data->plugin_died.load(std::memory_order_relaxed)) {
        e->event_id = SHD_SHIM_EVENT_STOP;
    } else {
        *e = data->plugin_to_shadow;
    }
}

int shimevent_tryRecvEventFromShadow(struct IPCData* data, ShimEvent* e) {
    int rv = data->xfer_ctrl_to_plugin.trywait();
    if (rv != 0) {
        return rv;
    }
    *e = data->shadow_to_plugin;
    return 0;
}

int shimevent_tryRecvEventFromPlugin(struct IPCData* data, ShimEvent* e) {
    int rv = data->xfer_ctrl_to_shadow.trywait();
    if (rv != 0) {
        return rv;
    }
    if (data->plugin_died.load(std::memory_order_relaxed)) {
        e->event_id = SHD_SHIM_EVENT_STOP;
        return 0;
    }

    *e = data->plugin_to_shadow;
    return 0;
}

} // extern "C"
