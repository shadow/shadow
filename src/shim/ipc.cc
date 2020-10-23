#include "ipc.h"

#include <new>

#include "shim/binary_spinning_sem.h"

struct IPCData {
    IPCData(ssize_t spin_max) : xfer_ctrl_to_plugin(spin_max), xfer_ctrl_to_shadow(spin_max) {}
    ShimEvent plugin_to_shadow, shadow_to_plugin;
    BinarySpinningSem xfer_ctrl_to_plugin, xfer_ctrl_to_shadow;
};

extern "C" {

void ipcData_init(IPCData* ipc_data, ssize_t spin_max) {
    new (ipc_data) IPCData(spin_max);
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
    *e = data->plugin_to_shadow;
}

} // extern "C"
