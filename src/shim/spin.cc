#include "spin.h"

#include <atomic>

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <sched.h>

#include "main/shmem/shmem_file.h"

#define NBYTES (1ULL << 24)

struct IPCData {

    ShimEvent plugin_to_shadow, shadow_to_plugin;
    std::atomic<bool> xfer_ctrl_to_plugin, xfer_ctrl_to_shadow;

};

static bool _shmemfile_init = false;
static ShMemFile _shmemfile = {0};

static inline void _spinwait(std::atomic<bool> *atm) {
  bool expected = true;

  while (!atm->compare_exchange_strong(expected, false)) {
    sched_yield();
    expected = true;
  }
}

static inline IPCData *_ipcDataGet(int idx) {

    assert(_shmemfile.p);

    IPCData *rv = NULL;

    IPCData *base = (IPCData*)_shmemfile.p;

    return &base[idx];
}

extern "C" {

IPCData *globalIPCDataCreate() {

    if (!_shmemfile_init) {
        shmemfile_alloc(NBYTES, &_shmemfile);
        _shmemfile_init = true;
    }

    return (IPCData *)(_shmemfile.p);
}

IPCData *globalIPCDataMap(const char *name) {
    if (!_shmemfile_init) {
        int rc = shmemfile_map(name, NBYTES, &_shmemfile);
        assert(rc == 0);
        _shmemfile_init = true;
    }

    return (IPCData *)(_shmemfile.p);
}

const char *globalIPCDataName() {
    assert(_shmemfile_init);
    return _shmemfile.name;
}

void ipcDataInitIdx(size_t idx) {
    IPCData *data = _ipcDataGet(idx);
    ipcDataInit(data);
}

void ipcDataInit(IPCData *ipc_data) {
    memset(ipc_data, 0, sizeof(IPCData));

    ipc_data->xfer_ctrl_to_plugin.store(false);
    ipc_data->xfer_ctrl_to_shadow.store(false);
}

void shimevent_sendEventToShadow(int event_fd, const ShimEvent* e) {
    IPCData *data = _ipcDataGet(event_fd);
    data->plugin_to_shadow = *e;
    data->xfer_ctrl_to_shadow.store(true);
}

void shimevent_sendEventToPlugin(int event_fd, const ShimEvent* e) {
    IPCData *data = _ipcDataGet(event_fd);
    data->shadow_to_plugin = *e;
    data->xfer_ctrl_to_plugin.store(true);
}

void shimevent_recvEventFromShadow(int event_fd, ShimEvent* e) {
    IPCData *data = _ipcDataGet(event_fd);
    _spinwait(&data->xfer_ctrl_to_plugin);
    *e = data->shadow_to_plugin;
}

void shimevent_recvEventFromPlugin(int event_fd, ShimEvent* e) {
    IPCData *data = _ipcDataGet(event_fd);
    _spinwait(&data->xfer_ctrl_to_shadow);
    *e = data->plugin_to_shadow;
}

} // extern "C"
