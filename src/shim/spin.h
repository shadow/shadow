#ifndef SHD_SPIN_H_
#define SHD_SPIN_H_

#include <stdatomic.h>
#include <stddef.h>
#include <sched.h>

#include "shim_event.h"

typedef struct _IPCData {

    ShimEvent plugin_to_shadow, shadow_to_plugin;
    atomic_bool xfer_ctrl_to_plugin, xfer_ctrl_to_shadow;

} IPCData;

IPCData *globalIPCDataCreate();
IPCData *globalIPCDataMap(const char *name);

const char *globalIPCDataName();

void ipcDataInit(IPCData *ipc_data);

void shimevent_sendEventToShadow(int event_fd, const ShimEvent* e);
void shimevent_sendEventToPlugin(int event_fd, const ShimEvent* e);
void shimevent_recvEventFromShadow(int event_fd, ShimEvent* e);
void shimevent_recvEventFromPlugin(int event_fd, ShimEvent* e);

#endif // SHD_SPIN_H_
