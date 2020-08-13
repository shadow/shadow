#ifndef SHD_SPIN_H_
#define SHD_SPIN_H_

#include <stddef.h>

#include "shim_event.h"

#ifdef __cplusplus
extern "C" {
#endif

struct IPCData;

void ipcData_init(struct IPCData* ipc_data);

size_t ipcData_nbytes();

void shimevent_sendEventToShadow(struct IPCData *data, const ShimEvent* e);
void shimevent_sendEventToPlugin(struct IPCData *data, const ShimEvent* e);
void shimevent_recvEventFromShadow(struct IPCData *data, ShimEvent* e);
void shimevent_recvEventFromPlugin(struct IPCData *data, ShimEvent* e);

#ifdef __cplusplus
}
#endif

#endif // SHD_SPIN_H_
