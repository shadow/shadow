/*
 * See LICENSE for licensing information
 */

#include "shd-tgen.h"

struct _TGenGenerator {
    gint refcount;

    TGenMarkovModel* streamModel;
    TGenMarkovModel* packetModel;
    TGenAction* generateAction;

    guint numStreamsGenerated;
    guint numPacketsGenerated;
    gboolean reachedEndState;

    guint numTransfersCreated;
    guint numTransfersCompleted;

    guint magic;
};

static void _tgengenerator_free(TGenGenerator* gen) {
    TGEN_ASSERT(gen);
    g_assert(gen->refcount == 0);

    gen->magic = 0;
    g_free(gen);
}

void tgengenerator_ref(TGenGenerator* gen) {
    TGEN_ASSERT(gen);
    gen->refcount++;
}

void tgengenerator_unref(TGenGenerator* gen) {
    TGEN_ASSERT(gen);
    if(--(gen->refcount) == 0) {
        _tgengenerator_free(gen);
    }
}

TGenGenerator* tgengenerator_new(const gchar* streamModelPath, const gchar* packetModelPath,
        TGenAction* generateAction) {
    TGenMarkovModel* streamModel = tgenmarkovmodel_new(streamModelPath);
    if(!streamModel) {
        tgen_warning("failed to parse stream markov model");
        return NULL;
    }

    TGenMarkovModel* packetModel = tgenmarkovmodel_new(packetModelPath);
    if(!packetModel) {
        tgen_warning("failed to parse packet markov model");
        tgenmarkovmodel_unref(streamModel);
        return NULL;
    }

    TGenGenerator* gen = g_new0(TGenGenerator, 1);
    gen->magic = TGEN_MAGIC;

    gen->streamModel = streamModel;
    gen->packetModel = packetModel;
    gen->generateAction = generateAction;

    gen->refcount = 1;

    return gen;
}

TGenAction* tgengenerator_getGenerateAction(TGenGenerator* gen) {
    TGEN_ASSERT(gen);
    return gen->generateAction;
}

void tgengenerator_onTransferCreated(TGenGenerator* gen) {
    TGEN_ASSERT(gen);
    gen->numTransfersCreated++;
}

void tgengenerator_onTransferCompleted(TGenGenerator* gen) {
    TGEN_ASSERT(gen);
    gen->numTransfersCompleted++;
}

gboolean tgengenerator_isDoneGenerating(TGenGenerator* gen) {
    TGEN_ASSERT(gen);
    return gen->reachedEndState;
}

guint tgengenerator_getNumOutstandingTransfers(TGenGenerator* gen) {
    TGEN_ASSERT(gen);
    return (gen->numTransfersCreated - gen->numTransfersCompleted);
}

guint tgengenerator_getNumStreamsGenerated(TGenGenerator* gen) {
    TGEN_ASSERT(gen);
    return gen->numStreamsGenerated;
}

guint tgengenerator_getNumPacketsGenerated(TGenGenerator* gen) {
    TGEN_ASSERT(gen);
    return gen->numPacketsGenerated;
}

static void _tgengenerator_generatePacketSchedules(TGenGenerator* gen,
        GString* serverBuffer, GString* originBuffer) {
    TGEN_ASSERT(gen);
    g_assert(serverBuffer);
    g_assert(originBuffer);

    gint32 nextServerPacketDelay = 0;
    gint32 nextOriginpacketDelay = 0;

    guint numServerPackets = 0;
    guint numOriginPackets = 0;

    /* make sure the packet model is ready to generate more */
    tgenmarkovmodel_reset(gen->packetModel);

    while(TRUE) {
        tgen_debug("Generating next packet observation");
        guint64 packetDelay = 0;
        Observation obs = tgenmarkovmodel_getNextObservation(gen->packetModel, &packetDelay);

        /* keep track of cumulative delay for each packet. we need this because
         * we are actually computing independent delays for the server and the origin.
         * we take care not to overflow the int32 type. */
        if(packetDelay > INT32_MAX) {
            nextOriginpacketDelay = INT32_MAX;
            nextServerPacketDelay = INT32_MAX;
        } else {
            if(((guint64)nextOriginpacketDelay)+packetDelay > INT32_MAX) {
                nextOriginpacketDelay = INT32_MAX;
            } else {
                nextOriginpacketDelay += (gint32)packetDelay;
            }

            if(((guint64)nextServerPacketDelay)+packetDelay > INT32_MAX) {
                nextServerPacketDelay = INT32_MAX;
            } else {
                nextServerPacketDelay += (gint32)packetDelay;
            }
        }

        /* now build the schedule */
        if(obs == OBSERVATION_PACKET_TO_ORIGIN) {
            tgen_debug("Found packet to origin observation with packet delay %"G_GUINT64_FORMAT, packetDelay);

            /* packet to origin means the server sent it.
             * so add a packet to the server schedule. */
            g_string_append_printf(serverBuffer, "%s%"G_GINT32_FORMAT,
                    serverBuffer->len > 0 ? "," : "", nextServerPacketDelay);

            nextServerPacketDelay = 0;
            numServerPackets++;
            gen->numPacketsGenerated++;
        } else if(obs == OBSERVATION_PACKET_TO_SERVER) {
            tgen_debug("Found packet to server observation with packet delay %"G_GUINT64_FORMAT, packetDelay);

            /* packet to server means the origin sent it.
             * so add a packet to the origin schedule. */
            g_string_append_printf(originBuffer, "%s%"G_GINT32_FORMAT,
                    originBuffer->len > 0 ? "," : "", nextOriginpacketDelay);

            nextOriginpacketDelay = 0;
            numOriginPackets++;
            gen->numPacketsGenerated++;
        } else {
            /* we observed an end state, so the packet stream is done. */
            tgen_debug("Found packet end observation");
            break;
        }
    }

    tgen_info("Generated origin packet schedule of size %"G_GSIZE_FORMAT" "
            "with %u packets (%u bytes) "
            "and server packet schedule of size %"G_GSIZE_FORMAT" "
            "with %u packets (%u bytes)",
            originBuffer->len, numOriginPackets, numOriginPackets*TGEN_MMODEL_PACKET_DATA_SIZE,
            serverBuffer->len, numServerPackets, numServerPackets*TGEN_MMODEL_PACKET_DATA_SIZE);
}

/**
 * Compute the packet schedules for the next stream using the configured
 * markov models, and the pause time that we should wait after this stream
 * is created until we generate the next stream (in microseconds).
 *
 * Following a call to this function, and non-null strings returned to the
 * caller in localSchedule or remoteSchedule are owned and must be free'd
 * by the caller.
 *
 * returns TRUE if another stream should be created. In this case the output
 *         variables will be set appropriately.
 * returns FALSE if we have reached the end of the stream flow for this
 *         iteration of the model. The generator can be unref'd and free'd.
 */
gboolean tgengenerator_generateStream(TGenGenerator* gen,
        gchar** localSchedule, gchar** remoteSchedule, guint64* pauseTimeUSec) {
    TGEN_ASSERT(gen);

    if(gen->reachedEndState) {
        return FALSE;
    }

    tgen_debug("Generating next stream observation");
    guint64 streamDelay = 0;
    Observation obs = tgenmarkovmodel_getNextObservation(gen->streamModel, &streamDelay);

    if(obs == OBSERVATION_STREAM) {
        tgen_debug("Found stream observation with stream delay %"G_GUINT64_FORMAT, streamDelay);

        /* we should create a new stream now, and then wait streamDelay before
         * creating the next one. We need packet schedules for the stream.
         * start the buffers with 100k to avoid too many reallocs. */
        GString* serverBuffer = g_string_sized_new(100000);
        GString* originBuffer = g_string_sized_new(100000);
        _tgengenerator_generatePacketSchedules(gen, serverBuffer, originBuffer);

        if(localSchedule) {
            *localSchedule = g_string_free(originBuffer, FALSE);
        } else {
            g_string_free(originBuffer, TRUE);
        }

        if(remoteSchedule) {
            *remoteSchedule = g_string_free(serverBuffer, FALSE);
        } else {
            g_string_free(serverBuffer, TRUE);
        }

        if(pauseTimeUSec) {
            *pauseTimeUSec = streamDelay;
        }

        gen->numStreamsGenerated++;
        return TRUE;
    } else {
        tgen_debug("Found stream end observation");

        /* we got to the end, we should not create a transfer */
        gen->reachedEndState = TRUE;
        return FALSE;
    }
}

