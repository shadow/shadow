/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "main/core/main.h"

#include <sched.h>

int main (int argc, char* argv[]) {

    struct sched_param sparam = {0};
    sparam.sched_priority = 1;
    sched_setscheduler(0, SCHED_FIFO, &sparam);

    return main_runShadow(argc, argv);
}
