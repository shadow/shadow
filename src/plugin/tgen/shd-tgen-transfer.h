/*
 * See LICENSE for licensing information
 */

#ifndef SHD_TGEN_TRANSFER_H_
#define SHD_TGEN_TRANSFER_H_

typedef enum _TGenTransferType {
	TGEN_TYPE_NONE, TGEN_TYPE_GET, TGEN_TYPE_PUT,
} TGenTransferType;

typedef enum _TGenTransferProtocol {
	TGEN_PROTOCOL_NONE, TGEN_PROTOCOL_TCP, TGEN_PROTOCOL_UDP,
	TGEN_PROTOCOL_PIPE, TGEN_PROTOCOL_SOCKETPAIR,
} TGenTransferProtocol;

typedef struct _TGenTransferStatus {
    guint64 bytesRead;
    guint64 bytesWritten;
} TGenTransferStatus;

typedef struct _TGenTransfer TGenTransfer;

TGenTransfer* tgentransfer_newReactive(gint socketD, in_addr_t peerIP, in_port_t peerPort);
TGenTransfer* tgentransfer_newActive(TGenTransferType type, TGenTransferProtocol protocol,
        guint64 size, TGenPool* peerPool, TGenPeer proxy);
void tgentransfer_free(TGenTransfer* transfer);

TGenTransferStatus tgentransfer_activate(TGenTransfer* transfer);
gboolean tgentransfer_isComplete(TGenTransfer* transfer);
gint tgentransfer_getEpollDescriptor(TGenTransfer* transfer);
guint64 tgentransfer_getSize(TGenTransfer* transfer);

#endif /* SHD_TGEN_TRANSFER_H_ */
