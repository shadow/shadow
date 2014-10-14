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
    GList* blocks;

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


ScoreBoard* scoreboard_new() {
    ScoreBoard* scoreboard = g_new0(ScoreBoard, 1);
    MAGIC_INIT(scoreboard);

    return scoreboard;
}

ScoreBoardBlock* scoreboardblock_new(gint sequence, BlockStatus status) {
    ScoreBoardBlock* block = g_new0(ScoreBoardBlock, 1);
    MAGIC_INIT(block);
    block->sequence = sequence;
    block->status = status;

    return block;
}

gint _scoreboard_compareBlock(gconstpointer b1, gconstpointer b2) {
    ScoreBoardBlock* block1 = (ScoreBoardBlock*)b1;
    ScoreBoardBlock* block2 = (ScoreBoardBlock*)b2;
    return (block1->sequence < block2->sequence ? -1 : block1->sequence > block2->sequence ? 1 : 0);
}

static ScoreBoardBlock* _scoreboard_addBlock(ScoreBoard* scoreboard, gint sequence, BlockStatus status) {
    MAGIC_ASSERT(scoreboard);

    ScoreBoardBlock* block = scoreboardblock_new(sequence, status);
    scoreboard->blocks = g_list_insert_sorted(scoreboard->blocks, block, (GCompareFunc)_scoreboard_compareBlock);

    return block;
}

static ScoreBoardBlock* _scoreboard_findBlock(ScoreBoard* scoreboard, gint sequence) {
    MAGIC_ASSERT(scoreboard);

    for(GList* link = scoreboard->blocks; link; link = g_list_next(link)) {
        ScoreBoardBlock* block = (ScoreBoardBlock*)link->data;
        if(block->sequence == sequence) {
            return block;
        }
    }

    return NULL;
}

static gchar* _scoreboard_statusString(BlockStatus status) {
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

static void _scoreboard_dump(ScoreBoard* scoreboard) {
    MAGIC_ASSERT(scoreboard);

    GString* msg = g_string_new("");
    g_string_append_printf(msg, "[SCOREBOARD] fack=%d ackRtx=%d |", scoreboard->fack, scoreboard->ackedRetransmissionId);

    for(GList* link = scoreboard->blocks; link; link = g_list_next(link)) {
        ScoreBoardBlock* block = (ScoreBoardBlock*)link->data;
        g_string_append_printf(msg, " %d (st=%s nxt=%d rtx=%d)", block->sequence, _scoreboard_statusString(block->status), block->nextSend, block->retransmissionId);
    }

    message("%s", msg->str);
    g_string_free(msg, TRUE);
}


gint _scoreboard_compareSack(gconstpointer s1, gconstpointer s2) {
    gint sack1 = GPOINTER_TO_INT(s1);
    gint sack2 = GPOINTER_TO_INT(s2);
    return sack1 < sack2 ? -1 : sack1 > sack2 ? 1 : 0;
}

static void _scoreboard_removeAcked(ScoreBoard* scoreboard, gint unacked) {
    MAGIC_ASSERT(scoreboard);

    /* remove any blocks that have been fully ACKed */
    GList* link = scoreboard->blocks;
    while(link) {
        GList* next = g_list_next(link);
        ScoreBoardBlock* block = (ScoreBoardBlock*)link->data;

        /* if block is unACKed, break out of loop */
        if(block->sequence >= unacked) {
            return;
        }

        /* remove block and free it */
        scoreboard->blocks = g_list_delete_link(scoreboard->blocks, link);
        g_free(block);
        link = next;
    }
}

TCPProcessFlags scoreboard_update(ScoreBoard* scoreboard, GList* selectiveACKs, gint unacked) {
    MAGIC_ASSERT(scoreboard);

    TCPProcessFlags flag = TCP_PF_NONE;

    _scoreboard_removeAcked(scoreboard, unacked);

    if(selectiveACKs) {
        selectiveACKs = g_list_sort(selectiveACKs, (GCompareFunc)_scoreboard_compareSack);
        scoreboard->fack = MAX(scoreboard->fack, GPOINTER_TO_INT(g_list_last(selectiveACKs)->data));

        gint firstSeq = unacked;
        gint lastSeq = GPOINTER_TO_INT(g_list_last(selectiveACKs)->data);

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
    for(GList* link = scoreboard->blocks; link; link = g_list_next(link)) {
        ScoreBoardBlock* block = (ScoreBoardBlock*)link->data;

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

void scoreboard_clear(ScoreBoard* scoreboard) {
    MAGIC_ASSERT(scoreboard);

    scoreboard->retransmissionId = 0;
    scoreboard->ackedRetransmissionId = -1;
    scoreboard->fack = 0;
    scoreboard->fackOut = 0;
    if(scoreboard->blocks) {
        g_list_free_full(scoreboard->blocks, g_free);
        scoreboard->blocks = NULL;
    }
}

void scoreboard_free(ScoreBoard* scoreboard) {
    MAGIC_ASSERT(scoreboard);
    scoreboard_clear(scoreboard);
    MAGIC_CLEAR(scoreboard);
    g_free(scoreboard);
}

gint scoreboard_getNextRetransmit(ScoreBoard* scoreboard) {
    MAGIC_ASSERT(scoreboard);
 
    for(GList* link = scoreboard->blocks; link; link = g_list_next(link)) {
        ScoreBoardBlock* block = (ScoreBoardBlock*)link->data;
        if(block->status == BLOCK_STATUS_LOST) {
            return block->sequence;
        }
    }

    return -1; 
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

gboolean scoreboard_isEmpty(ScoreBoard* scoreboard) {
    MAGIC_ASSERT(scoreboard);
    return (!scoreboard->blocks || g_list_length(scoreboard->blocks) == 0);
}

void scoreboard_markLoss(ScoreBoard* scoreboard, gint unacked, gint nextSend) {
    MAGIC_ASSERT(scoreboard);

    for(GList* link = scoreboard->blocks; link; link = g_list_next(link)) {
        ScoreBoardBlock* block = (ScoreBoardBlock*)link->data;

        if(block->status != BLOCK_STATUS_SACKED) {
            if(block->status != BLOCK_STATUS_LOST) {
                scoreboard->fackOut += 1;
            }
            block->status = BLOCK_STATUS_LOST;
        }
    }

    gint start = unacked;
    BlockStatus status = BLOCK_STATUS_LOST;
    if(!scoreboard_isEmpty(scoreboard)) {
        GList* last = g_list_last(scoreboard->blocks);
        ScoreBoardBlock* block = (ScoreBoardBlock*)last->data;
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
