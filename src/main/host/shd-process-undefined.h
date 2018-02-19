/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_tryjoin_np);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_timedjoin_np);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_attr_getstack);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_attr_setstack);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_attr_setaffinity_np);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_attr_getaffinity_np);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_getattr_default_np);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_setattr_default_np);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_setschedprio);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_getname_np);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_setname_np);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_setaffinity_np);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_getaffinity_np);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_mutex_timedlock);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_mutex_consistent);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_mutex_consistent_np);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_mutexattr_getrobust);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_mutexattr_getrobust_np);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_mutexattr_setrobust);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_mutexattr_setrobust_np);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_rwlock_timedrdlock);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_rwlock_timedwrlock);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_rwlockattr_getkind_np);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_rwlockattr_setkind_np);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_spin_init);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_spin_destroy);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_spin_lock);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_spin_trylock);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_spin_unlock);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_barrier_init);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_barrier_destroy);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_barrier_wait);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_barrierattr_init);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_barrierattr_destroy);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_barrierattr_getpshared);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_barrierattr_setpshared);
PROCESS_EMU_UNSUPPORTED(int, ENOSYS, pthread_getcpuclockid);
PROCESS_EMU_UNSUPPORTED(void,      , __pthread_register_cancel);
PROCESS_EMU_UNSUPPORTED(void,      , __pthread_unregister_cancel);
PROCESS_EMU_UNSUPPORTED(void,      , __pthread_register_cancel_defer);
PROCESS_EMU_UNSUPPORTED(void,      , __pthread_unregister_cancel_restore);
PROCESS_EMU_UNSUPPORTED(void,      , __pthread_unwind_next);
