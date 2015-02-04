/*
 * The Shadow Simulator
 * Copyright (c) 2013-2014, John Geddes
 * See LICENSE for licensing information
 */

#include "shadow.h"

typedef enum _BlockStatus BlockStatus;
enum _BlockStatus {
    BLOCK_STATUS_INFLIGHT,
    BLOCK_STATUS_SACKED,
    BLOCK_STATUS_LOST,
    BLOCK_STATUS_RETRANSMITTED,
};

typedef struct _ScoreBoardBlock ScoreBoardBlock;
struct _ScoreBoardBlock {
    /* sequence number of the block/packet */
    gint sequence;
    /* sequence of the next packet to be sent */
    gint nextSend;
    /* retransmission id if the packet has been retransmitted */
    gint retransmissionId;
    /* status of the block */
    BlockStatus status;

    MAGIC_DECLARE;
};

struct _ScoreBoard {
    /* list of blocks in the scoreboard */
    GQueue* blocksQ;

    /* the furthest SACKed sequence number */
    gint fack;
    /* number of packets in the scoreboard which are lost */
    gint fackOut;
    /* retransmission ID counter */
    gint retransmissionId;
    /* the retrans ID of the last (S)ACKed packet */
    gint ackedRetransmissionId;
    /* the last acknowledgment received */
    gint lastAcknowledgment;
    /* number of duplicate ACKs received */
    gint duplicateACKCount;

    MAGIC_DECLARE;
};

static ScoreBoardBlock* _scoreboardblock_new(gint sequence, BlockStatus status) {
    ScoreBoardBlock* block = g_new0(ScoreBoardBlock, 1);
    MAGIC_INIT(block);
    block->sequence = sequence;
    block->status = status;

    return block;
}

static gint _scoreboardblock_compareStatus(ScoreBoardBlock* block1, ScoreBoardBlock* block2) {
    return (block1->status < block2->status ? -1 : block1->status > block2->status ? 1 : 0);
}

static gint _scoreboardblock_compareSequence(ScoreBoardBlock* block1, ScoreBoardBlock* block2) {
    return (block1->sequence < block2->sequence ? -1 : block1->sequence > block2->sequence ? 1 : 0);
}

static gint _scoreboardblock_compareSequenceData(ScoreBoardBlock* block1, ScoreBoardBlock* block2, gpointer userData) {
    return _scoreboardblock_compareSequence(block1, block2);
}

static void _scoreboardblock_free(ScoreBoardBlock* block) {
    MAGIC_ASSERT(block);
    MAGIC_CLEAR(block);
    g_free(block);
}

ScoreBoard* scoreboard_new() {
    ScoreBoard* scoreboard = g_new0(ScoreBoard, 1);
    MAGIC_INIT(scoreboard);

    return scoreboard;
}

void scoreboard_clear(ScoreBoard* scoreboard) {
    MAGIC_ASSERT(scoreboard);

    scoreboard->retransmissionId = 0;
    scoreboard->ackedRetransmissionId = -1;
    scoreboard->fack = 0;
    scoreboard->fackOut = 0;

    /* reset the blocks queue */
    if(scoreboard->blocksQ) {
        g_queue_free_full(scoreboard->blocksQ, (GDestroyNotify)_scoreboardblock_free);
        scoreboard->blocksQ = g_queue_new();
    }
}

void scoreboard_free(ScoreBoard* scoreboard) {
    MAGIC_ASSERT(scoreboard);
    scoreboard_clear(scoreboard);
    g_queue_free(scoreboard->blocksQ);
    MAGIC_CLEAR(scoreboard);
    g_free(scoreboard);
}

static ScoreBoardBlock* _scoreboard_addBlock(ScoreBoard* scoreboard, gint sequence, BlockStatus status) {
    MAGIC_ASSERT(scoreboard);

    ScoreBoardBlock* block = _scoreboardblock_new(sequence, status);
    g_queue_insert_sorted(scoreboard->blocksQ, block, (GCompareDataFunc) _scoreboardblock_compareSequenceData, NULL);

    return block;
}

static ScoreBoardBlock* _scoreboard_findBlock(ScoreBoard* scoreboard, gint sequence) {
    MAGIC_ASSERT(scoreboard);

    ScoreBoardBlock b;
    b.sequence = sequence;
    GList* link = g_queue_find_custom(scoreboard->blocksQ, &b, (GCompareFunc)_scoreboardblock_compareSequence);

    return link ? (ScoreBoardBlock*)link->data : NULL;
}

static gchar* _scoreboard_getStatusString(BlockStatus status) {
    switch(status) {
        case BLOCK_STATUS_INFLIGHT:
            return "INFLIGHT";
        case BLOCK_STATUS_SACKED:
            return "SACKED";
        case BLOCK_STATUS_LOST:
            return "LOST";
        case BLOCK_STATUS_RETRANSMITTED:
            return "RETRANS";
    }
    return "UNKNOWN";
}

static void _scoreboardblock_appendString(ScoreBoardBlock* block, GString* msg) {
    g_string_append_printf(msg, " %d (st=%s nxt=%d rtx=%d)", block->sequence, _scoreboard_getStatusString(block->status), block->nextSend, block->retransmissionId);
}

static void _scoreboard_dump(ScoreBoard* scoreboard) {
    MAGIC_ASSERT(scoreboard);

    GString* msg = g_string_new("");
    g_string_append_printf(msg, "[SCOREBOARD] fack=%d ackRtx=%d |", scoreboard->fack, scoreboard->ackedRetransmissionId);
    g_queue_foreach(scoreboard->blocksQ, (GFunc)_scoreboardblock_appendString, msg);

    message("%s", msg->str);
    g_string_free(msg, TRUE);
}

static gint _scoreboard_compareSack(gconstpointer s1, gconstpointer s2) {
    gint sack1 = GPOINTER_TO_INT(s1);
    gint sack2 = GPOINTER_TO_INT(s2);
    return sack1 < sack2 ? -1 : sack1 > sack2 ? 1 : 0;
}

static void _scoreboard_removeAcked(ScoreBoard* scoreboard, gint32 unacked) {
    MAGIC_ASSERT(scoreboard);

    /* reconstruct the blocks queue to avoid editing it in-place */
    GQueue* updatedBlocksQ = g_queue_new();

    /* keep all blocks that have not been fully ACKed */
    while(!g_queue_is_empty(scoreboard->blocksQ)) {
        ScoreBoardBlock* block = g_queue_pop_head(scoreboard->blocksQ);
        if(block->sequence < ((gint)unacked)) {
            /* the block has been acked, destroy it */
            _scoreboardblock_free(block);
        } else {
            /* the block has not yet been acked, keep it */
            g_queue_push_tail(updatedBlocksQ, block);
        }
    }

    /* clean up the old blocks queue */
    g_queue_free(scoreboard->blocksQ);
    scoreboard->blocksQ = updatedBlocksQ;
}

TCPProcessFlags scoreboard_update(ScoreBoard* scoreboard, GList* selectiveACKs, gint32 unacked, gint32 next) {
    MAGIC_ASSERT(scoreboard);
    utility_assert(unacked <= next);

    TCPProcessFlags flag = TCP_PF_NONE;

    _scoreboard_removeAcked(scoreboard, unacked);

    if(selectiveACKs) {
        selectiveACKs = g_list_sort(selectiveACKs, (GCompareFunc)_scoreboard_compareSack);

        gint firstSeq = (gint)MAX(unacked, GPOINTER_TO_INT(g_list_first(selectiveACKs)->data));
        gint lastSeq = 0;
        if(next > 0) {
            lastSeq = (gint)MIN((next-1), GPOINTER_TO_INT(g_list_last(selectiveACKs)->data));
        }
        scoreboard->fack = MAX(scoreboard->fack, lastSeq);

        /* go through all sequence that might be sacked and update scoreboard */
        for(gint seq = firstSeq; seq <= lastSeq; seq++) {
            gboolean sacked = (gboolean)g_list_find(selectiveACKs, GINT_TO_POINTER(seq));
            BlockStatus status = (sacked ? BLOCK_STATUS_SACKED : BLOCK_STATUS_INFLIGHT);

            ScoreBoardBlock* block = _scoreboard_findBlock(scoreboard, seq);
            if(!block) {
                _scoreboard_addBlock(scoreboard, seq, status);
                flag |= TCP_PF_DATA_SACKED;
            } else if(status == BLOCK_STATUS_SACKED) {
                if(block->status == BLOCK_STATUS_RETRANSMITTED) {
                    scoreboard->ackedRetransmissionId = block->retransmissionId;
                }
                block->status = status;
            }
        }
    }

    /* update duplicate ACK count */
    if(scoreboard->lastAcknowledgment == unacked) {
        scoreboard->duplicateACKCount += 1;
    } else {
        scoreboard->duplicateACKCount = 0;
    }
    scoreboard->lastAcknowledgment = unacked;

    /* go through all the blocks and check if any of the INFLIGHT ones need to be retransmitted */
    for(guint i = 0; i < g_queue_get_length(scoreboard->blocksQ); i++) {
        ScoreBoardBlock* block = (ScoreBoardBlock*)g_queue_peek_nth(scoreboard->blocksQ, i);
        if(!block) continue;

        switch(block->status) {
            case BLOCK_STATUS_INFLIGHT:
                /* checks for 3 duplicate ACKs */
                if(block->sequence <= scoreboard->fack - 4 || 
                   (block->sequence == scoreboard->lastAcknowledgment && scoreboard->duplicateACKCount == 3)) {
                    block->status = BLOCK_STATUS_LOST;
                    scoreboard->fackOut += 1;
                    flag |= TCP_PF_DATA_LOST;
                }
                break;

            case BLOCK_STATUS_RETRANSMITTED:
                if((block->nextSend <= scoreboard->fack) ||
                   (block->retransmissionId + 4 < scoreboard->ackedRetransmissionId)) {
                    block->status = BLOCK_STATUS_LOST;
                    scoreboard->fackOut += 1;
                    flag |= TCP_PF_DATA_LOST;
                }
                break;

            case BLOCK_STATUS_LOST:
                break;

            case BLOCK_STATUS_SACKED:
                break;
        }

    }

    //_scoreboard_dump(scoreboard);

    return flag;
}

gint scoreboard_getNextRetransmit(ScoreBoard* scoreboard) {
    MAGIC_ASSERT(scoreboard);

    ScoreBoardBlock b;
    b.status = BLOCK_STATUS_LOST;
    GList* link = g_queue_find_custom(scoreboard->blocksQ, &b, (GCompareFunc)_scoreboardblock_compareStatus);

    return link ? ((ScoreBoardBlock*)(link->data))->sequence : -1;
}

void scoreboard_markRetransmitted(ScoreBoard* scoreboard, gint sequence, gint nextSend) {
    MAGIC_ASSERT(scoreboard);

    ScoreBoardBlock* block = _scoreboard_findBlock(scoreboard, sequence);
    if(!block) {
        warning("Couldn't find block for sequence %d to mark retransmitted", sequence);
        return;
    }

    scoreboard->fackOut--;
    if(scoreboard->fackOut < 0) {
        warning("fack out is negative at %d with sequence %d and next send %d", scoreboard->fackOut, sequence, nextSend);
    }

    block->status = BLOCK_STATUS_RETRANSMITTED;
    block->nextSend = nextSend;
    block->retransmissionId = scoreboard->retransmissionId;
    scoreboard->retransmissionId++;
}

void scoreboard_packetDropped(ScoreBoard* scoreboard, gint sequence) {
    MAGIC_ASSERT(scoreboard);

    ScoreBoardBlock* block = _scoreboard_findBlock(scoreboard, sequence);
    if(!block) {
        block = _scoreboard_addBlock(scoreboard, sequence, BLOCK_STATUS_INFLIGHT);
    }

    if(block->status != BLOCK_STATUS_INFLIGHT) {
        return;
    }

    block->status = BLOCK_STATUS_LOST;

    scoreboard->fackOut++;
}

static void _scoreboard_markLossHelper(ScoreBoardBlock* block, ScoreBoard* scoreboard) {
    MAGIC_ASSERT(scoreboard);
    MAGIC_ASSERT(block);
    if(block->status != BLOCK_STATUS_SACKED) {
        if(block->status != BLOCK_STATUS_LOST) {
            scoreboard->fackOut += 1;
        }
        block->status = BLOCK_STATUS_LOST;
    }
}

void scoreboard_markLoss(ScoreBoard* scoreboard, gint unacked, gint nextSend) {
    MAGIC_ASSERT(scoreboard);

    g_queue_foreach(scoreboard->blocksQ, (GFunc)_scoreboard_markLossHelper, scoreboard);

    gint start = unacked;
    BlockStatus status = BLOCK_STATUS_LOST;
    if(!g_queue_is_empty(scoreboard->blocksQ)) {
        ScoreBoardBlock* block = g_queue_peek_tail(scoreboard->blocksQ);
        start = block->sequence + 1;
        status = BLOCK_STATUS_INFLIGHT;
    }

    for(gint sequence = start; sequence < nextSend; sequence++) {
        _scoreboard_addBlock(scoreboard, sequence, status);
        if(status == BLOCK_STATUS_LOST) {
            scoreboard->fackOut += 1;
        }
    }

    scoreboard->retransmissionId = 0;
    scoreboard->ackedRetransmissionId = -1;
}
