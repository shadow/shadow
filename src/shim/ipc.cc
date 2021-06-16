#include "ipc.h"

#include <assert.h>
#include <errno.h>
#include <new>
#include <stdlib.h>

#include "shim/binary_spinning_sem.h"

typedef enum {
    IPC_SOCKET,
    IPC_SEMAPHORE,
} IPCDataType;

struct SocketIPCData {
    int to_shadow_fd;
    int to_plugin_fd;
    ssize_t spin_max;
};

struct SemaphoreIPCData {
    SemaphoreIPCData(ssize_t spin_max)
        : xfer_ctrl_to_plugin(spin_max), xfer_ctrl_to_shadow(spin_max) {}
    ShimEvent plugin_to_shadow, shadow_to_plugin;
    BinarySpinningSem xfer_ctrl_to_plugin, xfer_ctrl_to_shadow;
};

struct IPCData {
    IPCDataType type;
    union {
        SocketIPCData socket;
        SemaphoreIPCData semaphore;
    };
};

extern "C" {

static void utility_assert(bool b) {
    if (!b) {
        // Save errno for debugger.
        int e = errno;

        abort();
    }
}

void ipcData_initSocket(IPCData* ipc_data, ssize_t spin_max) {
    int fds[2];
    int rv = socketpair(PF_LOCAL, SOCK_SEQPACKET, 0, fds);
    utility_assert(rv == 0);
    ipc_data->type = IPC_SOCKET;
    ipc_data->socket = (SocketIPCData){
        .to_shadow_fd = fds[0],
        .to_plugin_fd = fds[1],
        .spin_max = spin_max,
    };
}

void ipcData_initSemaphore(IPCData* ipc_data, ssize_t spin_max) {
    ipc_data->type = IPC_SEMAPHORE;
    new (&ipc_data->semaphore) SemaphoreIPCData(spin_max);
}

size_t ipcData_nbytes() { return sizeof(IPCData); }

void shimevent_sendEventToShadow(struct IPCData* data, const ShimEvent* e) {
    switch (data->type) {
        case IPC_SOCKET: {
            int rv;
            do {
                rv = send(data->socket.to_shadow_fd, e, sizeof(*e), 0);
            } while (rv == -1 && errno == EINTR);
            utility_assert(rv == sizeof(*e));
            return;
        }
        case IPC_SEMAPHORE: {
            data->semaphore.plugin_to_shadow = *e;
            data->semaphore.xfer_ctrl_to_shadow.post();
            return;
        }
            // No default to force exhaustive.
    }
    abort();
}

void shimevent_sendEventToPlugin(struct IPCData* data, const ShimEvent* e) {
    switch (data->type) {
        case IPC_SOCKET: {
            int rv;
            do {
                rv = send(data->socket.to_plugin_fd, e, sizeof(*e), 0);
            } while (rv == -1 && errno == EINTR);
            utility_assert(rv == sizeof(*e));
            return;
        }
        case IPC_SEMAPHORE: {
            data->semaphore.shadow_to_plugin = *e;
            data->semaphore.xfer_ctrl_to_plugin.post();
            return;
        }
            // No default to force exhaustive.
    }
    abort();
}

void shimevent_recvEventFromShadow(struct IPCData* data, ShimEvent* e, bool spin) {
    switch (data->type) {
        case IPC_SOCKET: {
            if (spin) {
                for (int i = 0; i < data->socket.spin_max; ++i) {
                    if (shimevent_tryRecvEventFromShadow(data, e) >= 0) {
                        return;
                    }
                }
            }
            int rv;
            do {
                rv = recv(data->socket.to_shadow_fd, e, sizeof(*e), 0);
            } while (rv == -1 && errno == EINTR);
            utility_assert(rv == sizeof(*e));
            return;
        }
        case IPC_SEMAPHORE: {
            data->semaphore.xfer_ctrl_to_plugin.wait(spin);
            *e = data->semaphore.shadow_to_plugin;
            return;
        }
            // No default to force exhaustive.
    }
    abort();
}

void shimevent_recvEventFromPlugin(struct IPCData* data, ShimEvent* e) {
    switch (data->type) {
        case IPC_SOCKET: {
            for (int i = 0; i < data->socket.spin_max; ++i) {
                if (shimevent_tryRecvEventFromPlugin(data, e) >= 0) {
                    return;
                }
            }
            int rv;
            do {
                rv = recv(data->socket.to_plugin_fd, e, sizeof(*e), 0);
            } while (rv == -1 && errno == EINTR);
            utility_assert(rv == sizeof(*e));
            return;
        }
        case IPC_SEMAPHORE: {
            data->semaphore.xfer_ctrl_to_shadow.wait();
            *e = data->semaphore.plugin_to_shadow;
            return;
        }
            // No default to force exhaustive.
    }
    abort();
}

int shimevent_tryRecvEventFromShadow(struct IPCData* data, ShimEvent* e) {
    switch (data->type) {
        case IPC_SOCKET: {
            int rv = recv(data->socket.to_shadow_fd, e, sizeof(*e), MSG_DONTWAIT);
            if (rv >= 0) {
                utility_assert(rv == sizeof(*e));
                return 0;
            }
            return rv;
        }
        case IPC_SEMAPHORE: {
            int rv = data->semaphore.xfer_ctrl_to_plugin.trywait();
            if (rv != 0) {
                return rv;
            }
            *e = data->semaphore.shadow_to_plugin;
            return 0;
        }
            // No default to force exhaustive.
    }
    abort();
}

int shimevent_tryRecvEventFromPlugin(struct IPCData* data, ShimEvent* e) {
    switch (data->type) {
        case IPC_SOCKET: {
            int rv = recv(data->socket.to_plugin_fd, e, sizeof(*e), MSG_DONTWAIT);
            if (rv >= 0) {
                utility_assert(rv == sizeof(*e));
                return 0;
            }
            return rv;
        }
        case IPC_SEMAPHORE: {
            int rv = data->semaphore.xfer_ctrl_to_shadow.trywait();
            if (rv != 0) {
                return rv;
            }
            *e = data->semaphore.plugin_to_shadow;
            return 0;
        }
            // No default to force exhaustive.
    }
    abort();
}

} // extern "C"
