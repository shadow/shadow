/*
 * See LICENSE for licensing information
 */

#ifndef SHD_TGEN_TRANSFER_H_
#define SHD_TGEN_TRANSFER_H_

typedef enum _TGenTransferType TGenTransferType;
enum _TGenTransferType {
	TGEN_TYPE_NONE, TGEN_TYPE_GET, TGEN_TYPE_PUT,
};

typedef enum _TGenTransferProtocol TGenTransferProtocol;
enum _TGenTransferProtocol {
	TGEN_PROTOCOL_NONE, TGEN_PROTOCOL_TCP, TGEN_PROTOCOL_UDP,
	TGEN_PROTOCOL_PIPE, TGEN_PROTOCOL_SOCKETPAIR,
};

typedef struct _TGenTransfer TGenTransfer;

TGenTransfer* tgentransfer_new(guint64 getBytes, guint64 putBytes,
		gboolean isParallel, gboolean isRepeat, TGenPool* peerPool);
void tgentransfer_free(TGenTransfer* transfer);

#endif /* SHD_TGEN_TRANSFER_H_ */
