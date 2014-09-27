/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "shd-tgen.h"

struct _TGenIO {
    gint epollD;

    GHashTable* children;

    gint refcount;
    guint magic;
};

typedef struct _TGenIOChild {
    gint descriptor;
    TGenIO_onEventFunc notify;
    gpointer notifyData;
} TGenIOChild;

TGenIO* tgenio_new() {
    /* create an epoll descriptor so we can manage events */
    gint epollD = epoll_create(1);
    if (epollD < 0) {
        tgen_critical("epoll_create(): returned %i error %i: %s", epollD, errno, g_strerror(errno));
        return NULL;
    }

    /* allocate the new server object and return it */
    TGenIO* io = g_new0(TGenIO, 1);
    io->magic = TGEN_MAGIC;
    io->refcount = 1;

    io->children = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, g_free);

    io->epollD = epollD;

    return io;
}

static void _tgenio_free(TGenIO* io) {
    TGEN_ASSERT(io);
    g_assert(io->refcount == 0);

    if(io->children) {
        g_hash_table_destroy(io->children);
    }

    io->magic = 0;
    g_free(io);
}

void tgenio_ref(TGenIO* io) {
    TGEN_ASSERT(io);
    io->refcount++;
}

void tgenio_unref(TGenIO* io) {
    TGEN_ASSERT(io);
    if(--(io->refcount) <= 0) {
        _tgenio_free(io);
    }
}

static void _tgenio_deregister(TGenIO* io, gint descriptor) {
    TGEN_ASSERT(io);

    gint result = epoll_ctl(io->epollD, EPOLL_CTL_DEL, descriptor, NULL);
    if(result != 0) {
        tgen_warning("epoll_ctl(): epoll %i descriptor %i returned %i error %i: %s",
                io->epollD, descriptor, result, errno, g_strerror(errno));
    }

    g_hash_table_remove(io->children, &descriptor);
}

gboolean tgenio_register(TGenIO* io, gint descriptor, TGenIO_onEventFunc notify, gpointer notifyData) {
    TGEN_ASSERT(io);

    if(g_hash_table_lookup(io->children, &descriptor)) {
        _tgenio_deregister(io, descriptor);
    }

    /* start watching */
    struct epoll_event ee;
    memset(&ee, 0, sizeof(struct epoll_event));
    ee.events = EPOLLIN|EPOLLOUT;
    ee.data.fd = descriptor;

    gint result = epoll_ctl(io->epollD, EPOLL_CTL_ADD, descriptor, &ee);

    if (result != 0) {
        tgen_critical("epoll_ctl(): epoll %i socket %i returned %i error %i: %s",
                io->epollD, descriptor, result, errno, g_strerror(errno));
        return FALSE;
    }

    TGenIOChild* child = g_new0(TGenIOChild, 1);
    child->descriptor = descriptor;
    child->notify = notify;
    child->notifyData = notifyData;
    g_hash_table_replace(io->children, &child->descriptor, child);

    return TRUE;
}

static void _tgenio_helper(TGenIO* io, TGenIOChild* child, gboolean in, gboolean out) {
    TGEN_ASSERT(io);
    g_assert(child);

    TGenEvent inEvents = TGEN_EVENT_NONE;

    /* check if we need read flag */
    if(in) {
        tgen_debug("descriptor %i is readable", child->descriptor);
        inEvents |= TGEN_EVENT_READ;
    }

    /* check if we need write flag */
    if(out) {
        tgen_debug("descriptor %i is writable", child->descriptor);
        inEvents |= TGEN_EVENT_WRITE;
    }

    /* activate the transfer */
    TGenEvent outEvents = child->notify(child->notifyData, child->descriptor, inEvents);

    /* now check if we should update our epoll events */
    if(outEvents & TGEN_EVENT_DONE) {
        _tgenio_deregister(io, child->descriptor);
    } else if(inEvents != outEvents) {
        guint32 newEvents = 0;
        if(outEvents & TGEN_EVENT_READ) {
            newEvents |= EPOLLIN;
        }
        if(outEvents & TGEN_EVENT_WRITE) {
            newEvents |= EPOLLOUT;
        }

        struct epoll_event ee;
        memset(&ee, 0, sizeof(struct epoll_event));
        ee.events = newEvents;
        ee.data.fd = child->descriptor;

        gint result = epoll_ctl(io->epollD, EPOLL_CTL_MOD, child->descriptor, &ee);
        if(result != 0) {
            tgen_warning("epoll_ctl(): epoll %i descriptor %i returned %i error %i: %s",
                    io->epollD, child->descriptor, result, errno, g_strerror(errno));
        }
    }
}

void tgenio_loopOnce(TGenIO* io) {
    TGEN_ASSERT(io);

    /* storage for collecting events from our epoll descriptor */
    struct epoll_event epevs[100];
    memset(epevs, 0, 100*sizeof(struct epoll_event));

    /* collect all events that are ready */
    gint nfds = epoll_wait(io->epollD, epevs, 100, 0);

    if(nfds == -1) {
        tgen_critical("epoll_wait(): epoll %i returned %i error %i: %s",
                io->epollD, nfds, errno, g_strerror(errno));
    }

    if(nfds <= 0) {
        return;
    }

    /* activate correct component for every descriptor that's ready. */
    for (gint i = 0; i < nfds; i++) {
        gboolean in = (epevs[i].events & EPOLLIN) ? TRUE : FALSE;
        gboolean out = (epevs[i].events & EPOLLOUT) ? TRUE : FALSE;
        TGenIOChild* child = g_hash_table_lookup(io->children, &epevs[i].data.fd);
        _tgenio_helper(io, child, in, out);
    }
}

gint tgenio_getEpollDescriptor(TGenIO* io) {
    TGEN_ASSERT(io);
    return io->epollD;
}

guint tgenio_getSize(TGenIO* io) {
    return g_hash_table_size(io->children);
}
