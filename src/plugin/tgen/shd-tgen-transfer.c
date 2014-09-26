/*
 * See LICENSE for licensing information
 */

#include <arpa/inet.h>

#include "shd-tgen.h"

typedef enum _TGenTransferState {
    TGEN_XFER_COMMAND, TGEN_XFER_PAYLOAD, TGEN_XFER_CHECKSUM, TGEN_XFER_DONE, TGEN_XFER_ERROR
} TGenTransferState;

struct _TGenTransfer {
    TGenTransferState state;
    TGenTransferEvent events;

    TGenTransferCommand command;
    gboolean isCommander;
    gchar* commanderName;

	GString* readBuffer;
	gint readBufferOffset;
	GString* writeBuffer;
	gint writeBufferOffset;

	GChecksum* payloadChecksum;

	gchar* string;

	/* track bytes for read/write progress reporting */
	struct {
	    gsize payloadRead;
	    gsize payloadWrite;
	    gsize totalRead;
	    gsize totalWrite;
	} bytes;

	/* track timings for time reporting */
	struct {
        gint64 start;
        gint64 command;
        gint64 firstPayloadByte;
        gint64 lastPayloadByte;
        gint64 checksum;
        gint64 lastBytesStatusReport;
        gint64 lastTimeStatusReport;
	} time;

    gint refcount;
	guint magic;
};

static const gchar* _tgentransfer_typeToString(TGenTransfer* transfer) {
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

static const gchar* _tgentransfer_stateToString(TGenTransferState state) {
    switch(state) {
        case TGEN_XFER_COMMAND: {
            return "COMMAND";
        }
        case TGEN_XFER_PAYLOAD: {
            return "PAYLOAD";
        }
        case TGEN_XFER_CHECKSUM: {
            return "CHECKSUM";
        }
        case TGEN_XFER_DONE: {
            return "DONE";
        }
        case TGEN_XFER_ERROR:
        default: {
            return "ERROR";
        }
    }
}

static gchar* _tgentransfer_toString(TGenTransfer* transfer) {
    GString* stringBuffer = g_string_new(NULL);
    g_string_printf(stringBuffer, "[%s-%"G_GSIZE_FORMAT"-%s-%"G_GSIZE_FORMAT"]",
            transfer->commanderName, transfer->command.id,
            _tgentransfer_typeToString(transfer), transfer->command.size);
    return g_string_free(stringBuffer, FALSE);
}

TGenTransfer* tgentransfer_new(gchar* commanderName, TGenTransferCommand* command) {
    TGenTransfer* transfer = g_new0(TGenTransfer, 1);
    transfer->magic = TGEN_MAGIC;
    transfer->refcount = 1;

    transfer->time.start = g_get_monotonic_time();

    transfer->events = TGEN_EVENT_READ;
    if(command) {
        transfer->command = *command;
        transfer->isCommander = TRUE;
    }

    if(commanderName) {
        transfer->commanderName = g_strdup(commanderName);
    }

    transfer->payloadChecksum = g_checksum_new(G_CHECKSUM_MD5);
    transfer->string = _tgentransfer_toString(transfer);

    return transfer;
}

static void _tgentransfer_free(TGenTransfer* transfer) {
	TGEN_ASSERT(transfer);

	if(transfer->string) {
	    g_free(transfer->string);
	}

	if(transfer->commanderName) {
        g_free(transfer->commanderName);
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

void tgentransfer_unref(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
    transfer->refcount--;
    if(transfer->refcount <= 0) {
        _tgentransfer_free(transfer);
    }
}

static void _tgentransfer_changeState(TGenTransfer* transfer, TGenTransferState state) {
    TGEN_ASSERT(transfer);
    tgen_info("transfer %s moving from state %s to state %s", transfer->string,
            _tgentransfer_stateToString(transfer->state), _tgentransfer_stateToString(state));
    transfer->state = state;
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
            _tgentransfer_changeState(transfer, TGEN_XFER_ERROR);
            tgen_critical("read(): transfer %s socket %i error %i: %s",
                    transfer->string, socketD, errno, g_strerror(errno));
        } else if(bytes == 0) {
            _tgentransfer_changeState(transfer, TGEN_XFER_ERROR);
            tgen_critical("read(): transfer %s socket %i closed unexpectedly",
                        transfer->string, socketD);
        } else if(bytes == 1) {
            transfer->bytes.totalRead += 1;
            if(c == '\n') {
                tgen_debug("finished receiving line: '%s'", transfer->readBuffer->str);
                return TRUE;
            }
            g_string_append_c(transfer->readBuffer, c);
        }
    }

    return FALSE;
}

static void _tgentransfer_readCommand(TGenTransfer* transfer, gint socketD) {
    TGEN_ASSERT(transfer);

    if(_tgentransfer_getLine(transfer, socketD)) {
        /* we have read the entire command from the other end */
        gboolean hasError = FALSE;
        transfer->time.command = g_get_monotonic_time();

        gchar* line = g_string_free(transfer->readBuffer, FALSE);
        transfer->readBuffer = NULL;

        gchar** parts = g_strsplit(line, " ", 0);
        if(parts[0] == NULL || parts[1] == NULL || parts[2] == NULL || parts[3] == NULL) {
            tgen_critical("error parsing command '%s'", transfer->readBuffer->str);
            hasError = TRUE;
        } else {
            if(transfer->commanderName) {
                g_free(transfer->commanderName);
            }
            transfer->commanderName = g_strdup(parts[0]);

            transfer->command.id = (gsize)g_ascii_strtoull(parts[1], NULL, 10);
            if(transfer->command.id == 0) {
                tgen_critical("error parsing command ID '%s'", parts[1]);
                hasError = TRUE;
            }

            /* if they are trying to GET, then we need to PUT to them */
            if(!g_ascii_strncasecmp(parts[2], "GET", 3)) {
                transfer->command.type = TGEN_TYPE_PUT;
                /* we read command, but now need to write payload */
                transfer->events |= TGEN_EVENT_WRITE;
            } else if(!g_ascii_strncasecmp(parts[0], "PUT", 3)) {
                transfer->command.type = TGEN_TYPE_GET;
            } else {
                tgen_critical("error parsing command type '%s'", parts[0]);
                hasError = TRUE;
            }

            transfer->command.size = (gsize)g_ascii_strtoull(parts[3], NULL, 10);
            if(transfer->command.size == 0) {
                tgen_critical("error parsing command size '%s'", parts[3]);
                hasError = TRUE;
            }
        }

        g_strfreev(parts);
        g_free(line);

        /* payload phase is next unless there was an error parsing */
        if(hasError) {
            _tgentransfer_changeState(transfer, TGEN_XFER_ERROR);
        } else {
            /* we need to update our string with the new command info */
            if(transfer->string) {
                g_free(transfer->string);
                transfer->string = _tgentransfer_toString(transfer);
            }
            _tgentransfer_changeState(transfer, TGEN_XFER_PAYLOAD);
        }
    } else {
        /* unable to receive entire command, wait for next chance to read */
    }
}

static void _tgentransfer_readPayload(TGenTransfer* transfer, gint socketD) {
    TGEN_ASSERT(transfer);

    guchar buffer[65536];

    /* keep reading until blocked */
    while(TRUE) {
        gsize length = MIN(65536, (transfer->command.size - transfer->bytes.payloadRead));

        if(length > 0) {
            /* we need to read more payload */
            gssize bytes = read(socketD, buffer, length);

            if(bytes < 0 && errno != EAGAIN) {
                _tgentransfer_changeState(transfer, TGEN_XFER_ERROR);
                tgen_critical("read(): transfer %s socket %i error %i: %s",
                        transfer->string, socketD, errno, g_strerror(errno));
            } else if(bytes == 0) {
                _tgentransfer_changeState(transfer, TGEN_XFER_ERROR);
                tgen_critical("read(): transfer %s socket %i closed unexpectedly",
                                transfer->string, socketD);
            } else if(bytes > 0) {
                if(transfer->bytes.payloadRead == 0) {
                    transfer->time.firstPayloadByte = g_get_monotonic_time();
                }

                transfer->bytes.payloadRead += bytes;
                transfer->bytes.totalRead += bytes;
                g_checksum_update(transfer->payloadChecksum, buffer, bytes);
                continue;
            }
        } else {
            /* payload done, send the checksum next */
            _tgentransfer_changeState(transfer, TGEN_XFER_CHECKSUM);
            transfer->time.lastPayloadByte = g_get_monotonic_time();
        }
        break;
    }
}

static void _tgentransfer_readChecksum(TGenTransfer* transfer, gint socketD) {
    TGEN_ASSERT(transfer);

    if(_tgentransfer_getLine(transfer, socketD)) {
        /* transfer is done */
        _tgentransfer_changeState(transfer, TGEN_XFER_DONE);
        transfer->time.checksum = g_get_monotonic_time();
        /* no more reads or writes */
        transfer->events = TGEN_EVENT_DONE;

        /* we have read the entire checksum from the other end */
        gssize sha1Length = g_checksum_type_get_length(G_CHECKSUM_MD5);
        g_assert(sha1Length >= 0);
        gchar* computedSum = g_strdup(g_checksum_get_string(transfer->payloadChecksum));

        gchar* line = g_string_free(transfer->readBuffer, FALSE);
        transfer->readBuffer = NULL;

        gchar** parts = g_strsplit(line, " ", 0);
        const gchar* receivedSum = parts[1];
        g_assert(receivedSum);

        /* check that the sums match */
        if(!g_ascii_strncasecmp(computedSum, receivedSum, (gsize)sha1Length)) {
            tgen_message("transfer %s socket %i MD5 checksums passed: computed=%s received=%s",
                    transfer->string, socketD, computedSum, receivedSum);
        } else {
            tgen_message("MD5 checksums failed: computed=%s received=%s", computedSum, receivedSum);
        }

        g_strfreev(parts);
        g_free(line);
        g_free(computedSum);
    } else {
        /* unable to receive entire checksum, wait for next chance to read */
    }
}

static void _tgentransfer_onReadable(TGenTransfer* transfer, gint socketD) {
    TGEN_ASSERT(transfer);

    tgen_debug("active transfer %s is readable", transfer->string);
    gsize startBytes = transfer->bytes.totalRead;

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

    if(transfer->readBuffer ||
            (transfer->command.type == TGEN_TYPE_GET && transfer->state != TGEN_XFER_DONE)) {
        /* we have more to read */
        transfer->events |= TGEN_EVENT_READ;
    } else {
        /* done reading */
        transfer->events &= ~TGEN_EVENT_READ;
    }

    gsize endBytes = transfer->bytes.totalRead;
    tgen_debug("active transfer %s read %"G_GSIZE_FORMAT" more bytes",
            transfer->string, endBytes - startBytes);
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
        _tgentransfer_changeState(transfer, TGEN_XFER_ERROR);
        tgen_critical("write(): transfer %s socket %i error %i: %s",
                transfer->string, socketD, errno, g_strerror(errno));
    } else if(bytes == 0) {
        _tgentransfer_changeState(transfer, TGEN_XFER_ERROR);
        tgen_critical("write(): transfer %s socket %i closed unexpectedly",
                transfer->string, socketD);
    } else if(bytes > 0) {
        transfer->writeBufferOffset += bytes;
        if(transfer->writeBufferOffset >= (transfer->writeBuffer->len - 1)) {
            transfer->writeBufferOffset = 0;
            g_string_free(transfer->writeBuffer, TRUE);
            transfer->writeBuffer = NULL;
        }
        transfer->bytes.totalWrite += bytes;
        return (gsize) bytes;
    }

    return 0;
}

static void _tgentransfer_writeCommand(TGenTransfer* transfer, gint socketD) {
    TGEN_ASSERT(transfer);

    /* buffer the command if we have not done that yet */
    if(!transfer->writeBuffer) {
        transfer->writeBuffer = g_string_new(NULL);
        g_string_printf(transfer->writeBuffer, "%s %"G_GSIZE_FORMAT" %s %"G_GSIZE_FORMAT"\n",
            transfer->commanderName, transfer->command.id,
            _tgentransfer_typeToString(transfer), transfer->command.size);
    }

    _tgentransfer_flushOut(transfer, socketD);

    if(!transfer->writeBuffer) {
        /* entire command was sent, move to payload phase */
        _tgentransfer_changeState(transfer, TGEN_XFER_PAYLOAD);
        transfer->time.command = g_get_monotonic_time();
    } else {
        /* unable to send entire command, wait for next chance to write */
    }
}

static void _tgentransfer_writePayload(TGenTransfer* transfer, gint socketD) {
    TGEN_ASSERT(transfer);

    gboolean firstByte = transfer->bytes.payloadWrite == 0 ? TRUE : FALSE;

    /* try to flush any leftover bytes */
    transfer->bytes.payloadWrite += _tgentransfer_flushOut(transfer, socketD);

    /* keep writing until blocked */
    while(!transfer->writeBuffer) {
        gsize length = MIN(16384, (transfer->command.size - transfer->bytes.payloadWrite));

        if(length > 0) {
            /* we need to send more payload */
            transfer->writeBuffer = _tgentransfer_getRandomString(length);
            g_checksum_update(transfer->payloadChecksum, (guchar*)transfer->writeBuffer->str,
                    (gssize)transfer->writeBuffer->len);

            transfer->bytes.payloadWrite += _tgentransfer_flushOut(transfer, socketD);

            if(firstByte && transfer->bytes.payloadWrite > 0) {
                firstByte = FALSE;
                transfer->time.firstPayloadByte = g_get_monotonic_time();
            }
        } else {
            /* payload done, send the checksum next */
            _tgentransfer_changeState(transfer, TGEN_XFER_CHECKSUM);
            transfer->time.lastPayloadByte = g_get_monotonic_time();
            break;
        }
    }
}

static void _tgentransfer_writeChecksum(TGenTransfer* transfer, gint socketD) {
    TGEN_ASSERT(transfer);

    /* buffer the checksum if we have not done that yet */
    if(!transfer->writeBuffer) {
        transfer->writeBuffer = g_string_new(NULL);
        g_string_printf(transfer->writeBuffer, "MD5 %s\n",
                g_checksum_get_string(transfer->payloadChecksum));
    }

    _tgentransfer_flushOut(transfer, socketD);

    if(!transfer->writeBuffer) {
        /* entire checksum was sent, we are now done */
        _tgentransfer_changeState(transfer, TGEN_XFER_DONE);
        transfer->time.checksum = g_get_monotonic_time();
    } else {
        /* unable to send entire checksum, wait for next chance to write */
    }
}

static void _tgentransfer_onWritable(TGenTransfer* transfer, gint socketD) {
    TGEN_ASSERT(transfer);

    tgen_debug("active transfer %s is writable", transfer->string);
    gsize startBytes = transfer->bytes.totalWrite;

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

    if(transfer->writeBuffer ||
            (transfer->command.type == TGEN_TYPE_PUT && transfer->state != TGEN_XFER_DONE)) {
        /* we have more to write */
        transfer->events |= TGEN_EVENT_WRITE;
    } else {
        /* done writing */
        transfer->events &= ~TGEN_EVENT_WRITE;
    }

    gsize endBytes = transfer->bytes.totalWrite;
    tgen_debug("active transfer %s wrote %"G_GSIZE_FORMAT" more bytes",
                transfer->string, endBytes - startBytes);
}

static gchar* _tgentransfer_getBytesStatusReport(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    GString* buffer = g_string_new(NULL);

    gsize payload = transfer->command.type == TGEN_TYPE_GET ?
            transfer->bytes.payloadRead : transfer->bytes.payloadWrite;
    const gchar* payloadVerb = transfer->command.type == TGEN_TYPE_GET ? "read" : "write";
    gdouble progress = (gdouble)payload / (gdouble)transfer->command.size * 100.0f;

    g_string_printf(buffer,
            "total-bytes-read=%"G_GSIZE_FORMAT" total-bytes-write=%"G_GSIZE_FORMAT" "
            "payload-bytes-%s=%"G_GSIZE_FORMAT"/%"G_GSIZE_FORMAT" (%.2f%%)",
            transfer->bytes.totalRead, transfer->bytes.totalWrite,
            payloadVerb, payload, transfer->command.size, progress);

    transfer->time.lastBytesStatusReport = g_get_monotonic_time();

    return g_string_free(buffer, FALSE);
}

static gchar* _tgentransfer_getTimeStatusReport(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    GString* buffer = g_string_new(NULL);

    /* print the times in milliseconds */
    g_string_printf(buffer,
            "msecs-to-command=%"G_GINT64_FORMAT" msecs-to-first-byte=%"G_GINT64_FORMAT" "
            "msecs-to-last-byte=%"G_GINT64_FORMAT" msecs-to-checksum=%"G_GINT64_FORMAT,
            (transfer->time.command - transfer->time.start) / 1000,
            (transfer->time.firstPayloadByte - transfer->time.start) / 1000,
            (transfer->time.lastPayloadByte - transfer->time.start) / 1000,
            (transfer->time.checksum - transfer->time.start) / 1000);

    transfer->time.lastTimeStatusReport = g_get_monotonic_time();

    return g_string_free(buffer, FALSE);
}

static void _tgentransfer_log(TGenTransfer* transfer, gboolean wasActive) {
    TGEN_ASSERT(transfer);

    if(transfer->state != TGEN_XFER_DONE && wasActive) {
        gboolean timerExpired = g_get_monotonic_time() - transfer->time.lastBytesStatusReport > 1000000;

        if(timerExpired) {
            gchar* bytesMessage = _tgentransfer_getBytesStatusReport(transfer);
            tgen_info("transfer %s transfer-status %s", transfer->string, bytesMessage);
            g_free(bytesMessage);
        }
    }

    if(transfer->state == TGEN_XFER_DONE && transfer->time.lastTimeStatusReport == 0) {
        gchar* bytesMessage = _tgentransfer_getBytesStatusReport(transfer);
        gchar* timeMessage = _tgentransfer_getTimeStatusReport(transfer);
        tgen_message("transfer %s transfer-complete %s %s",
                transfer->string, bytesMessage, timeMessage);
        g_free(bytesMessage);
        g_free(timeMessage);
    }
}

TGenTransferStatus tgentransfer_onSocketEvent(TGenTransfer* transfer, gint socketD, TGenTransferEvent flags) {
    TGEN_ASSERT(transfer);

    gsize readBytesBefore = transfer->bytes.payloadRead;
    gsize writeBytesBefore = transfer->bytes.payloadWrite;

    /* process the incoming events */
    if(flags & TGEN_EVENT_READ) {
        _tgentransfer_onReadable(transfer, socketD);
    }

    if(flags & TGEN_EVENT_WRITE) {
        _tgentransfer_onWritable(transfer, socketD);
    }

    /* update status of bytes transferred and if we still want to read/write */
    TGenTransferStatus status;
    status.events = transfer->events;
    status.bytesRead = transfer->bytes.payloadRead - readBytesBefore;
    status.bytesWritten = transfer->bytes.payloadWrite - writeBytesBefore;

    /* check if we want to log any progress information */
    gboolean wasActive = (status.bytesRead > 0 || status.bytesWritten > 0) ? TRUE : FALSE;
    _tgentransfer_log(transfer, wasActive);

    return status;
}
