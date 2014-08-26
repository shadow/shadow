/*
 * See LICENSE for licensing information
 */

#include "shd-tgen.h"

struct _TGenTransfer {
    TGenTransferType type;
    TGenTransferProtocol protocol;
	guint64 size;
	TGenPool* peerPool;

	gint epollD;
	gint tcpD;
	gint udpD;
	guint64 nBytesDownloaded;
	guint64 nBytesUploaded;

	gboolean isActive;
	in_addr_t peerIP;
	in_port_t peerPort;

	guint magic;
};

TGenTransfer* tgentransfer_newReactive(gint socketD, in_addr_t peerIP, in_port_t peerPort) {
    TGenTransfer* transfer = g_new0(TGenTransfer, 1);
    transfer->magic = TGEN_MAGIC;

    transfer->tcpD = socketD;
    // todo create epolld, add tcpD to epolld
    transfer->peerIP = peerIP;
    transfer->peerPort = peerPort;

    return transfer;
}

TGenTransfer* tgentransfer_newActive(gint socketD, TGenTransferType type,
        TGenTransferProtocol protocol, guint64 size, TGenPool* peerPool) {
	TGenTransfer* transfer = g_new0(TGenTransfer, 1);
	transfer->magic = TGEN_MAGIC;

	transfer->type = type;
	transfer->protocol = protocol;
	transfer->size = size;
	transfer->peerPool = peerPool;

	// todo create epolld, add tcpD to epolld
	transfer->tcpD = socketD;
	transfer->isActive = TRUE;

	return transfer;
}

void tgentransfer_free(TGenTransfer* transfer) {
	TGEN_ASSERT(transfer);

	if(transfer->peerPool) {
		tgenpool_unref(transfer->peerPool);
	}

	transfer->magic = 0;
	g_free(transfer);
}

static void tgentransfer_onReadable(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
}

static void tgentransfer_onWritable(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
}

void tgentransfer_activate(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    /* collect the event from our epoll descriptor */
    /* collect the events that are ready */
    struct epoll_event epevs[10];
    gint nfds = epoll_wait(transfer->epollD, epevs, 10, 0);
    if (nfds == -1) {
        tgen_critical("error in transfer epoll_wait");
    }

    /* activate correct component for every socket thats ready.
     * either our listening server socket has activity, or one of
     * our transfer sockets. */
    for (gint i = 0; i < nfds; i++) {
        gint desc = epevs[i].data.fd;
        if(desc == transfer->tcpD) {

        } else if(desc == transfer->udpD) {

        }
    }
}

gboolean tgentransfer_isComplete(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
    return FALSE;//transfer->nBytesCompleted >= transfer->size ? TRUE : FALSE;
}

gint tgentransfer_getEpollDescriptor(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
}

guint64 tgentransfer_getSize(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
}
