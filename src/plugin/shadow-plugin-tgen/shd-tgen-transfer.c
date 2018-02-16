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
    TGEN_XFER_ERR_TIMEOUT, TGEN_XFER_ERR_STALLOUT, TGEN_XFER_ERR_PROXY, TGEN_XFER_ERR_MISC,
} TGenTransferError;

typedef struct _TGenTransferGetputData {
    gsize ourSize;
    gsize theirSize;
    gsize expectedReceiveBytes;
    GChecksum *ourPayloadChecksum;
    GChecksum *theirPayloadChecksum;
    gboolean doneReadingPayload;
    gboolean doneWritingPayload;
    gboolean sentOurChecksum;
    gboolean receivedTheirChecksum;
} TGenTransferGetputData;

typedef struct _TGenTransferMModelData {
    TGenMModel *mmodel;
    TGenTimer *timer;
    GChecksum *ourPayloadChecksum;
    GChecksum *theirPayloadChecksum;
    GArray *sched;
    gint schedIdx;
    gint descriptor;
    gsize expectedReceiveBytes;
    gboolean timerSet;
    gboolean goneToSleepOnce;
    gboolean doneReadingPayload;
    gboolean doneWritingPayload;
    gboolean sentOurChecksum;
    gboolean receivedTheirChecksum;
} TGenTransferMModelData;

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

    TGenTransferGetputData *getput;
    TGenTransferMModelData *mmodel;

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

static void _tgentransfer_initGetputData(TGenTransfer *transfer,
        gsize ourSize, gsize theirSize) {
    TGEN_ASSERT(transfer);
    g_assert(!transfer->getput); // Yes, assert that it is NULL
    transfer->getput = g_new0(TGenTransferGetputData, 1);
    transfer->getput->ourPayloadChecksum = g_checksum_new(G_CHECKSUM_MD5);
    transfer->getput->theirPayloadChecksum = g_checksum_new(G_CHECKSUM_MD5);
    transfer->getput->ourSize = ourSize;
    transfer->getput->theirSize = theirSize;
}

static void
_tgentransfer_setMModelSchedFromString(TGenTransfer *transfer, gchar *str)
{
    TGEN_ASSERT(transfer);
    g_assert(!transfer->mmodel->sched);
    g_assert(str);
    transfer->mmodel->sched = g_array_new(TRUE, FALSE, 4);
    transfer->mmodel->schedIdx = 0;
    gchar **delays = g_strsplit(str, ",", 0);
    int i = 0;
    gboolean first_item = TRUE;
    gchar *delay = delays[i];
    while (delay) {
        /* Make sure this string is of positive length, but stop at a max
         * length of 10 chars so we don't read garbage. 10 chars isn't really
         * important, but if it somehow became important, it allows for 1000
         * seconds of delay expressed in microseconds */
        if (strnlen(delay, 10) < 1) {
            delay = delays[++i];
            continue;
        }
        /* The first item is the number of bytes we should expect the other
         * side to send us */
        if (first_item) {
            first_item = FALSE;
            transfer->mmodel->expectedReceiveBytes =
                    g_ascii_strtoll(delay, NULL, 10);
        /* The remaining items are delays */
        } else {
            int32_t v = g_ascii_strtoll(delay, NULL, 10);
            g_array_append_val(transfer->mmodel->sched, v);
            transfer->size += TGEN_MMODEL_PACKET_DATA_SIZE;
        }
        delay = delays[++i];
    }
    g_strfreev(delays);
}

static void _tgentransfer_initMModelData(TGenTransfer *transfer,
        TGenMModel *mmodel, gchar *str)
{
    TGEN_ASSERT(transfer);
    g_assert(!transfer->mmodel); // Yes, assert that it is NULL
    transfer->mmodel = g_new0(TGenTransferMModelData, 1);
    transfer->mmodel->ourPayloadChecksum = g_checksum_new(G_CHECKSUM_MD5);
    transfer->mmodel->theirPayloadChecksum = g_checksum_new(G_CHECKSUM_MD5);
    if (mmodel) {
        transfer->mmodel->mmodel = mmodel;
        tgenmmodel_ref(mmodel);
    } else {
        transfer->mmodel->mmodel = NULL;
    }
    if (str) {
        _tgentransfer_setMModelSchedFromString(transfer, str);
    }
}

static void _tgentransfer_freeGetputData(TGenTransfer *transfer) {
    TGEN_ASSERT(transfer);
    if (!transfer->getput) {
        return;
    }
    if (transfer->getput->ourPayloadChecksum) {
        g_checksum_free(transfer->getput->ourPayloadChecksum);
    }
    if (transfer->getput->theirPayloadChecksum) {
        g_checksum_free(transfer->getput->theirPayloadChecksum);
    }
    g_free(transfer->getput);
}

static void _tgentransfer_freeMModelData(TGenTransfer *transfer) {
    TGEN_ASSERT(transfer);
    if (!transfer->mmodel) {
        return;
    }
    if (transfer->mmodel->ourPayloadChecksum) {
        g_checksum_free(transfer->mmodel->ourPayloadChecksum);
    }
    if (transfer->mmodel->theirPayloadChecksum) {
        g_checksum_free(transfer->mmodel->theirPayloadChecksum);
    }
    if (transfer->mmodel->sched) {
        g_array_unref(transfer->mmodel->sched);
    }
    if (transfer->mmodel->timer) {
        tgentimer_unref(transfer->mmodel->timer);
    }
    if (transfer->mmodel->mmodel) {
        tgenmmodel_unref(transfer->mmodel->mmodel);
    }
}

static const gchar* _tgentransfer_typeToString(TGenTransfer* transfer) {
    switch(transfer->type) {
        case TGEN_TYPE_GET: {
            return "GET";
        }
        case TGEN_TYPE_PUT: {
            return "PUT";
        }
        case TGEN_TYPE_GETPUT: {
            return "GETPUT";
        }
        case TGEN_TYPE_MMODEL: {
            return "MMODEL";
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
        case TGEN_XFER_ERR_STALLOUT: {
            return "STALLOUT";
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
        GString *sizeStr = g_string_new(NULL);
        if (transfer->type == TGEN_TYPE_GETPUT && transfer->getput) {
            g_string_printf(sizeStr, "%"G_GSIZE_FORMAT"|%"G_GSIZE_FORMAT,
                    transfer->getput->ourSize, transfer->getput->theirSize);
        } else if (transfer->type == TGEN_TYPE_MMODEL && transfer->mmodel) {
            g_string_printf(sizeStr, "%"G_GSIZE_FORMAT"|%"G_GSIZE_FORMAT,
                    transfer->size, transfer->mmodel->expectedReceiveBytes);
        } else if (transfer->type == TGEN_TYPE_GET || transfer->type == TGEN_TYPE_PUT) {
            g_string_printf(sizeStr, "%"G_GSIZE_FORMAT, transfer->size);
        } else {
            /* Most likely TGEN_TYPE_NONE, but a general good fail safe */
            g_string_printf(sizeStr, "%"G_GSIZE_FORMAT, 0);
        }
        g_string_printf(stringBuffer, "%s,%"G_GSIZE_FORMAT",%s,%s,%s,%s,%"G_GSIZE_FORMAT",state=%s,error=%s",
                transfer->id, transfer->count, transfer->hostname, _tgentransfer_typeToString(transfer),
                sizeStr->str, transfer->remoteName, transfer->remoteCount,
                _tgentransfer_stateToString(transfer->state), _tgentransfer_errorToString(transfer->error));

        transfer->string = g_string_free(stringBuffer, FALSE);
        g_string_free(sizeStr, TRUE);
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
    g_assert(transfer->type == TGEN_TYPE_NONE);

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

            if (!g_ascii_strncasecmp(parts[3], "GETPUT", 6)) {
                /* We are both going to be sending data */
                transfer->type = TGEN_TYPE_GETPUT;
                transfer->events |= TGEN_EVENT_WRITE;
            }
            else if(!g_ascii_strncasecmp(parts[3], "GET", 3)) {
                /* they are trying to GET, then we need to PUT to them */
                transfer->type = TGEN_TYPE_PUT;
                /* we read command, but now need to write payload */
                transfer->events |= TGEN_EVENT_WRITE;
            } else if(!g_ascii_strncasecmp(parts[3], "PUT", 3)) {
                /* they want to PUT, so we will GET from them */
                transfer->type = TGEN_TYPE_GET;
            } else if (!g_ascii_strncasecmp(parts[3], "MMODEL", 6)) {
                transfer->type = TGEN_TYPE_MMODEL;
            } else {
                tgen_critical("error parsing command type '%s'", parts[3]);
                hasError = TRUE;
            }

            if (!hasError && transfer->type != TGEN_TYPE_NONE) {
                if (transfer->type == TGEN_TYPE_GET || transfer->type == TGEN_TYPE_PUT) {
                    transfer->size = (gsize)g_ascii_strtoull(parts[4], NULL, 10);
                    if(transfer->size == 0) {
                        tgen_critical("error parsing command size '%s'", parts[4]);
                        hasError = TRUE;
                    }
                } else if (transfer->type == TGEN_TYPE_GETPUT) {
                    /* other side has sent OURSIZE,THEIRSIZE, but from their
                     * perspective. So from our perspective, the first item is
                     * THEIRSIZE and the second is OURSIZE */
                    gchar **sizeParts = g_strsplit(parts[4], ",", 0);
                    gsize ourSize, theirSize;
                    theirSize = (gsize)g_ascii_strtoull(sizeParts[0], NULL, 10);
                    ourSize = (gsize)g_ascii_strtoull(sizeParts[1], NULL, 10);
                    g_strfreev(sizeParts);
                    _tgentransfer_initGetputData(transfer, ourSize, theirSize);
                } else if (transfer->type == TGEN_TYPE_MMODEL) {
                    _tgentransfer_initMModelData(transfer, NULL, parts[4]);
                } else {
                    g_assert_not_reached();
                }
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
    g_assert(transfer->type != TGEN_TYPE_NONE);

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
            } else if (transfer->state == TGEN_TYPE_GETPUT) {
                transfer->events |= TGEN_EVENT_WRITE;
            } else if (transfer->state == TGEN_TYPE_MMODEL) {
                transfer->events |= TGEN_EVENT_WRITE|TGEN_EVENT_READ;
            }
        }
    } else {
        /* unable to receive entire command, wait for next chance to read */
    }
}

static void _tgentransfer_readPayload(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
    g_assert(transfer->type == TGEN_TYPE_GET
            || transfer->type == TGEN_TYPE_GETPUT
            || transfer->type == TGEN_TYPE_MMODEL);

    guchar buffer[65536];

    /* keep reading until blocked */
    while(TRUE) {
        gsize length;
        if (transfer->type == TGEN_TYPE_GET) {
            length = MIN(65536, (transfer->size - transfer->bytes.payloadRead));
        } else if (transfer->type == TGEN_TYPE_GETPUT) {
            length = MIN(65536, (transfer->getput->theirSize - transfer->bytes.payloadRead));
        } else if (transfer->type == TGEN_TYPE_MMODEL) {
            length = MIN(65536, (transfer->mmodel->expectedReceiveBytes - transfer->bytes.payloadRead));
        } else {
            g_assert_not_reached();
        }

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
                if (transfer->type == TGEN_TYPE_GET) {
                    g_checksum_update(transfer->payloadChecksum, buffer, bytes);
                } else if (transfer->type == TGEN_TYPE_GETPUT) {
                    g_checksum_update(transfer->getput->theirPayloadChecksum, buffer, bytes);
                } else if (transfer->type == TGEN_TYPE_MMODEL) {
                    g_checksum_update(transfer->mmodel->theirPayloadChecksum, buffer, bytes);
                } else {
                    g_assert_not_reached();
                }
                continue;
            }
        } else {
            if (transfer->type == TGEN_TYPE_GET) {
                /* payload done, send the checksum next */
                _tgentransfer_changeState(transfer, TGEN_XFER_CHECKSUM);
                transfer->time.lastPayloadByte = g_get_monotonic_time();
            } else if (transfer->type == TGEN_TYPE_GETPUT) {
                transfer->getput->doneReadingPayload = TRUE;
                if (transfer->getput->doneWritingPayload) {
                    _tgentransfer_changeState(transfer, TGEN_XFER_CHECKSUM);
                    transfer->time.lastPayloadByte = g_get_monotonic_time();
                    transfer->events |= TGEN_EVENT_WRITE;
                }
            } else if (transfer->type == TGEN_TYPE_MMODEL) {
                transfer->mmodel->doneReadingPayload = TRUE;
                if (transfer->mmodel->doneWritingPayload) {
                    _tgentransfer_changeState(transfer, TGEN_XFER_CHECKSUM);
                    transfer->time.lastPayloadByte = g_get_monotonic_time();
                    transfer->events |= TGEN_EVENT_WRITE;
                }
            } else {
                g_assert_not_reached();
            }
        }
        break;
    }
}

static void _tgentransfer_readChecksum(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
    g_assert(transfer->type == TGEN_TYPE_GET
            || transfer->type == TGEN_TYPE_GETPUT
            || transfer->type == TGEN_TYPE_MMODEL);

    if(_tgentransfer_getLine(transfer)) {
        if (transfer->type == TGEN_TYPE_GET) {
            /* transfer is done */
            _tgentransfer_changeState(transfer, TGEN_XFER_SUCCESS);
            transfer->time.checksum = g_get_monotonic_time();
        } else if (transfer->type == TGEN_TYPE_GETPUT) {
            transfer->getput->receivedTheirChecksum = TRUE;
            if (transfer->getput->sentOurChecksum) {
                _tgentransfer_changeState(transfer, TGEN_XFER_SUCCESS);
                transfer->time.checksum = g_get_monotonic_time();
            }
        } else if (transfer->type == TGEN_TYPE_MMODEL) {
            transfer->mmodel->receivedTheirChecksum = TRUE;
            if (transfer->mmodel->sentOurChecksum) {
                _tgentransfer_changeState(transfer, TGEN_XFER_SUCCESS);
                transfer->time.checksum = g_get_monotonic_time();
            }
        } else {
            g_assert_not_reached();
        }

        /* we have read the entire checksum from the other end */
        gssize sha1Length = g_checksum_type_get_length(G_CHECKSUM_MD5);
        g_assert(sha1Length >= 0);
        gchar* computedSum = NULL;
        if (transfer->type == TGEN_TYPE_GET) {
            computedSum = g_strdup(g_checksum_get_string(transfer->payloadChecksum));
        } else if (transfer->type == TGEN_TYPE_GETPUT) {
            computedSum = g_strdup(g_checksum_get_string(transfer->getput->theirPayloadChecksum));
        } else if (transfer->type == TGEN_TYPE_MMODEL) {
            computedSum = g_strdup(g_checksum_get_string(transfer->mmodel->theirPayloadChecksum));
        } else {
            g_assert_not_reached();
        }
        g_assert(computedSum);

        gchar* line = g_string_free(transfer->readBuffer, FALSE);
        transfer->readBuffer = NULL;

        gchar** parts = g_strsplit(line, " ", 0);
        const gchar* receivedSum = parts[1];

        /* check that the sums match */
        if(receivedSum && !g_ascii_strncasecmp(computedSum, receivedSum, (gsize)sha1Length)) {
            tgen_message("transport %s transfer %s MD5 checksums passed: computed=%s received=%s",
                    tgentransport_toString(transfer->transport), _tgentransfer_toString(transfer),
                    computedSum, receivedSum);
        } else if (receivedSum) {
            tgen_message("MD5 checksums failed: computed=%s received=%s", computedSum, receivedSum);
        } else {
            tgen_message("MD5 checksums failed: received sum is NULL");
        }

        g_strfreev(parts);
        g_free(line);
        g_free(computedSum);
    } else {
        /* unable to receive entire checksum, wait for next chance to read */
    }
}

static gboolean
_tgentransfer_getputWantsReadEvents(TGenTransfer *transfer) {
    TGEN_ASSERT(transfer);

    if (transfer->type != TGEN_TYPE_GETPUT) {
        return FALSE;
    } else if (transfer->readBuffer) {
        return TRUE;
    } else if (transfer->state == TGEN_XFER_RESPONSE) {
        return TRUE;
    } else if (!transfer->getput->doneReadingPayload && transfer->state == TGEN_XFER_PAYLOAD) {
        return TRUE;
    } else {
        return FALSE;
    }
}

static gboolean
_tgentransfer_mmodelWantsReadEvents(TGenTransfer *transfer) {
    TGEN_ASSERT(transfer);

    if (transfer->type != TGEN_TYPE_MMODEL) {
        return FALSE;
    } else if (transfer->readBuffer) {
        return TRUE;
    } else if (transfer->state == TGEN_XFER_RESPONSE) {
        return TRUE;
    } else if (!transfer->mmodel->doneReadingPayload && transfer->state == TGEN_XFER_PAYLOAD) {
        return TRUE;
    } else if (!transfer->mmodel->receivedTheirChecksum && transfer->state == TGEN_XFER_CHECKSUM) {
        return TRUE;
    } else {
        return FALSE;
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
    if((transfer->type == TGEN_TYPE_GET
                || transfer->type == TGEN_TYPE_GETPUT
                || transfer->type == TGEN_TYPE_MMODEL)
            && transfer->state == TGEN_XFER_PAYLOAD) {
        _tgentransfer_readPayload(transfer);
    }

    if((transfer->type == TGEN_TYPE_GET
                || transfer->type == TGEN_TYPE_GETPUT
                || transfer->type == TGEN_TYPE_MMODEL)
            && transfer->state == TGEN_XFER_CHECKSUM) {
        _tgentransfer_readChecksum(transfer);
    }

    if(transfer->readBuffer ||
            (transfer->type == TGEN_TYPE_GET && transfer->state != TGEN_XFER_SUCCESS) ||
            (_tgentransfer_getputWantsReadEvents(transfer)) ||
            (_tgentransfer_mmodelWantsReadEvents(transfer))) {
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
    g_assert(transfer->type != TGEN_TYPE_NONE);

    /* buffer the command if we have not done that yet */
    if(!transfer->writeBuffer) {
        transfer->writeBuffer = g_string_new(NULL);
        g_string_printf(transfer->writeBuffer, "%s %s %s %"G_GSIZE_FORMAT" %s ",
                TGEN_AUTH_PW, transfer->hostname, transfer->id, transfer->count,
                _tgentransfer_typeToString(transfer));
        if (transfer->type == TGEN_TYPE_GET || transfer->type == TGEN_TYPE_PUT) {
            g_string_append_printf(transfer->writeBuffer,"%"G_GSIZE_FORMAT,
                transfer->size);
        } else if (transfer->type == TGEN_TYPE_GETPUT) {
            g_string_append_printf(transfer->writeBuffer,
                "%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT,
                transfer->getput->ourSize, transfer->getput->theirSize);
        } else if (transfer->type == TGEN_TYPE_MMODEL) {
            GString *us = g_string_new(NULL);
            GString *them = g_string_new(NULL);
            if (!tgenmmodel_generatePath(transfer->mmodel->mmodel, us, them)) {
                tgen_critical("Unable to generate path and store it in strings");
                g_assert(NULL);
            }
            _tgentransfer_setMModelSchedFromString(transfer, us->str);
            g_string_append_printf(transfer->writeBuffer, "%s", them->str);
            g_string_free(us, TRUE);
            g_string_free(them, TRUE);
        } else {
            g_assert_not_reached();
        }
        g_string_append_printf(transfer->writeBuffer, "\n");
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
    g_assert(transfer->type != TGEN_TYPE_NONE);

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
    g_assert(transfer->type == TGEN_TYPE_GETPUT || transfer->type == TGEN_TYPE_PUT);

    gboolean firstByte = transfer->bytes.payloadWrite == 0 ? TRUE : FALSE;

    /* try to flush any leftover bytes */
    transfer->bytes.payloadWrite += _tgentransfer_flushOut(transfer);

    /* keep writing until blocked */
    while(!transfer->writeBuffer) {
        gsize length;
        if (transfer->type == TGEN_TYPE_PUT) {
            length = MIN(16384, (transfer->size - transfer->bytes.payloadWrite));
        } else {
            length = MIN(16384, (transfer->getput->ourSize - transfer->bytes.payloadWrite));
        }

        if(length > 0) {
            /* we need to send more payload */
            transfer->writeBuffer = _tgentransfer_getRandomString(length);
            if (transfer->type == TGEN_TYPE_PUT) {
                g_checksum_update(transfer->payloadChecksum, (guchar*)transfer->writeBuffer->str,
                        (gssize)transfer->writeBuffer->len);
            } else if (transfer->type == TGEN_TYPE_GETPUT) {
                g_checksum_update(transfer->getput->ourPayloadChecksum,
                        (guchar*)transfer->writeBuffer->str,
                        (gssize)transfer->writeBuffer->len);
            } else {
                g_assert_not_reached();
            }

            transfer->bytes.payloadWrite += _tgentransfer_flushOut(transfer);

            if(firstByte && transfer->bytes.payloadWrite > 0) {
                firstByte = FALSE;
                transfer->time.firstPayloadByte = g_get_monotonic_time();
            }
        } else {
            if (transfer->type == TGEN_TYPE_PUT) {
                /* payload done, send the checksum next */
                _tgentransfer_changeState(transfer, TGEN_XFER_CHECKSUM);
                transfer->time.lastPayloadByte = g_get_monotonic_time();
            } else if (transfer->type == TGEN_TYPE_GETPUT) {
                transfer->getput->doneWritingPayload = TRUE;
                if (transfer->getput->doneReadingPayload) {
                    _tgentransfer_changeState(transfer, TGEN_XFER_CHECKSUM);
                    transfer->time.lastPayloadByte = g_get_monotonic_time();
                    transfer->events |= TGEN_EVENT_READ;
                }
            } else {
                g_assert_not_reached();
            }
            break;
        }
    }
}

static gboolean
_tgentransfer_mmodelOnTimerExpired(gpointer data1, gpointer data2)
{
    TGenTransfer *transfer = (TGenTransfer *)data1;
    TGEN_ASSERT(transfer);
    g_assert(transfer->type == TGEN_TYPE_MMODEL);
    transfer->mmodel->timerSet = FALSE;
    tgen_debug("MModel timer expired. Asking for write events again.");
    tgendriver_setEvents(transfer->data1, transfer->mmodel->descriptor,
            TGEN_EVENT_WRITE);
    /* Meaning YES cancel future callbacks of this function from
     * tgentimer_onEvent. The timer is already not persistent, so this is just
     * extra. */
    return TRUE;
}

static void
_tgentransfer_mmodelStartPause(TGenTransfer *transfer, int32_t micros)
{
    TGEN_ASSERT(transfer);
    g_assert(transfer->type == TGEN_TYPE_MMODEL);
    g_assert(micros >= 0);
    if (!transfer->mmodel->timer) {
        tgentransfer_ref(transfer);
        /* The initial timeout isn't important since we will set it later in
         * the function body */
        transfer->mmodel->timer = tgentimer_new(5000, FALSE,
                _tgentransfer_mmodelOnTimerExpired,
                transfer, NULL, (GDestroyNotify)tgentransfer_unref, NULL);
    }
    g_assert(transfer->mmodel->timer);
    tgen_debug("Setting a MModel timer for %dus", micros);
    tgentimer_settime_micros(transfer->mmodel->timer, micros);
    tgendriver_registerTransferPause(transfer->data1, transfer->mmodel->timer);
    transfer->mmodel->timerSet = TRUE;
    transfer->mmodel->goneToSleepOnce = TRUE;
}

static gboolean
_tgentransfer_mmodelSchedAdvance(TGenTransfer *transfer)
{
    TGEN_ASSERT(transfer);
    g_assert(transfer->type == TGEN_TYPE_MMODEL);
    tgen_debug("Advancing one from idx=%d (len=%d)",
            transfer->mmodel->schedIdx, transfer->mmodel->sched->len);
    if (++transfer->mmodel->schedIdx >= transfer->mmodel->sched->len) {
        return FALSE;
    }
    return TRUE;

}

static void
_tgentransfer_mmodelTryFlushWriteBuffer(TGenTransfer *transfer)
{
    TGEN_ASSERT(transfer);
    g_assert(transfer->type == TGEN_TYPE_MMODEL);
    g_assert(transfer->writeBuffer);
    gboolean firstByte = transfer->bytes.payloadWrite == 0 ? TRUE : FALSE;
    transfer->bytes.payloadWrite += _tgentransfer_flushOut(transfer);
    if (firstByte && transfer->bytes.payloadWrite > 0) {
        transfer->time.firstPayloadByte = g_get_monotonic_time();
    }
}

static void
_tgentransfer_mmodelWriteToBuffer(TGenTransfer *transfer)
{
    TGEN_ASSERT(transfer);
    g_assert(transfer->type == TGEN_TYPE_MMODEL);
    g_assert(!transfer->writeBuffer);
    GString *more_data;
    int32_t delay, cum_delay = 0;
    gsize flushed;

    transfer->writeBuffer = g_string_new(NULL);
    more_data = _tgentransfer_getRandomString(TGEN_MMODEL_PACKET_DATA_SIZE);
    g_string_append(transfer->writeBuffer, more_data->str);
    g_string_free(more_data, TRUE);
    while (_tgentransfer_mmodelSchedAdvance(transfer)) {
        delay = g_array_index(transfer->mmodel->sched, int32_t,
                transfer->mmodel->schedIdx);
        cum_delay += delay;
        if (cum_delay <= TGEN_MMODEL_MICROS_AT_ONCE) {
            more_data = _tgentransfer_getRandomString(TGEN_MMODEL_PACKET_DATA_SIZE);
            g_string_append(transfer->writeBuffer, more_data->str);
            g_string_free(more_data, TRUE);
        } else {
            _tgentransfer_mmodelStartPause(transfer, cum_delay);
            break;
        }
    }
    g_checksum_update(transfer->mmodel->ourPayloadChecksum,
            (guchar*)transfer->writeBuffer->str,
            (gssize)transfer->writeBuffer->len);
}

static void
_tgentransfer_writeMModelPayload(TGenTransfer *transfer)
{
    TGEN_ASSERT(transfer);
    g_assert(transfer->type == TGEN_TYPE_MMODEL);
    if (transfer->writeBuffer) {
        tgen_debug("There's an existing writeBuffer, so going to try to write "
                "it out first");
        _tgentransfer_mmodelTryFlushWriteBuffer(transfer);
    }

    if (!transfer->writeBuffer) {
        if (!transfer->mmodel->timerSet) {
            if (transfer->mmodel->schedIdx < transfer->mmodel->sched->len) {
                if (transfer->mmodel->schedIdx == 0) {
                    int32_t delay = g_array_index(transfer->mmodel->sched,
                            int32_t, transfer->mmodel->schedIdx);
                    if (delay > 0 && !transfer->mmodel->goneToSleepOnce) {
                        tgen_debug("This is the first item but it calls to "
                                "sleep first. So we are not going to write "
                                "this time.");
                        _tgentransfer_mmodelStartPause(transfer, delay);
                    }
                }
                /* Make sure timer still isn't set since it might have been set
                 * if this was the first schedIdx and we needed to sleep */
                if (!transfer->mmodel->timerSet) {
                    tgen_debug("Empty write buffer, no timer set, and not at "
                            "the end of the schedule. Writing more data.");
                    _tgentransfer_mmodelWriteToBuffer(transfer);
                    _tgentransfer_mmodelTryFlushWriteBuffer(transfer);
                }
            }
        } else {
            tgen_debug("Empty write buffer, but timer is set. Trusting that "
                    "when it expires we'll get the writable event again.");
        }
    } else {
        tgen_debug("There's a write buffer already so we aren't going to "
                "write more");
    }

    if (transfer->mmodel->schedIdx >= transfer->mmodel->sched->len &&
            !transfer->writeBuffer) {
        tgen_debug("We're done writing for the MModel!");
        transfer->mmodel->doneWritingPayload = TRUE;
        if (transfer->mmodel->doneReadingPayload) {
            transfer->time.lastPayloadByte = g_get_monotonic_time();
            _tgentransfer_changeState(transfer, TGEN_XFER_CHECKSUM);
            transfer->events |= TGEN_EVENT_READ|TGEN_EVENT_WRITE;
        }
        if (transfer->mmodel->timer) {
            tgentimer_settime_micros(transfer->mmodel->timer, 0);
        }
    }
}

static void _tgentransfer_writeChecksum(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
    g_assert(transfer->type == TGEN_TYPE_GETPUT
            || transfer->type == TGEN_TYPE_PUT
            || transfer->type == TGEN_TYPE_MMODEL);

    /* buffer the checksum if we have not done that yet */
    if(!transfer->writeBuffer) {
        transfer->writeBuffer = g_string_new(NULL);
        if (transfer->type == TGEN_TYPE_PUT) {
            g_string_printf(transfer->writeBuffer, "MD5 %s\n",
                    g_checksum_get_string(transfer->payloadChecksum));
        } else if (transfer->type == TGEN_TYPE_GETPUT) {
            g_string_printf(transfer->writeBuffer, "MD5 %s\n",
                    g_checksum_get_string(transfer->getput->ourPayloadChecksum));
        } else if (transfer->type == TGEN_TYPE_MMODEL) {
            g_string_printf(transfer->writeBuffer, "MD5 %s\n",
                    g_checksum_get_string(transfer->mmodel->ourPayloadChecksum));
        } else {
            g_assert_not_reached();
        }
    }

    _tgentransfer_flushOut(transfer);

    if(!transfer->writeBuffer) {
        if (transfer->type == TGEN_TYPE_PUT) {
            /* entire checksum was sent, we are now done */
            _tgentransfer_changeState(transfer, TGEN_XFER_SUCCESS);
            transfer->time.checksum = g_get_monotonic_time();
        } else if (transfer->type == TGEN_TYPE_GETPUT) {
            transfer->getput->sentOurChecksum = TRUE;
            if (transfer->getput->receivedTheirChecksum) {
                _tgentransfer_changeState(transfer, TGEN_XFER_SUCCESS);
                transfer->time.checksum = g_get_monotonic_time();
            }
        } else if (transfer->type == TGEN_TYPE_MMODEL) {
            transfer->mmodel->sentOurChecksum = TRUE;
            if (transfer->mmodel->receivedTheirChecksum) {
                _tgentransfer_changeState(transfer, TGEN_XFER_SUCCESS);
                transfer->time.checksum = g_get_monotonic_time();
            }
        } else {
            g_assert_not_reached();
        }
    } else {
        /* unable to send entire checksum, wait for next chance to write */
    }
}

static gboolean
_tgentransfer_getputWantsWriteEvents(TGenTransfer *transfer) {
    TGEN_ASSERT(transfer);

    if (transfer->type != TGEN_TYPE_GETPUT) {
        return FALSE;
    } else if (transfer->writeBuffer) {
        return TRUE;
    } else if (transfer->state == TGEN_XFER_COMMAND) {
        return TRUE;
    } else if (!transfer->getput->doneWritingPayload && transfer->state == TGEN_XFER_PAYLOAD) {
        return TRUE;
    } else {
        return FALSE;
    }
}

static gboolean
_tgentransfer_mmodelWantsWriteEvents(TGenTransfer *transfer) {
    TGEN_ASSERT(transfer);

    if (transfer->type != TGEN_TYPE_MMODEL) {
        return FALSE;
    } else if (transfer->writeBuffer) {
        return TRUE;
    } else if (transfer->state == TGEN_XFER_COMMAND) {
        return TRUE;
    } else if (!transfer->mmodel->doneWritingPayload
            && transfer->state == TGEN_XFER_PAYLOAD
            && !transfer->mmodel->timerSet) {
        return TRUE;
    } else if (!transfer->mmodel->sentOurChecksum && transfer->state == TGEN_XFER_CHECKSUM) {
        return TRUE;
    } else {
        return FALSE;
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
    if((transfer->type == TGEN_TYPE_PUT || transfer->type == TGEN_TYPE_GETPUT)
            && transfer->state == TGEN_XFER_PAYLOAD) {
        _tgentransfer_writePayload(transfer);
    } else if (transfer->type == TGEN_TYPE_MMODEL
            && transfer->state == TGEN_XFER_PAYLOAD) {
        _tgentransfer_writeMModelPayload(transfer);
    }

    if((transfer->type == TGEN_TYPE_PUT
                || transfer->type == TGEN_TYPE_GETPUT
                || transfer->type == TGEN_TYPE_MMODEL)
            && transfer->state == TGEN_XFER_CHECKSUM) {
        _tgentransfer_writeChecksum(transfer);
    }

    if(transfer->writeBuffer ||
            (transfer->type == TGEN_TYPE_PUT && transfer->state == TGEN_XFER_PAYLOAD) ||
            (_tgentransfer_getputWantsWriteEvents(transfer)) ||
            (_tgentransfer_mmodelWantsWriteEvents(transfer))) {
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

    g_string_append_printf(buffer, "total-bytes-read=%"G_GSIZE_FORMAT
            " total-bytes-write=%"G_GSIZE_FORMAT" ", transfer->bytes.totalRead,
            transfer->bytes.totalWrite);

    if (transfer->type == TGEN_TYPE_GET || transfer->type == TGEN_TYPE_PUT) {
        gsize payload = transfer->type == TGEN_TYPE_GET ?
                transfer->bytes.payloadRead : transfer->bytes.payloadWrite;
        const gchar* payloadVerb = transfer->type == TGEN_TYPE_GET ?
                "read" : "write";
        gdouble progress = (gdouble)payload / (gdouble)transfer->size * 100.0f;
        g_string_append_printf(buffer, "payload-bytes-%s=%"G_GSIZE_FORMAT"/"
                "%"G_GSIZE_FORMAT" (%.2f%%)", payloadVerb, payload,
                transfer->size, progress);
    } else {
        gsize read, written;
        gsize to_read, to_write;
        gdouble read_progress, write_progress;

        read = transfer->bytes.payloadRead;
        written = transfer->bytes.payloadWrite;

        if (transfer->type == TGEN_TYPE_GETPUT && transfer->getput != NULL) {
            to_read = transfer->getput->theirSize;
            to_write = transfer->getput->ourSize;
        } else if (transfer->type == TGEN_TYPE_MMODEL && transfer->mmodel != NULL) {
            to_read = transfer->mmodel->expectedReceiveBytes;
            to_write = transfer->size;
        } else {
            /* TGEN_TYPE_NONE is a valid state, if the server exists but has
             * yet to receive the command from the client. */
            to_read = 0;
            to_write = 0;
        }

        read_progress = (to_read > 0) ? (gdouble)read / (gdouble)to_read * 100.0f : 0.0f;
        write_progress = (to_write > 0) ? (gdouble)written / (gdouble)to_write * 100.0f : 0.0f;

        g_string_append_printf(buffer, "payload-bytes-read=%"G_GSIZE_FORMAT"/"
                "%"G_GSIZE_FORMAT" (%.2f%%) payload-bytes-write=%"
                G_GSIZE_FORMAT"/%"G_GSIZE_FORMAT" (%.2f%%)",
                read, to_read, read_progress,
                written, to_write, write_progress);
    }

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

        /* return DONE to the io module so it does deregistration */
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

    if (transfer->type == TGEN_TYPE_MMODEL && transfer->mmodel
            && !transfer->mmodel->descriptor) {
        transfer->mmodel->descriptor = descriptor;
    }

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
        transfer->events |= TGEN_EVENT_DONE;
        _tgentransfer_changeState(transfer, TGEN_XFER_ERROR);

        if(transferStalled) {
            _tgentransfer_changeError(transfer, TGEN_XFER_ERR_STALLOUT);
        } else {
            _tgentransfer_changeError(transfer, TGEN_XFER_ERR_TIMEOUT);
        }

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

TGenTransfer* tgentransfer_new(const gchar* idStr, gsize count, TGenTransferType type,
        gsize size, gsize ourSize, gsize theirSize,
        guint64 timeout, guint64 stallout, TGenMModel *mmodel,
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

    if (type == TGEN_TYPE_GETPUT) {
        _tgentransfer_initGetputData(transfer, ourSize, theirSize);
    } else if (type == TGEN_TYPE_MMODEL) {
        _tgentransfer_initMModelData(transfer, mmodel, NULL);
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

    if (transfer->getput) {
        _tgentransfer_freeGetputData(transfer);
    }

    if (transfer->mmodel) {
        _tgentransfer_freeMModelData(transfer);
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
