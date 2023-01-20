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
void ipcData_destroy(struct IPCData* ipc_data);

// After calling this function, the next (or current) call to
// `shimevent_recvEventFromPlugin` or `shimevent_tryRecvEventFromPlugin` will
// return SHD_SHIM_EVENT_PROCESS_DEATH.
//
// This function is thread-safe, and is safe to call at any point in this APIs
// state-machine, e.g. even if the last method called was
// `shimevent_sendEventToShadow`.
void ipcData_markPluginExited(struct IPCData* ipc_data);

size_t ipcData_nbytes();

void shimevent_sendEventToShadow(struct IPCData* data, const ShimEvent* e);
void shimevent_sendEventToPlugin(struct IPCData* data, const ShimEvent* e);
void shimevent_recvEventFromShadow(struct IPCData* data, ShimEvent* e, bool spin);
void shimevent_recvEventFromPlugin(struct IPCData* data, ShimEvent* e);

/*
 * If a message is ready, sets *e to it and returns 0. Otherwise returns -1
 * and sets errno to EAGAIN.
 */
int shimevent_tryRecvEventFromShadow(struct IPCData* data, ShimEvent* e);
int shimevent_tryRecvEventFromPlugin(struct IPCData* data, ShimEvent* e);

#ifdef __cplusplus
}
#endif

#endif // SHD_IPC_H_
