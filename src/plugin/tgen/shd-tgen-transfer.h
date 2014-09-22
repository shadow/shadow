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

typedef struct _TGenTransferCommand {
    TGenTransferType type;
    guint64 size;
} TGenTransferCommand;

typedef struct _TGenTransfer TGenTransfer;

TGenTransfer* tgentransfer_new(TGenTransferCommand* command);
void tgentransfer_ref(TGenTransfer* transfer);
gboolean tgentransfer_unref(TGenTransfer* transfer);

gboolean tgentransfer_onReadable(TGenTransfer* transfer, gint socketD);
gboolean tgentransfer_onWritable(TGenTransfer* transfer, gint socketD);

gboolean tgentransfer_isComplete(TGenTransfer* transfer);
gboolean tgentransfer_wantsWriteResponse(TGenTransfer* transfer);

#endif /* SHD_TGEN_TRANSFER_H_ */
