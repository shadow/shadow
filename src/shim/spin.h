#ifndef SHD_SPIN_H_
#define SHD_SPIN_H_

#include <stddef.h>

#include "shim_event.h"

#ifdef __cplusplus
extern "C" {
#endif

struct IPCData;

struct IPCData* globalIPCDataCreate();
struct IPCData* globalIPCDataMap(const char* name);

const char* globalIPCDataName();

void ipcDataInit(struct IPCData* ipc_data);
void ipcDataInitIdx(size_t idx);

void shimevent_sendEventToShadow(int event_fd, const ShimEvent* e);
void shimevent_sendEventToPlugin(int event_fd, const ShimEvent* e);
void shimevent_recvEventFromShadow(int event_fd, ShimEvent* e);
void shimevent_recvEventFromPlugin(int event_fd, ShimEvent* e);

#ifdef __cplusplus
}
#endif

#endif // SHD_SPIN_H_
