/*
 * See LICENSE for licensing information
 */

#include <arpa/inet.h>

#include "shd-tgen.h"

struct _TGenTransfer {
    TGenTransferType type;
    gint refcount;
    gint id;

    guint64 size;
    guint64 payloadBytesDownloaded;
	guint64 totalBytesDownloaded;
    guint64 payloadBytesUploaded;
	guint64 totalBytesUploaded;

	gchar* string;

	guint magic;
};

static gchar* _tgentransfer_toString(TGenTransfer* transfer) {
    gchar* type = NULL;
    switch(transfer->type) {
        case TGEN_TYPE_GET: {
            type = "GET";
            break;
        }
        case TGEN_TYPE_PUT: {
            type = "PUT";
            break;
        }
        case TGEN_TYPE_NONE:
        default: {
            break;
        }
    }

    GString* stringBuffer = g_string_new(NULL);
    g_string_printf(stringBuffer, "[%s-%lu]", type, transfer->size);

    return g_string_free(stringBuffer, FALSE);
}

TGenTransfer* tgentransfer_new(gint transferID, TGenTransferType type, guint64 size) {
    TGenTransfer* transfer = g_new0(TGenTransfer, 1);
    transfer->magic = TGEN_MAGIC;
    transfer->id = transferID;
    transfer->refcount = 1;

    transfer->string = _tgentransfer_toString(transfer);

    return transfer;
}

static void _tgentransfer_free(TGenTransfer* transfer) {
	TGEN_ASSERT(transfer);

	if(transfer->string) {
	    g_free(transfer->string);
	}

	transfer->magic = 0;
	g_free(transfer);
}

void tgentransfer_ref(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
    transfer->refcount++;
}

gboolean tgentransfer_unref(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
    transfer->refcount--;
    if(transfer->refcount == 0) {
        _tgentransfer_free(transfer);
        return TRUE;
    } else {
        return FALSE;
    }
}

static void _tgentransfer_onReadable(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
    //TODO
    tgen_debug("active transfer %s is readable", transfer->string);
}

static void _tgentransfer_onWritable(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
    //TODO
    tgen_debug("active transfer %s is writable", transfer->string);
}

gboolean tgentransfer_isComplete(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    switch(transfer->type) {
        case TGEN_TYPE_GET: {
            return transfer->payloadBytesDownloaded >= transfer->size ? TRUE : FALSE;
        }
        case TGEN_TYPE_PUT: {
            return transfer->payloadBytesUploaded >= transfer->size ? TRUE : FALSE;
        }
        case TGEN_TYPE_NONE:
        default: {
            return FALSE;
        }
    }
}

guint64 tgentransfer_getSize(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
    return transfer->size;
}
