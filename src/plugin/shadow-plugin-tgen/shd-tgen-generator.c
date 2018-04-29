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

gboolean tgengenerator_hasOutstandingTransfers(TGenGenerator* gen) {
    TGEN_ASSERT(gen);
    return (gen->numTransfersCreated >= gen->numTransfersCompleted) ? TRUE : FALSE;
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
gboolean tgengenerator_nextStream(TGenGenerator* gen,
        gchar** localSchedule, gchar** remoteSchedule, guint64* pauseTimeUSec) {
    TGEN_ASSERT(gen);

    /* TODO this is a short term hard code for testing other code first */

    if(gen->numStreamsGenerated >= 50) {
        gen->reachedEndState = TRUE;
        return FALSE;
    }

    if(localSchedule) {
        *localSchedule = g_strdup("1000000,1000000");
    }

    if(remoteSchedule) {
        *remoteSchedule = g_strdup("1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000,1000000");
    }

    if(pauseTimeUSec) {
        *pauseTimeUSec = 1000000;
    }

    gen->numStreamsGenerated++;
    gen->numPacketsGenerated += 12;

    return TRUE;
}

