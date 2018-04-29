/*
 * See LICENSE for licensing information
 */

#include "shd-tgen.h"

struct _TGenGenerator {
    gint refcount;

    TGenPool* peers;
    TGenMarkovModel* streamModel;
    TGenMarkovModel* packetModel;

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

TGenGenerator* tgengenerator_new(const gchar* streamModelPath, const gchar* packetModelPath, TGenPool* peers) {
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
    gen->peers = peers;

    gen->refcount = 1;

    return gen;
}

gboolean tgengenerator_nextStream(TGenGenerator* gen) {
    TGEN_ASSERT(gen);
    return TRUE;
}

