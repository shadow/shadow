/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_TGEN_TIMER_H_
#define SHD_TGEN_TIMER_H_

typedef struct _TGenTimer TGenTimer;

/* return TRUE to cancel the timer, FALSE to continue the timer as originally configured */
typedef gboolean (*TGenTimer_notifyExpiredFunc)(gpointer data1, gpointer data2);

TGenTimer* tgentimer_new(guint64 microseconds, gboolean isPersistent,
        TGenTimer_notifyExpiredFunc notify, gpointer data1, gpointer data2,
        GDestroyNotify destructData1, GDestroyNotify destructData2);
void tgentimer_ref(TGenTimer* timer);
void tgentimer_unref(TGenTimer* timer);

TGenEvent tgentimer_onEvent(TGenTimer* timer, gint descriptor, TGenEvent events);
gint tgentimer_getDescriptor(TGenTimer* timer);
void tgentimer_settime_micros(TGenTimer *timer, guint64 micros);

#endif /* SHD_TGEN_TIMER_H_ */
