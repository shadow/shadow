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
    gint start;
    gint end;
    gint nextSend;
    BlockStatus status;
    gint retransmitId;
};

struct _ScoreBoard {
    /* list of blocks in the scoreboard */
    GList* blocks;
    /* link to the next block we want to retransmit */
    ScoreBoardBlock* nextToRetransmit;

    /* the furthest SACKed sequence number */
    gint fack;
    /* number of packets in the scoreboard which are lost */
    gint fackOut;
    /* number of packets in the scoreboard which are SACKed */
    gint sackOut;
    gint retransmitId;
    gint ackedRetransmitId;
    gint lastRetransmitSeq;

    MAGIC_DECLARE;
};


ScoreBoard* scoreboard_new() {
	ScoreBoard* scoreboard = g_new0(ScoreBoard, 1);
	MAGIC_INIT(scoreboard);

    return scoreboard;
}

gint _scoreboard_compareBlock(gconstpointer b1, gconstpointer b2) {
    ScoreBoardBlock* block1 = (ScoreBoardBlock*)b1;
    ScoreBoardBlock* block2 = (ScoreBoardBlock*)b2;
    
    return (block1->start < block2->start ? -1 : block1->start > block2->start ? 1 : 0);
}

static ScoreBoardBlock* _scoreboard_addBlock(ScoreBoard* scoreboard, gint start, gint end, BlockStatus status) {
    MAGIC_ASSERT(scoreboard);
    utility_assert(start <= end);

    ScoreBoardBlock* block = g_new0(ScoreBoardBlock, 1);
    block->start = start;
    block->end = end;
    block->status = status;
    scoreboard->blocks = g_list_insert_sorted(scoreboard->blocks, block, (GCompareFunc)_scoreboard_compareBlock);

    return block;
}

static void _scoreboard_dump(ScoreBoard* scoreboard) {
    MAGIC_ASSERT(scoreboard);

    GString* msg = g_string_new("[SCOREBOARD] ");
    for(GList* blockIter = scoreboard->blocks; blockIter; blockIter = g_list_next(blockIter)) {
        ScoreBoardBlock* block = (ScoreBoardBlock*)blockIter->data;
        g_string_append_printf(msg,"[%d,%d] (%d) ", block->start, block->end, block->status);
    }

    g_string_append_printf(msg, " fack=%d fackout=%d", scoreboard->fack, scoreboard->fackOut);
    message("%s", msg->str);
    g_string_free(msg, TRUE);
}


static void _scoreboard_updateBlock(ScoreBoard* scoreboard, gint start, gint end, BlockStatus status) {
    MAGIC_ASSERT(scoreboard);

    ScoreBoardBlock* block;
    GList* blockIter = scoreboard->blocks;
    while(blockIter) {
        block = (ScoreBoardBlock*)blockIter->data;
        if(block->start == start) {
            break;
        }
        blockIter = g_list_next(blockIter);
    }

    /* only assign status if new block, otherwise might be lost/retransmitted */
    if(!blockIter) {
        block = g_new0(ScoreBoardBlock, 1);
        scoreboard->blocks = g_list_append(scoreboard->blocks, block);
        block->status = status;
    }

    if(block->end == end) {
        return;
    }

    block->start = start;
    block->end = end;
}

static void _scoreboard_mergeBlock(ScoreBoard* scoreboard, ScoreBoardBlock* block) {
    MAGIC_ASSERT(scoreboard);

    GList* link = g_list_find(scoreboard->blocks, block);
    ScoreBoardBlock* nextBlock = (ScoreBoardBlock*)link->data;
    block->end = nextBlock->end;
    scoreboard->blocks = g_list_delete_link(scoreboard->blocks, link);
}


static void _scoreboard_splitBlock(ScoreBoard* scoreboard, ScoreBoardBlock* block, gint sequence) {
    MAGIC_ASSERT(scoreboard);

    gint start = block->start;
    gint end = block->end;
    BlockStatus status = block->status;

    /* create single block */
    block->start = sequence;
    block->end = sequence;
    block->status = BLOCK_STATUS_SACKED;

    /* create new blocks */
    ScoreBoardBlock* newBlock;
    if(sequence - 1 >= start) {
        newBlock = _scoreboard_addBlock(scoreboard, start, sequence - 1, status);
        newBlock->nextSend = block->nextSend;
    }

    if(sequence + 1 <= end) {
        newBlock = _scoreboard_addBlock(scoreboard, sequence + 1, end, status);
        newBlock->nextSend = block->nextSend;
    }
}

gint _scoreboard_compareSack(gconstpointer s1, gconstpointer s2) {
    gint sack1 = GPOINTER_TO_INT(s1);
    gint sack2 = GPOINTER_TO_INT(s2);
    return sack1 < sack2 ? -1 : sack1 > sack2 ? 1 : 0;
}

gboolean scoreboard_update(ScoreBoard* scoreboard, GList* selectiveACKs, gint unacked) {
    MAGIC_ASSERT(scoreboard);

    gboolean dataLoss = FALSE;

    /* remove any blocks that have been fully ACKed */
    GList* blockIter = scoreboard->blocks;
    while(blockIter) {
        GList* next = g_list_next(blockIter);
        ScoreBoardBlock* block = (ScoreBoardBlock*)blockIter->data;

        /* if block is unACKed, break out of loop */
        if(block->start >= unacked) {
            break;
        }

        /* remove block and free it */
        scoreboard->blocks = g_list_delete_link(scoreboard->blocks, blockIter);
        blockIter = next;
    }

    if(selectiveACKs) {
        selectiveACKs = g_list_sort(selectiveACKs, (GCompareFunc)_scoreboard_compareSack);
        scoreboard->fack = MAX(scoreboard->fack, GPOINTER_TO_INT(g_list_last(selectiveACKs)->data));

        gint firstSeq = unacked;
        gint lastSeq = GPOINTER_TO_INT(g_list_last(selectiveACKs)->data);

        /* go through all sequence that might be sacked and update scoreboard */
        for(gint seq = firstSeq; seq <= lastSeq; seq++) {
            gboolean sacked = (gboolean)g_list_find(selectiveACKs, GINT_TO_POINTER(seq));

            ScoreBoardBlock* block = NULL;
            GList* blockIter = g_list_first(scoreboard->blocks);
            while(blockIter) {
                block = (ScoreBoardBlock*)blockIter->data;
                
                if(sacked && block->status == BLOCK_STATUS_SACKED && block->end == seq - 1) {
                    block->end = seq;
                } else if(block->start <= seq && seq <= block->end) {
                    break;
                }

                block = NULL;
                blockIter = g_list_next(blockIter);
            }

            if(!block) {
                BlockStatus status = (sacked ? BLOCK_STATUS_SACKED : BLOCK_STATUS_INFLIGHT);
                _scoreboard_addBlock(scoreboard, seq, seq, status);
            } else {
                /* check if we need to split the block */
                if(sacked && block->status != BLOCK_STATUS_SACKED) {
                    gint start = block->start;
                    gint end = block->end;
                    BlockStatus status = block->status;

                    /* create single block */
                    block->start = seq;
                    block->end = seq;
                    block->status = BLOCK_STATUS_SACKED;

                    /* create new blocks */
                    ScoreBoardBlock* newBlock;
                    if(seq - 1 >= start) {
                        newBlock = _scoreboard_addBlock(scoreboard, start, seq - 1, status);
                        newBlock->nextSend = block->nextSend;
                    }

                    if(seq + 1 <= end) {
                        newBlock = _scoreboard_addBlock(scoreboard, seq + 1, end, status);
                        newBlock->nextSend = block->nextSend;
                    }
                }
            }
        }

        /* go through and merge blocks that overlap with same status */
        blockIter = g_list_first(scoreboard->blocks);
        while(g_list_next(blockIter)) {
        	GList* next = g_list_next(blockIter);
            ScoreBoardBlock* block1 = (ScoreBoardBlock*)blockIter->data;
            ScoreBoardBlock* block2 = (ScoreBoardBlock*)next->data;

            if(block1->status == BLOCK_STATUS_SACKED && block2->status == BLOCK_STATUS_SACKED) {
                block1->end = block2->end;
                scoreboard->blocks = g_list_delete_link(scoreboard->blocks, g_list_next(blockIter));
            } else {
                blockIter = g_list_next(blockIter);
            }
        }
    }

    /* go through all the blocks and check if any of the INFLIGHT ones need to be retransmitted */
    for(blockIter = scoreboard->blocks; blockIter; blockIter = g_list_next(blockIter)) {
        ScoreBoardBlock* block = (ScoreBoardBlock*)blockIter->data;

        switch(block->status) {
            case BLOCK_STATUS_INFLIGHT:
                /* checks for 3 duplicate ACKs */
                if(block->start <= scoreboard->fack - 4) {
                    block->status = BLOCK_STATUS_LOST;
                    if(!scoreboard->nextToRetransmit) {
                        scoreboard->nextToRetransmit = block;
                    }
                    scoreboard->fackOut += block->end - block->start + 1;
                    dataLoss = TRUE;
                }
                break;

            case BLOCK_STATUS_RETRANSMITTED:
                if(block->nextSend <= scoreboard->fack) {
                    block->status = BLOCK_STATUS_LOST;
                    if(!scoreboard->nextToRetransmit) {
                        scoreboard->nextToRetransmit = block;
                    }
                    scoreboard->fackOut += 1;
                    dataLoss = TRUE;
                }
                break;

            case BLOCK_STATUS_LOST:
                if(!scoreboard->nextToRetransmit) {
                    scoreboard->nextToRetransmit = block;
                }
                break;

            case BLOCK_STATUS_SACKED:
                break;
        }

    }

    return dataLoss;
}

void scoreboard_clear(ScoreBoard* scoreboard) {
    MAGIC_ASSERT(scoreboard);

    scoreboard->fack = 0;
    scoreboard->fackOut = 0;
    scoreboard->sackOut = 0;
    scoreboard->nextToRetransmit = NULL;
    g_list_free_full(scoreboard->blocks, g_free);
    scoreboard->blocks = NULL;
}

gint scoreboard_getNextRetransmit(ScoreBoard* scoreboard) {
    MAGIC_ASSERT(scoreboard);
    
    gint retransmitSequence = -1;

    GList* iter = g_list_find(scoreboard->blocks, scoreboard->nextToRetransmit);
    while(iter) {
        ScoreBoardBlock* block = (ScoreBoardBlock*)iter->data;
        if(block->status == BLOCK_STATUS_LOST) {
            retransmitSequence = block->start;
            break;
        }
        iter = g_list_next(iter);
    }

    if(!iter) {
        scoreboard->nextToRetransmit = NULL;
    } else {
        scoreboard->nextToRetransmit = (ScoreBoardBlock*)iter->data;
    }

    return retransmitSequence;
}

void scoreboard_markRetransmitted(ScoreBoard* scoreboard, gint sequence, gint nextSend) {
    MAGIC_ASSERT(scoreboard);

    ScoreBoardBlock* block = scoreboard->nextToRetransmit;
    if(sequence < block->start || sequence > block->end) {
        error("Trying to mark retransmission for %d from block [%d,%d]", sequence,
                block->start, block->end);
        return;
    }

    scoreboard->fackOut--;
    if(scoreboard->fackOut < 0) {
        error("fack out is negative at %d with sequence %d and next send %d", scoreboard->fackOut, sequence, nextSend);
        return;
    }

    /* check if we need to split the block */
    if(block->end > block->start) {
        _scoreboard_splitBlock(scoreboard, block, block->start + 1);
    }

    block->status = BLOCK_STATUS_RETRANSMITTED;
    block->retransmitId = scoreboard->retransmitId;
    block->nextSend = nextSend;
    scoreboard->retransmitId++;
    scoreboard->lastRetransmitSeq = sequence;
}

void scoreboard_packetDropped(ScoreBoard* scoreboard, gint sequence) {
    MAGIC_ASSERT(scoreboard);

    GList* iter = scoreboard->blocks;
    while(iter) {
        ScoreBoardBlock* block = (ScoreBoardBlock*)iter->data;
        if(block->start <= sequence && sequence <= block->end) {
            break;
        }
        iter = g_list_next(iter);
    }

    ScoreBoardBlock* block = NULL;
    if(iter) {
        block = (ScoreBoardBlock*)iter->data;
    } else {
        block = _scoreboard_addBlock(scoreboard, sequence, sequence, BLOCK_STATUS_INFLIGHT);
    }

    if(block->status != BLOCK_STATUS_INFLIGHT) {
        return;
    }

    gint start = block->start;
    gint end = block->start;
    BlockStatus status = block->status;

    block->start = sequence;
    block->end = sequence;
    block->status = BLOCK_STATUS_LOST;

    if(start <= sequence - 1) {
        _scoreboard_addBlock(scoreboard, start, sequence - 1, status);
    }

    if(end >= sequence + 1) {
        _scoreboard_addBlock(scoreboard, sequence + 1, end, status);
    }

    scoreboard->fackOut++;

    iter = scoreboard->blocks;
    while(iter && !scoreboard->nextToRetransmit) {
        ScoreBoardBlock* block = (ScoreBoardBlock*)iter->data;
        if(block->status == BLOCK_STATUS_LOST) {
            scoreboard->nextToRetransmit = block;
        }
        iter = g_list_next(iter);
    }
}

void scoreboard_markLoss(ScoreBoard* scoreboard, gint unacked, gint nextSend) {
    MAGIC_ASSERT(scoreboard);

    scoreboard->nextToRetransmit = NULL;

    ScoreBoardBlock* lastLostBlock = NULL;
    GList *iter = scoreboard->blocks;
    while(iter) {
        GList* next = g_list_next(iter);

        ScoreBoardBlock* block = (ScoreBoardBlock*)iter->data;
        if(block->status != BLOCK_STATUS_SACKED) {
            if(block->status != BLOCK_STATUS_LOST) {
                scoreboard->fackOut += block->end - block->start + 1;
            }
            block->status = BLOCK_STATUS_LOST;

            if(lastLostBlock) {
                _scoreboard_mergeBlock(scoreboard, lastLostBlock);
                block = lastLostBlock;
            } else if(!scoreboard->nextToRetransmit) {
                scoreboard->nextToRetransmit = block;
            }
            lastLostBlock = (ScoreBoardBlock*)iter->data;
        } else {
            lastLostBlock = NULL;
        }

        if(!next && block->end + 1 < nextSend) {
            _scoreboard_addBlock(scoreboard, block->end + 1, nextSend - 1, BLOCK_STATUS_INFLIGHT);
        }

        iter = next;
    }

    if(!scoreboard->blocks || g_list_length(scoreboard->blocks) == 0) {
        _scoreboard_addBlock(scoreboard, unacked, nextSend - 1, BLOCK_STATUS_LOST);
        scoreboard->fackOut += (nextSend - unacked);
    }

    scoreboard->retransmitId = 0;
    scoreboard->ackedRetransmitId = -1;
}

gboolean scoreboard_isEmpty(ScoreBoard* scoreboard) {
    MAGIC_ASSERT(scoreboard);
    return (!scoreboard->blocks || g_list_length(scoreboard->blocks) == 0);
}
