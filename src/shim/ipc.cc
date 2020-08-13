#include "ipc.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "shim/gate.h"

struct IPCData {
    ShimEvent plugin_to_shadow, shadow_to_plugin;
    Gate xfer_ctrl_to_plugin, xfer_ctrl_to_shadow;
};

extern "C" {

void ipcData_init(IPCData *ipc_data) {
    memset(ipc_data, 0, sizeof(IPCData));

    gate_init(&ipc_data->xfer_ctrl_to_plugin);
    gate_init(&ipc_data->xfer_ctrl_to_shadow);
}

size_t ipcData_nbytes() {
    return sizeof(IPCData);
}

void shimevent_sendEventToShadow(struct IPCData *data, const ShimEvent* e) {
    data->plugin_to_shadow = *e;
    gate_open(&data->xfer_ctrl_to_shadow);
}


void shimevent_sendEventToPlugin(struct IPCData *data, const ShimEvent* e) {
    data->shadow_to_plugin = *e;
    gate_open(&data->xfer_ctrl_to_plugin);
}

void shimevent_recvEventFromShadow(struct IPCData *data, ShimEvent* e) {
    gate_pass_and_close(&data->xfer_ctrl_to_plugin);
    *e = data->shadow_to_plugin;
}

void shimevent_recvEventFromPlugin(struct IPCData *data, ShimEvent* e) {
    gate_pass_and_close(&data->xfer_ctrl_to_shadow);
    *e = data->plugin_to_shadow;
}

} // extern "C"
