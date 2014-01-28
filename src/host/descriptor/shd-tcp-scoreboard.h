/*
 * The Shadow Simulator
 * Copyright (c) 2013-2014, John Geddes
 * See LICENSE for licensing information
 */

#ifndef SHD_TCP_SCOREBOARD_H_
#define SHD_TCP_SCOREBOARD_H_

#include "shadow.h"

typedef struct _ScoreBoard ScoreBoard;

ScoreBoard* scoreboard_new();
void scoreboard_free(ScoreBoard* scoreboard);
void scoreboard_clear(ScoreBoard* scoreboard);

gboolean scoreboard_update(ScoreBoard* scoreboard, GList* selectiveACKs, gint unacked);
gint scoreboard_getNextRetransmit(ScoreBoard* scoreboard, gint nextSend);
void scoreboard_markLoss(ScoreBoard* scoreboard, gint unacked, gint sendNext);
gboolean scoreboard_isEmpty(ScoreBoard* scoreboard);

void scoreboard_packetDropped(ScoreBoard* scoreboard, gint sequence);
void scoreboard_removeAckedBlocks(ScoreBoard* scoreboard, gint lowestUnackedPacket);

#endif /* SHD_TCP_SCOREBOARD_H_ */
