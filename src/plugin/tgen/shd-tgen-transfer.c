/*
 * See LICENSE for licensing information
 */

#include <arpa/inet.h>

#include "shd-tgen.h"

typedef enum _TGenTransferState {
    TGEN_XFER_COMMAND, TGEN_XFER_PAYLOAD, TGEN_XFER_CHECKSUM, TGEN_XFER_DONE
} TGenTransferState;

struct _TGenTransfer {
    TGenTransferState state;
    TGenTransferCommand command;
    gboolean isCommander;
    gboolean hasError;

    gint refcount;

    guint64 payloadBytesDownloaded;
	guint64 totalBytesDownloaded;
    guint64 payloadBytesUploaded;
	guint64 totalBytesUploaded;

	GString* readBuffer;
	gint readBufferOffset;
	GString* writeBuffer;
	gint writeBufferOffset;

	GChecksum* payloadChecksum;

	gchar* string;

	guint magic;
};

static const gchar* _tgentransfer_commandTypeToString(TGenTransfer* transfer) {
    switch(transfer->command.type) {
        case TGEN_TYPE_GET: {
            return "GET";
        }
        case TGEN_TYPE_PUT: {
            return "PUT";
        }
        case TGEN_TYPE_NONE:
        default: {
            return "NONE";
        }
    }
}

static gchar* _tgentransfer_toString(TGenTransfer* transfer) {
    GString* stringBuffer = g_string_new(NULL);
    g_string_printf(stringBuffer, "[%s-%lu]", _tgentransfer_commandTypeToString(transfer), transfer->command.size);
    return g_string_free(stringBuffer, FALSE);
}

TGenTransfer* tgentransfer_new(TGenTransferCommand* command) {
    TGenTransfer* transfer = g_new0(TGenTransfer, 1);
    transfer->magic = TGEN_MAGIC;
    transfer->refcount = 1;

    if(command) {
        transfer->command = *command;
        transfer->isCommander = TRUE;
        transfer->state = TGEN_XFER_COMMAND;
    }

    transfer->payloadChecksum = g_checksum_new(G_CHECKSUM_SHA1);
    transfer->string = _tgentransfer_toString(transfer);

    return transfer;
}

static void _tgentransfer_free(TGenTransfer* transfer) {
	TGEN_ASSERT(transfer);

	if(transfer->string) {
	    g_free(transfer->string);
	}

	if(transfer->readBuffer) {
	    g_string_free(transfer->readBuffer, TRUE);
	}

    if(transfer->writeBuffer) {
        g_string_free(transfer->writeBuffer, TRUE);
    }

	if(transfer->payloadChecksum) {
	    g_checksum_free(transfer->payloadChecksum);
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

static gboolean _tgentransfer_getLine(TGenTransfer* transfer, gint socketD) {
    TGEN_ASSERT(transfer);

    /* create a new buffer if we have not done that yet */
    if(!transfer->readBuffer) {
        transfer->readBuffer = g_string_new(NULL);
    }

    gchar c;
    gssize bytes = 1;

    while(bytes > 0) {
        bytes = read(socketD, &c, 1);

        if(bytes < 0 && errno != EAGAIN) {
            transfer->hasError = TRUE;
            tgen_warning("transfer %s error %i while reading from socket %i", transfer->string, errno, socketD);
        } else if(bytes == 0) {
            transfer->hasError = TRUE;
            tgen_warning("transfer %s failed socket %i closed", transfer->string, socketD);
        } else if(bytes == 1) {
            g_string_append_c(transfer->readBuffer, c);
            transfer->totalBytesDownloaded += 1;
            if(c == '\n') {
                return TRUE;
            }
        }
    }

    return FALSE;
}

static void _tgentransfer_readCommand(TGenTransfer* transfer, gint socketD) {
    TGEN_ASSERT(transfer);

    if(_tgentransfer_getLine(transfer, socketD)) {
        /* we have read the entire command from the other end */
        // TODO get type and size from line and put into transfer->command

        g_string_free(transfer->readBuffer, TRUE);

        /* payload phase is next */
        transfer->state = TGEN_XFER_PAYLOAD;
    } else {
        /* unable to receive entire command, wait for next chance to read */
    }
}

static void _tgentransfer_readPayload(TGenTransfer* transfer, gint socketD) {
    TGEN_ASSERT(transfer);

    guchar buffer[65536];

    /* keep reading until blocked */
    while(TRUE) {
        gsize length = MIN(65536, (transfer->command.size - transfer->payloadBytesDownloaded));

        if(length > 0) {
            /* we need to read more payload */
            gssize bytes = read(socketD, buffer, length);

            if(bytes < 0 && errno != EAGAIN) {
                transfer->hasError = TRUE;
                tgen_warning("transfer %s error %i while reading from socket %i", transfer->string, errno, socketD);
            } else if(bytes == 0) {
                transfer->hasError = TRUE;
                tgen_warning("transfer %s failed socket %i closed", transfer->string, socketD);
            } else if(bytes > 0) {
                transfer->payloadBytesDownloaded += (guint64)bytes;
                g_checksum_update(transfer->payloadChecksum, buffer, bytes);
                continue;
            }
        } else {
            /* payload done, send the checksum next */
            transfer->state = TGEN_XFER_CHECKSUM;
        }
        break;
    }
}

static void _tgentransfer_readChecksum(TGenTransfer* transfer, gint socketD) {
    TGEN_ASSERT(transfer);

    if(_tgentransfer_getLine(transfer, socketD)) {
        /* we have read the entire checksum from the other end */
        // TODO check that the checksums match

        g_string_free(transfer->readBuffer, TRUE);

        /* transfer is done */
        transfer->state = TGEN_XFER_DONE;
    } else {
        /* unable to receive entire checksum, wait for next chance to read */
    }
}

gboolean tgentransfer_onReadable(TGenTransfer* transfer, gint socketD) {
    TGEN_ASSERT(transfer);

    tgen_debug("active transfer %s is readable", transfer->string);

    /* first check if we need to read a command from the other end */
    if(!transfer->isCommander && transfer->state == TGEN_XFER_COMMAND) {
        _tgentransfer_readCommand(transfer, socketD);
    }

    /* check if we are responsible for reading payload bytes */
    if(transfer->command.type == TGEN_TYPE_GET && transfer->state == TGEN_XFER_PAYLOAD) {
        _tgentransfer_readPayload(transfer, socketD);
    }

    if(transfer->command.type == TGEN_TYPE_GET && transfer->state == TGEN_XFER_CHECKSUM) {
        _tgentransfer_readChecksum(transfer, socketD);
    }

    if(transfer->readBuffer) {
        /* we have more to read */
        return TRUE;
    } else {
        /* done reading */
        return FALSE;
    }
}

static GString* _tgentransfer_getRandomString(gsize size) {
    GString* buffer = g_string_new_len(NULL, (gssize)size);
    for(gint i = 0; i < size; i++) {
        gint n = rand() % 26;
        g_string_append_c(buffer, (gchar)('a' + n));
    }
    return buffer;
}

static gsize _tgentransfer_flushOut(TGenTransfer* transfer, gint socketD) {
    TGEN_ASSERT(transfer);

    if(!transfer->writeBuffer) {
        return 0;
    }

    gchar* position = &(transfer->writeBuffer->str[transfer->writeBufferOffset]);
    gsize length = transfer->writeBuffer->len - transfer->writeBufferOffset;
    gssize bytes = write(socketD, position, length);

    if(bytes < 0 && errno != EAGAIN) {
        transfer->hasError = TRUE;
        tgen_warning("transfer %s error %i while writing to socket %i", transfer->string, errno, socketD);
    } else if(bytes == 0) {
        transfer->hasError = TRUE;
        tgen_warning("transfer %s failed socket %i closed", transfer->string, socketD);
    } else if(bytes > 0) {
        transfer->writeBufferOffset += bytes;
        if(transfer->writeBufferOffset >= (transfer->writeBuffer->len - 1)) {
            transfer->writeBufferOffset = 0;
            g_string_free(transfer->writeBuffer, TRUE);
            transfer->writeBuffer = NULL;
        }
        transfer->totalBytesUploaded += (guint64) bytes;
        return (gsize) bytes;
    }

    return 0;
}

static void _tgentransfer_writeCommand(TGenTransfer* transfer, gint socketD) {
    TGEN_ASSERT(transfer);

    /* buffer the command if we have not done that yet */
    if(!transfer->writeBuffer) {
        transfer->writeBuffer = g_string_new(NULL);
        g_string_printf(transfer->writeBuffer, "%s %lu\n",
            _tgentransfer_commandTypeToString(transfer), transfer->command.size);
    }

    _tgentransfer_flushOut(transfer, socketD);

    if(!transfer->writeBuffer) {
        /* entire command was sent, move to payload phase */
        transfer->state = TGEN_XFER_PAYLOAD;
    } else {
        /* unable to send entire command, wait for next chance to write */
    }
}

static void _tgentransfer_writePayload(TGenTransfer* transfer, gint socketD) {
    TGEN_ASSERT(transfer);

    /* try to flush any leftover bytes */
    transfer->payloadBytesUploaded += (guint64)_tgentransfer_flushOut(transfer, socketD);

    /* keep writing until blocked */
    while(!transfer->writeBuffer) {
        gsize length = MIN(16384, (transfer->command.size - transfer->payloadBytesUploaded));

        if(length > 0) {
            /* we need to send more payload */
            transfer->writeBuffer = _tgentransfer_getRandomString(length);
            g_checksum_update(transfer->payloadChecksum, (guchar*)transfer->writeBuffer->str,
                    (gssize)transfer->writeBuffer->len);
            transfer->payloadBytesUploaded += (guint64)_tgentransfer_flushOut(transfer, socketD);
        } else {
            /* payload done, send the checksum next */
            transfer->state = TGEN_XFER_CHECKSUM;
            break;
        }
    }
}

static void _tgentransfer_writeChecksum(TGenTransfer* transfer, gint socketD) {
    TGEN_ASSERT(transfer);

    /* buffer the checksum if we have not done that yet */
    if(!transfer->writeBuffer) {
        transfer->writeBuffer = g_string_new(NULL);
        g_string_printf(transfer->writeBuffer, "SHA1 %s\n",
                g_checksum_get_string(transfer->payloadChecksum));
    }

    _tgentransfer_flushOut(transfer, socketD);

    if(!transfer->writeBuffer) {
        /* entire checksum was sent, we are now done */
        transfer->state = TGEN_XFER_DONE;
    } else {
        /* unable to send entire checksum, wait for next chance to write */
    }
}

gboolean tgentransfer_onWritable(TGenTransfer* transfer, gint socketD) {
    TGEN_ASSERT(transfer);

    tgen_debug("active transfer %s is writable", transfer->string);

    /* first check if we need to send a command to the other end */
    if(transfer->isCommander && transfer->state == TGEN_XFER_COMMAND) {
        _tgentransfer_writeCommand(transfer, socketD);
    }

    /* check if we are responsible for writing payload bytes */
    if(transfer->command.type == TGEN_TYPE_PUT && transfer->state == TGEN_XFER_PAYLOAD) {
        _tgentransfer_writePayload(transfer, socketD);
    }

    if(transfer->command.type == TGEN_TYPE_PUT && transfer->state == TGEN_XFER_CHECKSUM) {
        _tgentransfer_writeChecksum(transfer, socketD);
    }

    if(transfer->writeBuffer) {
        /* we have more to write */
        return TRUE;
    } else {
        /* done writing */
        return FALSE;
    }
}

gboolean tgentransfer_isComplete(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
    return transfer->state == TGEN_XFER_DONE;
}
