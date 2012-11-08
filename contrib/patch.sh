#!/bin/sh

## This file is used to patch various pieces of Tor that we need to run it in Shadow.
## Mainly, its used to remove 'static' from certain functions to make sure we
## can intercept them, and from variables that we need access to in the plugin
## wrapper code.

#echo Patching configure
#sed -i 's/-O2/-O0/g' configure
#sed -i 's/-fPIE/-fPIC/g' configure

# extention for backup files
bext=.bak

echo "Patching common/compat.c"
# static variable
sed -i${bext} 's/static int n_sockets_open =/int n_sockets_open =/g' src/common/compat.c
sed -i ':a;N;$!ba;s/static INLINE void\nsocket_accounting_lock/void\nsocket_accounting_lock/g' src/common/compat.c
sed -i ':a;N;$!ba;s/static INLINE void\nsocket_accounting_unlock/void\nsocket_accounting_unlock/g' src/common/compat.c

echo "Patching common/log.c"
# static function
sed -i${bext} '/static void/ {N;/\nlogv/ {s/static void\nlogv/void logv/}}' src/common/log.c
sed -i 's/static void logv(int severity, log_domain_mask_t domain, const char/void logv(int severity, log_domain_mask_t domain, const char/g' src/common/log.c

echo "Patching common/tortls.c"
sed -i${bext} '/char \*fake_hostname/{N;/SSL_set_tlsext/{N;/tor_free/ i if(!isServer) {
;a }
}}' src/common/tortls.c

echo "Patching or/circuitbuild.c"
# divide by zero error
sed -i${bext} '/ret \/= bin_counts/i if(bin_counts == 0) bin_counts = 1;' src/or/circuitbuild.c

# add SOCK_NONBLOCK
echo "Patching or/cpuworker.c"
sed -i${bext} 's/tor_socketpair(AF_UNIX, SOCK_STREAM,/tor_socketpair(AF_UNIX, SOCK_STREAM|SOCK_NONBLOCK,/' src/or/cpuworker.c
sed -i '/#ifndef TOR_IS_MULTITHREADED/ { N;N;N;/\n.*#endif/ {s/#ifndef.*#endif//}}' src/or/cpuworker.c
sed -i 's/set_socket_nonblocking(fd);//' src/or/cpuworker.c

echo "Patching or/main.c"
# multi-line static function definition
sed -i${bext} ':a;N;$!ba;s/static void\nsecond_elapsed_callback/void\nsecond_elapsed_callback/' src/or/main.c
# single line static function declaration
sed -i 's/static void second_elapsed_callback/void second_elapsed_callback/' src/or/main.c
# multi-line static function definition
sed -i ':a;N;$!ba;s/static void\nrefill_callback/void\nrefill_callback/g' src/or/main.c
# single line static function declaration
sed -i 's/static void refill_callback/void refill_callback/g' src/or/main.c
# static variable
sed -i 's/static int stats_prev_global_read_bucket/int stats_prev_global_read_bucket/g' src/or/main.c
# static variable
sed -i 's/static int stats_prev_global_write_bucket/int stats_prev_global_write_bucket/g' src/or/main.c
# static variable
sed -i 's/static periodic_timer_t \*second_timer/periodic_timer_t \*second_timer/g' src/or/main.c
# static variable
sed -i 's/static periodic_timer_t \*refill_timer/periodic_timer_t \*refill_timer/g' src/or/main.c
# static variable
sed -i 's/static int called_loop_once/int called_loop_once/g' src/or/main.c
# static variable
sed -i 's/static smartlist_t \*active_linked_connection_lst/smartlist_t \*active_linked_connection_lst/g' src/or/main.c
# needed for most versions before 0.2.3.20-rc
#echo "Patching infinite loop bugs in main.c"
# bugs causing infinite loops in shadow (multi-line)
#sed ':a;N;$!ba;s/conn->timestamp_lastwritten = now; \/\* reset so we can flush more \*\/\n      }/conn->timestamp_lastwritten = now; \/\* reset so we can flush more \*\/\n      } else if(sz == 0) { \/\* retval is 0 \*\/\n        \/\* wants to flush, but is rate limited \*\/\n        conn->write_blocked_on_bw = 1;\n        if (connection_is_reading(conn))\n        	connection_stop_reading(conn);\n        if (connection_is_writing(conn))\n        	connection_stop_writing(conn);\n 	  }/g' src/or/main.c > src/or/main.c.patch
#mv src/or/main.c.patch src/or/main.c

echo "Patching or/router.c"
# static variable
sed -i${bext} 's/static crypto_pk_t \*client_identitykey/crypto_pk_t \*client_identitykey/g' src/or/router.c

