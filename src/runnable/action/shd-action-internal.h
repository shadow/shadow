/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_ACTION_INTERNAL_H_
#define SHD_ACTION_INTERNAL_H_

#include "shadow.h"

struct _Action {
    Runnable super;
    gint priority;
    MAGIC_DECLARE;
};

#endif /* SHD_ACTION_INTERNAL_H_ */
