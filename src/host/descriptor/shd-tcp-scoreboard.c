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
    MAGIC_DECLARE;
};

struct _ScoreBoard {
    /* list of blocks in the scoreboard */
    GList* blocks;

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

static ScoreBoardBlock* _scoreboardblock_new(gint start, gint end, BlockStatus status) {
    utility_assert(start <= end);
	ScoreBoardBlock* block = g_new0(ScoreBoardBlock, 1);
	MAGIC_INIT(block);
    block->start = start;
    block->end = end;
    block->status = status;
    return block;
}

static void _scoreboardblock_free(ScoreBoardBlock* block) {
	MAGIC_ASSERT(block);
	MAGIC_CLEAR(block);
	g_free(block);
}

static gint _scoreboardblock_compare(ScoreBoardBlock* b1, ScoreBoardBlock* b2) {
	MAGIC_ASSERT(b1);
	MAGIC_ASSERT(b2);
    return (b1->start < b2->start ? -1 : b1->start > b2->start ? 1 : 0);
}

ScoreBoard* scoreboard_new() {
	ScoreBoard* scoreboard = g_new0(ScoreBoard, 1);
	MAGIC_INIT(scoreboard);

    return scoreboard;
}

static ScoreBoardBlock* _scoreboard_addBlock(ScoreBoard* scoreboard, gint start, gint end, BlockStatus status) {
    MAGIC_ASSERT(scoreboard);

    ScoreBoardBlock* block = _scoreboardblock_new(start, end, status);
    scoreboard->blocks = g_list_insert_sorted(scoreboard->blocks, block, (GCompareFunc)_scoreboardblock_compare);

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

static void _scoreboard_mergeBlockWithNext(ScoreBoard* scoreboard, ScoreBoardBlock* block) {
    MAGIC_ASSERT(scoreboard);

    GList* link = g_list_find(scoreboard->blocks, block);
    GList* nextLink = g_list_next(link);
    if(nextLink) {
		ScoreBoardBlock* nextBlock = (ScoreBoardBlock*)nextLink->data;
		utility_assert(nextBlock);

		/* make sure there are no holes */
		utility_assert((((block->end - block->start) + 1) + ((nextBlock->end - nextBlock->start) + 1)) ==
				((nextBlock->end - block->start) + 1));

		block->end = nextBlock->end;
		scoreboard->blocks = g_list_delete_link(scoreboard->blocks, nextLink);
		_scoreboardblock_free(nextBlock);
    }
}

static void _scoreboard_splitBlock(ScoreBoard* scoreboard, ScoreBoardBlock* block,
		gint sequence, BlockStatus status) {
    MAGIC_ASSERT(scoreboard);

    gint oldStart = block->start;
    gint oldEnd = block->end;
    BlockStatus oldStatus = block->status;

    /* create single block */
    block->start = sequence;
    block->end = sequence;
    block->status = status;

    /* create new blocks */
    ScoreBoardBlock* newBlock;
    if(sequence - 1 >= oldStart) {
        newBlock = _scoreboard_addBlock(scoreboard, oldStart, sequence - 1, oldStatus);
        newBlock->nextSend = block->nextSend;
    }

    if(sequence + 1 <= oldEnd) {
        newBlock = _scoreboard_addBlock(scoreboard, sequence + 1, oldEnd, oldStatus);
        newBlock->nextSend = block->nextSend;
    }
}

gint _scoreboard_compareSack(gconstpointer s1, gconstpointer s2) {
    gint sack1 = GPOINTER_TO_INT(s1);
    gint sack2 = GPOINTER_TO_INT(s2);
    return sack1 < sack2 ? -1 : sack1 > sack2 ? 1 : 0;
}

static void _scoreboard_removeAckedBlocks(ScoreBoard* scoreboard, gint lowestUnackedPacket) {
    GList* link = g_list_first(scoreboard->blocks);
    while(link) {
        ScoreBoardBlock* block = (ScoreBoardBlock*)link->data;
        MAGIC_ASSERT(block);

        /* if block is unACKed, break out of loop */
        if(block->start >= lowestUnackedPacket) {
            return;
        } else if(block->end >= lowestUnackedPacket) {
        	block->start = lowestUnackedPacket;
        	return;
        }

        utility_assert(block->start < lowestUnackedPacket && block->end < lowestUnackedPacket);

        /* remove block and free it */
        GList* nextLink = g_list_next(link);
        scoreboard->blocks = g_list_delete_link(scoreboard->blocks, link);
        _scoreboardblock_free(block);
        link = nextLink;
    }
}

static ScoreBoardBlock* _scoreboard_getBlockFromSequence(ScoreBoard* scoreboard, gint sequence) {
	MAGIC_ASSERT(scoreboard);

    GList* link = g_list_first(scoreboard->blocks);
    while(link) {
    	ScoreBoardBlock* block = (ScoreBoardBlock*)link->data;
    	MAGIC_ASSERT(block);

    	if(block->start <= sequence && sequence <= block->end) {
    		return block;
    	}

    	link = g_list_next(link);
    }

    return NULL;
}

static void _scoreboard_setStatus(ScoreBoard* scoreboard, gint sequence, BlockStatus status) {
	MAGIC_ASSERT(scoreboard);

	/* first check if we have an existing block for this sequence */
    ScoreBoardBlock* block = _scoreboard_getBlockFromSequence(scoreboard, sequence);

    /* if block exists, correctly update status; otherwise create a new one.
     * don't worry about consecutive blocks with the same status, those will
     * be merged later */
    if(block) {
    	if(block->status != status) {
    		_scoreboard_splitBlock(scoreboard, block, sequence, status);
    	}
    } else {
    	/* we need a new block for this status */
    	_scoreboard_addBlock(scoreboard, sequence, sequence, status);
    }
}

static void _scoreboard_mergeAllDuplicateBlocks(ScoreBoard* scoreboard) {
	MAGIC_ASSERT(scoreboard);

	if(scoreboard->blocks) {
		GList* link =  g_list_first(scoreboard->blocks);
		if(link) {
			GList* nextLink = g_list_next(link);
			while(nextLink) {
				ScoreBoardBlock* block = (ScoreBoardBlock*)link->data;
				MAGIC_ASSERT(block);
				ScoreBoardBlock* nextBlock = (ScoreBoardBlock*)nextLink->data;
				MAGIC_ASSERT(nextBlock);

				if((block->end + 1) >= nextBlock->start && block->status == nextBlock->status) {
					utility_assert(block->end <= nextBlock->end);
					_scoreboard_mergeBlockWithNext(scoreboard, block);
					nextLink = link;
				}

				link = nextLink;
				nextLink = g_list_next(link);
			}
		}
	}
}

gboolean scoreboard_update(ScoreBoard* scoreboard, GList* selectiveACKs, gint unacked) {
    MAGIC_ASSERT(scoreboard);

    gboolean dataLoss = FALSE;

    /* remove any blocks that have been fully ACKed */
    _scoreboard_removeAckedBlocks(scoreboard, unacked);

    if(selectiveACKs) {
        selectiveACKs = g_list_sort(selectiveACKs, (GCompareFunc)_scoreboard_compareSack);
        scoreboard->fack = MAX(scoreboard->fack, GPOINTER_TO_INT(g_list_last(selectiveACKs)->data));

        gint firstSeq = unacked;
        gint lastSeq = GPOINTER_TO_INT(g_list_last(selectiveACKs)->data);

        /* go through all sequence that might be sacked and update scoreboard */
        for(gint seq = firstSeq; seq <= lastSeq; seq++) {
            gboolean sacked = (gboolean)g_list_find(selectiveACKs, GINT_TO_POINTER(seq));
			BlockStatus status = (sacked ? BLOCK_STATUS_SACKED : BLOCK_STATUS_INFLIGHT);

			/* make sure this sequence status is correct in the scoreboard */
			_scoreboard_setStatus(scoreboard, seq, status);
        }

        /* go through and merge blocks that overlap with same status */
        _scoreboard_mergeAllDuplicateBlocks(scoreboard);
    }

    /* go through all the blocks and check if any of the INFLIGHT ones need to be retransmitted */
    for(GList* link = scoreboard->blocks; link; link = g_list_next(link)) {
        ScoreBoardBlock* block = (ScoreBoardBlock*)link->data;
        MAGIC_ASSERT(block);

        switch(block->status) {
            case BLOCK_STATUS_INFLIGHT:
                /* checks for 3 duplicate ACKs */
                if(block->start <= scoreboard->fack - 4) {
                    block->status = BLOCK_STATUS_LOST;
                    scoreboard->fackOut += ((block->end - block->start) + 1);
                    dataLoss = TRUE;
                }
                break;

            case BLOCK_STATUS_RETRANSMITTED:
                if(block->nextSend <= scoreboard->fack) {
                    block->status = BLOCK_STATUS_LOST;
                    scoreboard->fackOut += ((block->end - block->start) + 1);
                    dataLoss = TRUE;
                }
                break;

            case BLOCK_STATUS_LOST:
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
    if(scoreboard->blocks) {
		g_list_free_full(scoreboard->blocks, (GDestroyNotify)_scoreboardblock_free);
		scoreboard->blocks = NULL;
    }
}

void scoreboard_free(ScoreBoard* scoreboard) {
	MAGIC_ASSERT(scoreboard);
	scoreboard_clear(scoreboard);
	MAGIC_CLEAR(scoreboard);
	g_free(scoreboard);
}

static ScoreBoardBlock* _scoreboard_getNextLostBlock(ScoreBoard* scoreboard) {
	MAGIC_ASSERT(scoreboard);

    GList* link = g_list_first(scoreboard->blocks);
    while(link) {
    	ScoreBoardBlock* block = (ScoreBoardBlock*)link->data;
		MAGIC_ASSERT(block);

		if(block->status == BLOCK_STATUS_LOST) {
			return block;
		}

		link = g_list_next(link);
    }

    return NULL;
}

gint scoreboard_getNextRetransmit(ScoreBoard* scoreboard, gint nextSend) {
    MAGIC_ASSERT(scoreboard);

    /* look for the first lost sequence number so we can retransmit it */
    ScoreBoardBlock* block = _scoreboard_getNextLostBlock(scoreboard);

    if(block) {
    	/* separate the first sequence from the rest of the block and set it to RETRANSMITTED */
        _scoreboard_splitBlock(scoreboard, block, block->start, BLOCK_STATUS_RETRANSMITTED);

        block->retransmitId = scoreboard->retransmitId;
        block->nextSend = nextSend;
		scoreboard->retransmitId++;
		scoreboard->lastRetransmitSeq = block->start;

		/* status changed from LOST to RETRANSMITTED */
		scoreboard->fackOut--;
		if(scoreboard->fackOut < 0) {
			error("fack out is negative at %d with sequence %d and next send %d",
					scoreboard->fackOut, block->start, nextSend);
		}

    	return block->start;
    } else {
    	return -1;
    }
}

void scoreboard_packetDropped(ScoreBoard* scoreboard, gint sequence) {
    MAGIC_ASSERT(scoreboard);

    ScoreBoardBlock* block = _scoreboard_getBlockFromSequence(scoreboard, sequence);
    if(block) {
		if(block->status != BLOCK_STATUS_INFLIGHT) {
			return;
		}
		_scoreboard_splitBlock(scoreboard, block, sequence, BLOCK_STATUS_LOST);
    } else {
        block = _scoreboard_addBlock(scoreboard, sequence, sequence, BLOCK_STATUS_LOST);
    }

    scoreboard->fackOut++;
}

void scoreboard_markLoss(ScoreBoard* scoreboard, gint unacked, gint nextSend) {
    MAGIC_ASSERT(scoreboard);

    GList* link = g_list_first(scoreboard->blocks);
    while(link) {
        ScoreBoardBlock* block = (ScoreBoardBlock*)link->data;
        MAGIC_ASSERT(block);

        if(block->status != BLOCK_STATUS_SACKED) {
            if(block->status != BLOCK_STATUS_LOST) {
                scoreboard->fackOut += ((block->end - block->start) + 1);
            }
            block->status = BLOCK_STATUS_LOST;
        }

        link = g_list_next(link);

        if(!link && block->end + 1 < nextSend) {
            _scoreboard_addBlock(scoreboard, block->end + 1, nextSend - 1, BLOCK_STATUS_INFLIGHT);
        }
    }

    if(scoreboard_isEmpty(scoreboard) && (nextSend > unacked)) {
        _scoreboard_addBlock(scoreboard, unacked, nextSend - 1, BLOCK_STATUS_LOST);
        scoreboard->fackOut += (nextSend - unacked);
    }

    _scoreboard_mergeAllDuplicateBlocks(scoreboard);

    scoreboard->retransmitId = 0;
    scoreboard->ackedRetransmitId = -1;
}

gboolean scoreboard_isEmpty(ScoreBoard* scoreboard) {
    MAGIC_ASSERT(scoreboard);
    return (!scoreboard->blocks || g_list_length(scoreboard->blocks) == 0);
}
