/*
 * See LICENSE for licensing information
 */

#ifndef SHD_TGEN_TRANSFER_H_
#define SHD_TGEN_TRANSFER_H_

typedef enum _TGenTransferType {
	TGEN_TYPE_NONE, TGEN_TYPE_GET, TGEN_TYPE_PUT,
} TGenTransferType;

typedef struct _TGenTransferStatus {
    guint64 bytesRead;
    guint64 bytesWritten;
} TGenTransferStatus;

typedef struct _TGenTransfer TGenTransfer;

TGenTransfer* tgentransfer_new(gint transferID, TGenTransferType type, guint64 size);
void tgentransfer_ref(TGenTransfer* transfer);
gboolean tgentransfer_unref(TGenTransfer* transfer);

gboolean tgentransfer_isComplete(TGenTransfer* transfer);
guint64 tgentransfer_getSize(TGenTransfer* transfer);

#endif /* SHD_TGEN_TRANSFER_H_ */
