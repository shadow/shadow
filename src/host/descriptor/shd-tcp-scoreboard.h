/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_TCP_SCOREBOARD_H_
#define SHD_TCP_SCOREBOARD_H_

#include "shadow.h"

typedef struct _ScoreBoard ScoreBoard;

ScoreBoard* scoreboard_new();
gboolean scoreboard_update(ScoreBoard* scoreboard, GList* selectiveACKs, gint unacked);
void scoreboard_clear(ScoreBoard* scoreboard);
gint scoreboard_getNextRetransmit(ScoreBoard* scoreboard);
void scoreboard_markRetransmitted(ScoreBoard* scoreboard, gint sequence, gint sendNext);
void scoreboard_markLoss(ScoreBoard* scoreboard, gint unacked, gint sendNext);
gboolean scoreboard_isEmpty(ScoreBoard* scoreboard);

void scoreboard_packetDropped(ScoreBoard* scoreboard, gint sequence);

#endif /* SHD_TCP_SCOREBOARD_H_ */
