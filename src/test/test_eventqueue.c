#include <glib.h>
#include <glib/gprintf.h>
#include <string.h>
#include "shadow.h"

//gint shadowevent_compare(const Event* a, const Event* b, gpointer user_data) {
//  return a->time > b->time ? +1 : a->time == b->time ? 0 : -1;
//}
gint shadowevent_compare(const Event* a, const Event* b, gpointer user_data) {
    /* events already scheduled get priority over new events */
    return (a->time > b->time) ? +1 : (a->time < b->time) ? -1 :
            (a->sequence > b->sequence) ? +1 : (a->sequence < b->sequence) ? -1 : 0;
}

void shadowevent_free(Event* event) {}

gint main(gint argc, gchar * argv[]) {
    gint n = 10000; // total num of iterations to test
    gint m = 100; // num events with same time key
    gint N = n*m;

    g_printf("starting...\n");

    Event* events[N];
    for(gint k = 0; k < N; k++) {
        events[k] = g_new0(Event, 1);
    }

    g_printf("setting keys...\n");

    for(gint i = 0; i < n; i++) {
        for(gint j = 0; j < m; j++) {
            gint slot = (i * m) + j;
            Event* e = events[slot];
            e->magic = slot; // real order
            e->time = i; // key order
        }
    }

    g_printf("pushing...\n");

    EventQueue* eq = eventqueue_new();

    for(gint k = 0; k < N; k++) {
        eventqueue_push(eq, events[k]);
    }

    g_printf("popping...\n");

    gint lastKey = 0;
    gint lastMagic = 0;
    gboolean isSet = FALSE;
    for(gint k = 0; k < N; k++) {
        Event* e = eventqueue_pop(eq);
        g_printf("%u,%lu,%lu\n", e->magic, e->time,e->sequence);

        if(isSet) {
            g_assert(e->magic > lastMagic);
            g_assert(e->time >= lastKey);
        } else {
            isSet = TRUE;
        }

        lastKey = e->time;
        lastMagic = e->magic;
    }

    g_printf("cleaning\n");

    eventqueue_free(eq);
    for(gint k = 0; k < N; k++) {
        g_free(events[k]);
    }

    g_printf("test successful!\n");
}
