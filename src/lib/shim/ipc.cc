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

void ipcData_destroy(struct IPCData* ipc_data) {
    // Call any C++ destructors.
    ipc_data->~IPCData();
}

void ipcData_markPluginExited(struct IPCData* ipc_data) {
    // We can't write to the event in ipc_data, since it isn't protected
    // by a mutex. We also don't *want* to protect it with a mutex, since
    // that'd add a non-trivial cost to every transition.
    //
    // We can use memory_order_relaxed for this bool, since the semaphore
    // will already force correct synchronization.
    ipc_data->plugin_died.store(true, std::memory_order_relaxed);
    // We *can* post to the semaphore, which is thread safe. This ensures
    // that if a shadow thread is already blocked on the semaphore, it will
    // be woken up. Otherwise the next attempt to read from the plugin should
    // return immediately.
    ipc_data->xfer_ctrl_to_shadow.post();
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
