/*
 * See LICENSE for licensing information
 */

#include <arpa/inet.h>

#include "shd-tgen.h"

/* 60 seconds default timeout */
#define DEFAULT_XFER_TIMEOUT_USEC 60000000
#define DEFAULT_XFER_STALLOUT_USEC 15000000

/* an auth password so we know both sides understand tgen */
#define TGEN_AUTH_PW "T8nNx9L95LATtckJkR5n"

typedef enum _TGenTransferState {
    TGEN_XFER_COMMAND, TGEN_XFER_RESPONSE,
    TGEN_XFER_PAYLOAD, TGEN_XFER_CHECKSUM,
    TGEN_XFER_SUCCESS, TGEN_XFER_ERROR,
} TGenTransferState;

typedef enum _TGenTransferError {
    TGEN_XFER_ERR_NONE, TGEN_XFER_ERR_AUTH, TGEN_XFER_ERR_READ, TGEN_XFER_ERR_WRITE,
    TGEN_XFER_ERR_TIMEOUT, TGEN_XFER_ERR_PROXY, TGEN_XFER_ERR_MISC,
} TGenTransferError;

struct _TGenTransfer {
    /* transfer progress and context information */
    TGenTransferState state;
    TGenTransferError error;
    TGenEvent events;
    gchar* string;
    gint64 timeoutUSecs;
    gint64 stalloutUSecs;

    /* used for authentication */
    guint authIndex;
    gboolean authComplete;
    gboolean authSuccess;

    /* command information */
    gchar* id; // the unique vertex id from the graph
    gsize count; // global transfer count
    TGenTransferType type;
    gsize size;
    gboolean isCommander;
    gchar* hostname;
    gsize remoteCount;
    gchar* remoteName;

    /* socket communication layer and buffers */
    TGenTransport* transport;
    GString* readBuffer;
    gint readBufferOffset;
    GString* writeBuffer;
    gint writeBufferOffset;

    /* a checksum to store bytes received and test transfer integrity */
    GChecksum* payloadChecksum;

    /* track bytes for read/write progress reporting */
    struct {
        gsize payloadRead;
        gsize payloadWrite;
        gsize totalRead;
        gsize totalWrite;
    } bytes;

    /* track timings for time reporting, using g_get_monotonic_time in usec granularity */
    struct {
        gint64 start;
        gint64 command;
        gint64 response;
        gint64 firstPayloadByte;
        gint64 lastPayloadByte;
        gint64 checksum;
        gint64 lastBytesStatusReport;
        gint64 lastTimeStatusReport;
        gint64 lastTimeErrorReport;
        gint64 lastProgress;
    } time;

    /* notification and parameters for when this transfer finishes */
    TGenTransfer_notifyCompleteFunc notify;
    gpointer data1;
    gpointer data2;
    GDestroyNotify destructData1;
    GDestroyNotify destructData2;

    /* memory housekeeping */
    gint refcount;
    guint magic;
};

static const gchar* _tgentransfer_typeToString(TGenTransfer* transfer) {
    switch(transfer->type) {
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
        case TGEN_XFER_RESPONSE: {
            return "RESPONSE";
        }
        case TGEN_XFER_PAYLOAD: {
            return "PAYLOAD";
        }
        case TGEN_XFER_CHECKSUM: {
            return "CHECKSUM";
        }
        case TGEN_XFER_SUCCESS: {
            return "SUCCESS";
        }
        case TGEN_XFER_ERROR:
        default: {
            return "ERROR";
        }
    }
}

static const gchar* _tgentransfer_errorToString(TGenTransferError error) {
    switch(error) {
        case TGEN_XFER_ERR_NONE: {
            return "NONE";
        }
        case TGEN_XFER_ERR_AUTH: {
            return "AUTH";
        }
        case TGEN_XFER_ERR_READ: {
            return "READ";
        }
        case TGEN_XFER_ERR_WRITE: {
            return "WRITE";
        }
        case TGEN_XFER_ERR_TIMEOUT: {
            return "TIMEOUT";
        }
        case TGEN_XFER_ERR_PROXY: {
            return "PROXY";
        }
        case TGEN_XFER_ERR_MISC:
        default: {
            return "MISC";
        }
    }
}

static const gchar* _tgentransfer_toString(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    if(!transfer->string) {
        GString* stringBuffer = g_string_new(NULL);

        g_string_printf(stringBuffer, "%s,%"G_GSIZE_FORMAT",%s,%s,%"G_GSIZE_FORMAT",%s,%"G_GSIZE_FORMAT",state=%s,error=%s",
                transfer->id, transfer->count, transfer->hostname, _tgentransfer_typeToString(transfer),
                transfer->size, transfer->remoteName, transfer->remoteCount,
                _tgentransfer_stateToString(transfer->state), _tgentransfer_errorToString(transfer->error));

        transfer->string = g_string_free(stringBuffer, FALSE);
    }

    return transfer->string;
}

static void _tgentransfer_resetString(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
    if(transfer->string) {
        g_free(transfer->string);
        transfer->string = NULL;
    }
}

static void _tgentransfer_changeState(TGenTransfer* transfer, TGenTransferState state) {
    TGEN_ASSERT(transfer);
    tgen_info("transfer %s moving from state %s to state %s", _tgentransfer_toString(transfer),
            _tgentransfer_stateToString(transfer->state), _tgentransfer_stateToString(state));
    transfer->state = state;
    _tgentransfer_resetString(transfer);
}

static void _tgentransfer_changeError(TGenTransfer* transfer, TGenTransferError error) {
    TGEN_ASSERT(transfer);
    tgen_info("transfer %s moving from error %s to error %s", _tgentransfer_toString(transfer),
            _tgentransfer_errorToString(transfer->error), _tgentransfer_errorToString(error));
    transfer->error = error;
    _tgentransfer_resetString(transfer);
}

static gboolean _tgentransfer_getLine(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    /* create a new buffer if we have not done that yet */
    if(!transfer->readBuffer) {
        transfer->readBuffer = g_string_new(NULL);
    }

    gchar c;
    gssize bytes = 1;

    while(bytes > 0) {
        bytes = tgentransport_read(transfer->transport, &c, 1);

        if(bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            _tgentransfer_changeState(transfer, TGEN_XFER_ERROR);
            _tgentransfer_changeError(transfer, TGEN_XFER_ERR_READ);
            tgen_critical("read(): transport %s transfer %s error %i: %s",
                    tgentransport_toString(transfer->transport), _tgentransfer_toString(transfer),
                    errno, g_strerror(errno));
        } else if(bytes == 0) {
            _tgentransfer_changeState(transfer, TGEN_XFER_ERROR);
            _tgentransfer_changeError(transfer, TGEN_XFER_ERR_READ);
            tgen_critical("read(): transport %s transfer %s closed unexpectedly",
                    tgentransport_toString(transfer->transport), _tgentransfer_toString(transfer));
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

static void _tgentransfer_authenticate(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    while(TRUE) {
        gchar c;
        gssize bytes = tgentransport_read(transfer->transport, &c, 1);

        if(bytes == 1) {
            transfer->bytes.totalRead += 1;

            if(transfer->authIndex == 20) {
                /* we just read the space following the password, so we are now done */
                tgen_info("transfer authentication successful!");
                transfer->authComplete = TRUE;
                transfer->authSuccess = TRUE;
                break;
            }

            g_assert(transfer->authIndex < 20);

            if(c == TGEN_AUTH_PW[transfer->authIndex]) {
                /* this character matched */
                transfer->authIndex++;
            } else {
                /* password doesn't match */
                tgen_info("transfer authentication error: incorrect authentication token");
                transfer->authComplete = TRUE;
                transfer->authSuccess = FALSE;
                break;
            }
        } else if(bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            /* we ran out of bytes for now, but expect more to come */
            transfer->authComplete = FALSE;
            transfer->authSuccess = FALSE;
            break;
        } else if(bytes == 0) {
            /* socket closed */
            tgen_info("transfer authentication error: socket closed before authentication completed");
            transfer->authComplete = TRUE;
            transfer->authSuccess = FALSE;
            break;
        } else {
            /* some type of socket error while reading */
            tgen_info("transfer authentication error: socket read error before authentication completed");
            transfer->authComplete = TRUE;
            transfer->authSuccess = FALSE;
            break;
        }
    }

    if(transfer->authComplete && !transfer->authSuccess) {
        _tgentransfer_changeState(transfer, TGEN_XFER_ERROR);
        _tgentransfer_changeError(transfer, TGEN_XFER_ERR_AUTH);
    }
}

static void _tgentransfer_readCommand(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    if(!transfer->authComplete) {
        _tgentransfer_authenticate(transfer);
        if(!transfer->authComplete || !transfer->authSuccess) {
            return;
        }
    }

    if(_tgentransfer_getLine(transfer)) {
        /* we have read the entire command from the other end */
        gboolean hasError = FALSE;
        transfer->time.command = g_get_monotonic_time();

        gchar* line = g_string_free(transfer->readBuffer, FALSE);
        transfer->readBuffer = NULL;

        /* lets parse the string */
        gchar** parts = g_strsplit(line, " ", 0);
        if(parts[0] == NULL || parts[1] == NULL || parts[2] == NULL || parts[3] == NULL || parts[4] == NULL) {
            tgen_critical("error parsing command '%s'", line);
            hasError = TRUE;
        } else {
            g_assert(!transfer->remoteName);
            transfer->remoteName = g_strdup(parts[0]);

            /* we are not the commander so we should not have an id yet */
            g_assert(transfer->id == NULL);
            transfer->id = g_strdup(parts[1]);

            transfer->remoteCount = (gsize)g_ascii_strtoull(parts[2], NULL, 10);
            if(transfer->remoteCount == 0) {
                tgen_critical("error parsing command ID '%s'", parts[3]);
                hasError = TRUE;
            }

            if(!g_ascii_strncasecmp(parts[3], "GET", 3)) {
                /* they are trying to GET, then we need to PUT to them */
                transfer->type = TGEN_TYPE_PUT;
                /* we read command, but now need to write payload */
                transfer->events |= TGEN_EVENT_WRITE;
            } else if(!g_ascii_strncasecmp(parts[3], "PUT", 3)) {
                /* they want to PUT, so we will GET from them */
                transfer->type = TGEN_TYPE_GET;
            } else {
                tgen_critical("error parsing command type '%s'", parts[3]);
                hasError = TRUE;
            }

            transfer->size = (gsize)g_ascii_strtoull(parts[4], NULL, 10);
            if(transfer->size == 0) {
                tgen_critical("error parsing command size '%s'", parts[4]);
                hasError = TRUE;
            }
        }

        /* free the line from the read buffer */
        if (line != NULL) {
            g_free(line);
        }
        g_strfreev(parts);

        /* payload phase is next unless there was an error parsing */
        if(hasError) {
            _tgentransfer_changeState(transfer, TGEN_XFER_ERROR);
            _tgentransfer_changeError(transfer, TGEN_XFER_ERR_READ);
        } else {
            /* we need to update our string with the new command info */
            _tgentransfer_resetString(transfer);
            _tgentransfer_changeState(transfer, TGEN_XFER_RESPONSE);
            transfer->events |= TGEN_EVENT_WRITE;
        }

    } else {
        /* unable to receive entire command, wait for next chance to read */
    }
}

static void _tgentransfer_readResponse(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    if(!transfer->authComplete) {
        _tgentransfer_authenticate(transfer);
        if(!transfer->authComplete || !transfer->authSuccess) {
            return;
        }
    }

    if(_tgentransfer_getLine(transfer)) {
        /* we have read the entire command from the other end */
        gboolean hasError = FALSE;
        transfer->time.response = g_get_monotonic_time();

        gchar* line = g_string_free(transfer->readBuffer, FALSE);
        transfer->readBuffer = NULL;

        gchar** parts = g_strsplit(line, " ", 0);
        if(parts[0] == NULL || parts[1] == NULL) {
            tgen_critical("error parsing command '%s'", transfer->readBuffer->str);
            hasError = TRUE;
        } else {
            g_assert(!transfer->remoteName);
            transfer->remoteName = g_strdup(parts[0]);

            transfer->remoteCount = (gsize)g_ascii_strtoull(parts[1], NULL, 10);
            if(transfer->remoteCount == 0) {
                tgen_critical("error parsing command ID '%s'", parts[1]);
                hasError = TRUE;
            }
        }

        /* free the line taken from the read buffer */
        if(line != NULL) {
            g_free(line);
        }
        g_strfreev(parts);

        /* payload phase is next unless there was an error parsing */
        if(hasError) {
            _tgentransfer_changeState(transfer, TGEN_XFER_ERROR);
            _tgentransfer_changeError(transfer, TGEN_XFER_ERR_READ);
        } else {
            /* we need to update our string with the new command info */
            _tgentransfer_resetString(transfer);
            _tgentransfer_changeState(transfer, TGEN_XFER_PAYLOAD);
            if(transfer->state == TGEN_TYPE_PUT) {
                transfer->events |= TGEN_EVENT_WRITE;
            }
        }
    } else {
        /* unable to receive entire command, wait for next chance to read */
    }
}

static void _tgentransfer_readPayload(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    guchar buffer[65536];

    /* keep reading until blocked */
    while(TRUE) {
        gsize length = MIN(65536, (transfer->size - transfer->bytes.payloadRead));

        if(length > 0) {
            /* we need to read more payload */
            gssize bytes = tgentransport_read(transfer->transport, buffer, length);

            if(bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                _tgentransfer_changeState(transfer, TGEN_XFER_ERROR);
                _tgentransfer_changeError(transfer, TGEN_XFER_ERR_READ);
                tgen_critical("read(): transport %s transfer %s error %i: %s",
                        tgentransport_toString(transfer->transport), _tgentransfer_toString(transfer),
                        errno, g_strerror(errno));
            } else if(bytes == 0) {
                _tgentransfer_changeState(transfer, TGEN_XFER_ERROR);
                _tgentransfer_changeError(transfer, TGEN_XFER_ERR_READ);
                tgen_critical("read(): transport %s transfer %s closed unexpectedly",
                        tgentransport_toString(transfer->transport), _tgentransfer_toString(transfer));
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

static void _tgentransfer_readChecksum(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    if(_tgentransfer_getLine(transfer)) {
        /* transfer is done */
        _tgentransfer_changeState(transfer, TGEN_XFER_SUCCESS);
        transfer->time.checksum = g_get_monotonic_time();

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
            tgen_message("transport %s transfer %s MD5 checksums passed: computed=%s received=%s",
                    tgentransport_toString(transfer->transport), _tgentransfer_toString(transfer),
                    computedSum, receivedSum);
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

static void _tgentransfer_onReadable(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    tgen_debug("active transfer %s is readable", _tgentransfer_toString(transfer));
    gsize startBytes = transfer->bytes.totalRead;

    /* first check if we need to read a command from the other end */
    if(!transfer->isCommander && transfer->state == TGEN_XFER_COMMAND) {
        _tgentransfer_readCommand(transfer);
    }

    if(transfer->isCommander && transfer->state == TGEN_XFER_RESPONSE) {
        _tgentransfer_readResponse(transfer);
    }

    /* check if we are responsible for reading payload bytes */
    if(transfer->type == TGEN_TYPE_GET && transfer->state == TGEN_XFER_PAYLOAD) {
        _tgentransfer_readPayload(transfer);
    }

    if(transfer->type == TGEN_TYPE_GET && transfer->state == TGEN_XFER_CHECKSUM) {
        _tgentransfer_readChecksum(transfer);
    }

    if(transfer->readBuffer ||
            (transfer->type == TGEN_TYPE_GET && transfer->state != TGEN_XFER_SUCCESS)) {
        /* we have more to read */
        transfer->events |= TGEN_EVENT_READ;
    } else {
        /* done reading */
        transfer->events &= ~TGEN_EVENT_READ;
    }

    gsize endBytes = transfer->bytes.totalRead;
    gsize totalBytes = endBytes - startBytes;
    tgen_debug("active transfer %s read %"G_GSIZE_FORMAT" more bytes",
            _tgentransfer_toString(transfer), totalBytes);

    if(totalBytes > 0) {
        transfer->time.lastProgress = g_get_monotonic_time();
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

static gsize _tgentransfer_flushOut(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    if(!transfer->writeBuffer) {
        return 0;
    }

    gchar* position = &(transfer->writeBuffer->str[transfer->writeBufferOffset]);
    gsize length = transfer->writeBuffer->len - transfer->writeBufferOffset;
    gssize bytes = tgentransport_write(transfer->transport, position, length);

    if(bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        _tgentransfer_changeState(transfer, TGEN_XFER_ERROR);
        _tgentransfer_changeError(transfer, TGEN_XFER_ERR_WRITE);
        tgen_critical("write(): transport %s transfer %s error %i: %s",
                tgentransport_toString(transfer->transport), _tgentransfer_toString(transfer),
                errno, g_strerror(errno));
    } else if(bytes == 0) {
        _tgentransfer_changeState(transfer, TGEN_XFER_ERROR);
        _tgentransfer_changeError(transfer, TGEN_XFER_ERR_WRITE);
        tgen_critical("write(): transport %s transfer %s closed unexpectedly",
                tgentransport_toString(transfer->transport), _tgentransfer_toString(transfer));
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

static void _tgentransfer_writeCommand(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    /* buffer the command if we have not done that yet */
    if(!transfer->writeBuffer) {
        transfer->writeBuffer = g_string_new(NULL);
        g_string_printf(transfer->writeBuffer, "%s %s %s %"G_GSIZE_FORMAT" %s %"G_GSIZE_FORMAT"\n",
            TGEN_AUTH_PW, transfer->hostname, transfer->id, transfer->count, _tgentransfer_typeToString(transfer), transfer->size);
    }

    _tgentransfer_flushOut(transfer);

    if(!transfer->writeBuffer) {
        /* entire command was sent, move to payload phase */
        _tgentransfer_changeState(transfer, TGEN_XFER_RESPONSE);
        transfer->time.command = g_get_monotonic_time();
        transfer->events |= TGEN_EVENT_READ;
    } else {
        /* unable to send entire command, wait for next chance to write */
    }
}

static void _tgentransfer_writeResponse(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    /* buffer the command if we have not done that yet */
    if(!transfer->writeBuffer) {
        transfer->writeBuffer = g_string_new(NULL);
        g_string_printf(transfer->writeBuffer, "%s %s %"G_GSIZE_FORMAT"\n",
                TGEN_AUTH_PW, transfer->hostname, transfer->count);
    }

    _tgentransfer_flushOut(transfer);

    if(!transfer->writeBuffer) {
        /* entire command was sent, move to payload phase */
        _tgentransfer_changeState(transfer, TGEN_XFER_PAYLOAD);
        transfer->time.response = g_get_monotonic_time();
    } else {
        /* unable to send entire command, wait for next chance to write */
    }
}

static void _tgentransfer_writePayload(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    gboolean firstByte = transfer->bytes.payloadWrite == 0 ? TRUE : FALSE;

    /* try to flush any leftover bytes */
    transfer->bytes.payloadWrite += _tgentransfer_flushOut(transfer);

    /* keep writing until blocked */
    while(!transfer->writeBuffer) {
        gsize length = MIN(16384, (transfer->size - transfer->bytes.payloadWrite));

        if(length > 0) {
            /* we need to send more payload */
            transfer->writeBuffer = _tgentransfer_getRandomString(length);
            g_checksum_update(transfer->payloadChecksum, (guchar*)transfer->writeBuffer->str,
                    (gssize)transfer->writeBuffer->len);

            transfer->bytes.payloadWrite += _tgentransfer_flushOut(transfer);

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

static void _tgentransfer_writeChecksum(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    /* buffer the checksum if we have not done that yet */
    if(!transfer->writeBuffer) {
        transfer->writeBuffer = g_string_new(NULL);
        g_string_printf(transfer->writeBuffer, "MD5 %s\n",
                g_checksum_get_string(transfer->payloadChecksum));
    }

    _tgentransfer_flushOut(transfer);

    if(!transfer->writeBuffer) {
        /* entire checksum was sent, we are now done */
        _tgentransfer_changeState(transfer, TGEN_XFER_SUCCESS);
        transfer->time.checksum = g_get_monotonic_time();
    } else {
        /* unable to send entire checksum, wait for next chance to write */
    }
}

static void _tgentransfer_onWritable(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    tgen_debug("active transfer %s is writable", _tgentransfer_toString(transfer));
    gsize startBytes = transfer->bytes.totalWrite;

    /* first check if we need to send a command to the other end */
    if(transfer->isCommander && transfer->state == TGEN_XFER_COMMAND) {
        _tgentransfer_writeCommand(transfer);
    }

    if(!transfer->isCommander && transfer->state == TGEN_XFER_RESPONSE) {
        _tgentransfer_writeResponse(transfer);
    }

    /* check if we are responsible for writing payload bytes */
    if(transfer->type == TGEN_TYPE_PUT && transfer->state == TGEN_XFER_PAYLOAD) {
        _tgentransfer_writePayload(transfer);
    }

    if(transfer->type == TGEN_TYPE_PUT && transfer->state == TGEN_XFER_CHECKSUM) {
        _tgentransfer_writeChecksum(transfer);
    }

    if(transfer->writeBuffer ||
            (transfer->type == TGEN_TYPE_PUT && transfer->state == TGEN_XFER_PAYLOAD)) {
        /* we have more to write */
        transfer->events |= TGEN_EVENT_WRITE;
    } else {
        /* done writing */
        transfer->events &= ~TGEN_EVENT_WRITE;
    }

    gsize endBytes = transfer->bytes.totalWrite;
    gsize totalBytes = endBytes - startBytes;
    tgen_debug("active transfer %s wrote %"G_GSIZE_FORMAT" more bytes",
                _tgentransfer_toString(transfer), totalBytes);

    if(totalBytes > 0) {
        transfer->time.lastProgress = g_get_monotonic_time();
    }
}

static gchar* _tgentransfer_getBytesStatusReport(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    GString* buffer = g_string_new(NULL);

    gsize payload = transfer->type == TGEN_TYPE_GET ?
            transfer->bytes.payloadRead : transfer->bytes.payloadWrite;
    const gchar* payloadVerb = transfer->type == TGEN_TYPE_GET ? "read" : "write";
    gdouble progress = (gdouble)payload / (gdouble)transfer->size * 100.0f;

    g_string_printf(buffer,
            "total-bytes-read=%"G_GSIZE_FORMAT" total-bytes-write=%"G_GSIZE_FORMAT" "
            "payload-bytes-%s=%"G_GSIZE_FORMAT"/%"G_GSIZE_FORMAT" (%.2f%%)",
            transfer->bytes.totalRead, transfer->bytes.totalWrite,
            payloadVerb, payload, transfer->size, progress);

    return g_string_free(buffer, FALSE);
}

static gchar* _tgentransfer_getTimeStatusReport(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    gchar* proxyTimeStr = tgentransport_getTimeStatusReport(transfer->transport);

    gint64 command = (transfer->time.command > 0 && transfer->time.start > 0) ?
            (transfer->time.command - transfer->time.start) : -1;
    gint64 response = (transfer->time.response > 0 && transfer->time.start > 0) ?
            (transfer->time.response - transfer->time.start) : -1;
    gint64 firstPayloadByte = (transfer->time.firstPayloadByte > 0 && transfer->time.start > 0) ?
            (transfer->time.firstPayloadByte - transfer->time.start) : -1;
    gint64 lastPayloadByte = (transfer->time.lastPayloadByte > 0 && transfer->time.start > 0) ?
            (transfer->time.lastPayloadByte - transfer->time.start) : -1;
    gint64 checksum = (transfer->time.checksum > 0 && transfer->time.start > 0) ?
            (transfer->time.checksum - transfer->time.start) : -1;

    GString* buffer = g_string_new(NULL);

    /* print the times in milliseconds */
    g_string_printf(buffer,
            "%s usecs-to-command=%"G_GINT64_FORMAT" usecs-to-response=%"G_GINT64_FORMAT" "
            "usecs-to-first-byte=%"G_GINT64_FORMAT" usecs-to-last-byte=%"G_GINT64_FORMAT" "
            "usecs-to-checksum=%"G_GINT64_FORMAT, proxyTimeStr,
            command, response, firstPayloadByte, lastPayloadByte, checksum);

    g_free(proxyTimeStr);
    return g_string_free(buffer, FALSE);
}

static void _tgentransfer_log(TGenTransfer* transfer, gboolean wasActive) {
    TGEN_ASSERT(transfer);


    if(transfer->state == TGEN_XFER_ERROR) {
        /* we had an error at some point and will unlikely be able to complete.
         * only log an error once. */
        if(transfer->time.lastTimeErrorReport == 0) {
            gchar* bytesMessage = _tgentransfer_getBytesStatusReport(transfer);
            gchar* timeMessage = _tgentransfer_getTimeStatusReport(transfer);

            tgen_message("[transfer-error] transport %s transfer %s %s %s",
                    tgentransport_toString(transfer->transport),
                    _tgentransfer_toString(transfer), bytesMessage, timeMessage);

            gint64 now = g_get_monotonic_time();
            transfer->time.lastBytesStatusReport = now;
            transfer->time.lastTimeErrorReport = now;
            g_free(bytesMessage);
        }
    } else if(transfer->state == TGEN_XFER_SUCCESS) {
        /* we completed the transfer. yay. only log once. */
        if(transfer->time.lastTimeStatusReport == 0) {
            gchar* bytesMessage = _tgentransfer_getBytesStatusReport(transfer);
            gchar* timeMessage = _tgentransfer_getTimeStatusReport(transfer);

            tgen_message("[transfer-complete] transport %s transfer %s %s %s",
                    tgentransport_toString(transfer->transport),
                    _tgentransfer_toString(transfer), bytesMessage, timeMessage);

            gint64 now = g_get_monotonic_time();
            transfer->time.lastBytesStatusReport = now;
            transfer->time.lastTimeStatusReport = now;
            g_free(bytesMessage);
            g_free(timeMessage);
        }
    } else {
        /* the transfer is still working. only log on new activity */
        if(wasActive) {
            gchar* bytesMessage = _tgentransfer_getBytesStatusReport(transfer);

            tgen_info("[transfer-status] transport %s transfer %s %s",
                    tgentransport_toString(transfer->transport),
                    _tgentransfer_toString(transfer), bytesMessage);

            transfer->time.lastBytesStatusReport = g_get_monotonic_time();;
            g_free(bytesMessage);
        }
    }
}

static TGenEvent _tgentransfer_runTransportEventLoop(TGenTransfer* transfer, TGenEvent events) {
    TGenEvent retEvents = tgentransport_onEvent(transfer->transport, events);
    if(retEvents == TGEN_EVENT_NONE) {
        /* proxy failed */
        tgen_critical("proxy connection failed, transfer cannot begin");
        _tgentransfer_changeState(transfer, TGEN_XFER_ERROR);
        _tgentransfer_changeError(transfer, TGEN_XFER_ERR_PROXY);
        _tgentransfer_log(transfer, FALSE);
        return TGEN_EVENT_DONE;
    } else {
        transfer->time.lastProgress = g_get_monotonic_time();
        if(retEvents & TGEN_EVENT_DONE) {
            /* proxy is connected and ready, now its our turn */
            return TGEN_EVENT_READ|TGEN_EVENT_WRITE;
        } else {
            /* proxy in progress */
            return retEvents;
        }
    }
}

static TGenEvent _tgentransfer_runTransferEventLoop(TGenTransfer* transfer, TGenEvent events) {
    gsize readBytesBefore = transfer->bytes.payloadRead;
    gsize writeBytesBefore = transfer->bytes.payloadWrite;

    /* process the events */
    if(events & TGEN_EVENT_READ) {
        _tgentransfer_onReadable(transfer);
    }
    if(events & TGEN_EVENT_WRITE) {
        _tgentransfer_onWritable(transfer);
    }

    /* check if we want to log any progress information */
    gboolean wasActive = (transfer->bytes.payloadRead > readBytesBefore ||
            transfer->bytes.payloadWrite > writeBytesBefore) ? TRUE : FALSE;
    _tgentransfer_log(transfer, wasActive);

    return transfer->events;
}

TGenEvent tgentransfer_onEvent(TGenTransfer* transfer, gint descriptor, TGenEvent events) {
    TGEN_ASSERT(transfer);

    TGenEvent retEvents = TGEN_EVENT_NONE;

    if(transfer->transport && tgentransport_wantsEvents(transfer->transport)) {
        /* transport layer wants to do some IO, redirect as needed */
        retEvents = _tgentransfer_runTransportEventLoop(transfer, events);
    } else {
        /* transport layer is happy, our turn to start the transfer */
        retEvents = _tgentransfer_runTransferEventLoop(transfer, events);
    }

    if((transfer->state == TGEN_XFER_SUCCESS) || (transfer->state == TGEN_XFER_ERROR)) {
        /* send back that we are done */
        transfer->events |= TGEN_EVENT_DONE;
        retEvents |= TGEN_EVENT_DONE;

        if(transfer->notify) {
            /* execute the callback to notify that we are complete */
            gboolean wasSuccess = transfer->error == TGEN_XFER_ERR_NONE ? TRUE : FALSE;
            transfer->notify(transfer->data1, transfer->data2, wasSuccess);
            /* make sure we only do the notification once */
            transfer->notify = NULL;
        }
    }

    return retEvents;
}

gboolean tgentransfer_onCheckTimeout(TGenTransfer* transfer, gint descriptor) {
    TGEN_ASSERT(transfer);

    /* the io module is checking to see if we are in a timeout state. if we are, then
     * the transfer will be cancel will be de-registered and destroyed. */
    gboolean transferStalled = ((transfer->time.lastProgress > 0) &&
            (g_get_monotonic_time() >= transfer->time.lastProgress + transfer->stalloutUSecs)) ? TRUE : FALSE;
    gboolean transferTookTooLong = (g_get_monotonic_time() >= (transfer->time.start + transfer->timeoutUSecs)) ? TRUE : FALSE;
    if(transferStalled || transferTookTooLong) {
        /* log this transfer as a timeout */
        _tgentransfer_changeState(transfer, TGEN_XFER_ERROR);
        _tgentransfer_changeError(transfer, TGEN_XFER_ERR_TIMEOUT);
        transfer->events |= TGEN_EVENT_DONE;
        _tgentransfer_log(transfer, FALSE);

        /* we have to call notify so the next transfer can start */
        if(transfer->notify) {
            /* execute the callback to notify that we failed with a timeout error */
            transfer->notify(transfer->data1, transfer->data2, FALSE);
            /* make sure we only do the notification once */
            transfer->notify = NULL;
        }
        /* this transfer will be destroyed by the io module */
        return TRUE;
    } else {
        /* this transfer is still in progress */
        return FALSE;
    }
}

TGenTransfer* tgentransfer_new(const gchar* idStr, gsize count, TGenTransferType type, gsize size, guint64 timeout, guint64 stallout,
        TGenTransport* transport, TGenTransfer_notifyCompleteFunc notify,
        gpointer data1, gpointer data2, GDestroyNotify destructData1, GDestroyNotify destructData2) {
    TGenTransfer* transfer = g_new0(TGenTransfer, 1);
    transfer->magic = TGEN_MAGIC;
    transfer->refcount = 1;

    transfer->notify = notify;
    transfer->data1 = data1;
    transfer->data2 = data2;
    transfer->destructData1 = destructData1;
    transfer->destructData2 = destructData2;

    transfer->time.start = g_get_monotonic_time();

    transfer->events = TGEN_EVENT_READ;
    transfer->id = g_strdup(idStr);
    transfer->count = count;

    /* the timeout after which we abandon this transfer */
    transfer->timeoutUSecs = (gint64)(timeout > 0 ? (timeout * 1000) : DEFAULT_XFER_TIMEOUT_USEC);
    transfer->stalloutUSecs = (gint64)(stallout > 0 ? (stallout * 1000) : DEFAULT_XFER_STALLOUT_USEC);

    gchar nameBuffer[256];
    memset(nameBuffer, 0, 256);
    transfer->hostname = (0 == gethostname(nameBuffer, 255)) ? g_strdup(nameBuffer) : NULL;

    if(type != TGEN_TYPE_NONE) {
        transfer->isCommander = TRUE;
        transfer->type = type;
        transfer->size = size;
        transfer->events |= TGEN_EVENT_WRITE;
    }

    transfer->payloadChecksum = g_checksum_new(G_CHECKSUM_MD5);

    tgentransport_ref(transport);
    transfer->transport = transport;

    return transfer;
}

static void _tgentransfer_free(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    if(transfer->string) {
        g_free(transfer->string);
    }

    if(transfer->hostname) {
        g_free(transfer->hostname);
    }

    if(transfer->id) {
        g_free(transfer->id);
    }

    if(transfer->remoteName) {
        g_free(transfer->remoteName);
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

    if(transfer->destructData1 && transfer->data1) {
        transfer->destructData1(transfer->data1);
    }

    if(transfer->destructData2 && transfer->data2) {
        transfer->destructData2(transfer->data2);
    }

    if(transfer->transport) {
        tgentransport_unref(transfer->transport);
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
