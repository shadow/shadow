#ifndef SHD_IPC_H_
#define SHD_IPC_H_

#include <stddef.h>

#include "shim_event.h"

/*
 * Message-passing API between plugins and Shadow.
 *
 * (rwails) Currently implemented in C++ because the spinning semaphore uses
 * atomics that are not availble on all platforms in C's <stdatomic.h>.
 *
 * TODO: Port to rust.
 */

#ifdef __cplusplus
extern "C" {
#endif

struct IPCData;

void ipcData_init(struct IPCData* ipc_data, ssize_t spin_max);

size_t ipcData_nbytes();

void shimevent_sendEventToShadow(struct IPCData *data, const ShimEvent* e);
void shimevent_sendEventToPlugin(struct IPCData *data, const ShimEvent* e);
void shimevent_recvEventFromShadow(struct IPCData* data, ShimEvent* e, bool spin);
void shimevent_recvEventFromPlugin(struct IPCData *data, ShimEvent* e);

#ifdef __cplusplus
}
#endif

#endif // SHD_IPC_H_
