/*
 * See LICENSE for licensing information
 */

#ifndef SHD_TGEN_TRANSFER_H_
#define SHD_TGEN_TRANSFER_H_

typedef enum _TGenTransferType {
	TGEN_TYPE_NONE, TGEN_TYPE_GET, TGEN_TYPE_PUT,
} TGenTransferType;

typedef struct _TGenTransfer TGenTransfer;

typedef void (*TGenTransfer_onCompleteFunc)(gpointer data1, gpointer data2, TGenTransfer* transfer);

TGenTransfer* tgentransfer_new(gsize id, TGenTransferType type, gsize size,
        TGenIO* io, TGenTransport* transport,
        TGenTransfer_onCompleteFunc notify, gpointer notifyData1, gpointer notifyData2);
void tgentransfer_ref(TGenTransfer* transfer);
void tgentransfer_unref(TGenTransfer* transfer);

#endif /* SHD_TGEN_TRANSFER_H_ */
