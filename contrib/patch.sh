#!/bin/sh

## This file is used to patch various pieces of Tor that we need to run it in Shadow.
## Mainly, its used to remove 'static' from certain functions to make sure we
## can intercept them.

echo Patching configure
sed -i 's/-O2/-O0/g' configure
sed -i 's/-fPIE/-fPIC/g' configure

echo Patching common/log.c
sed '/static void/ {N;/\nlogv/ {s/static void\nlogv/void logv/}}' src/common/log.c > src/common/log.c.patch
mv src/common/log.c.patch src/common/log.c
sed 's/static void logv(int severity, log_domain_mask_t domain, const char/void logv(int severity, log_domain_mask_t domain, const char/g' src/common/log.c > src/common/log.c.patch
mv src/common/log.c.patch src/common/log.c

echo Patching common/tortls.c
sed '/char \*fake_hostname/{N;/SSL_set_tlsext/{N;/tor_free/ i if(!isServer) {
;a }
}}' src/common/tortls.c > src/common/tortls.c.patch
mv src/common/tortls.c.patch src/common/tortls.c

echo Patching or/circuitbuild.c
sed '/ret \/= bin_counts/i if(bin_counts == 0) bin_counts = 1;' src/or/circuitbuild.c > src/or/circuitbuild.c.patch
mv src/or/circuitbuild.c.patch src/or/circuitbuild.c

echo Patching or/cpuworker.c
sed 's/tor_socketpair(AF_UNIX, SOCK_STREAM,/tor_socketpair(AF_UNIX, SOCK_STREAM|SOCK_NONBLOCK,/' src/or/cpuworker.c > src/or/cpuworker.c.patch
mv src/or/cpuworker.c.patch src/or/cpuworker.c

sed '/#ifndef TOR_IS_MULTITHREADED/ { N;N;N;/\n.*#endif/ {s/#ifndef.*#endif//}}' src/or/cpuworker.c > src/or/cpuworker.c.patch
mv src/or/cpuworker.c.patch src/or/cpuworker.c

sed 's/set_socket_nonblocking(fd);//' src/or/cpuworker.c > src/or/cpuworker.c.patch
mv src/or/cpuworker.c.patch src/or/cpuworker.c

echo Patching or/main.c
# multi-line static function definition
sed ':a;N;$!ba;s/static void\nsecond_elapsed_callback/void\nsecond_elapsed_callback/' src/or/main.c > src/or/main.c.patch
mv src/or/main.c.patch src/or/main.c

# single line static function declaration
sed 's/static void second_elapsed_callback/void second_elapsed_callback/' src/or/main.c > src/or/main.c.patch
mv src/or/main.c.patch src/or/main.c

# multi-line static function definition
sed ':a;N;$!ba;s/static void\nrefill_callback/void\nrefill_callback/g' src/or/main.c > src/or/main.c.patch
mv src/or/main.c.patch src/or/main.c

# needed for most versions before 0.2.3.20-rc
#echo "Patching infinite loop bugs in main.c"
# bugs causing infinite loops in shadow (multi-line)
#sed ':a;N;$!ba;s/conn->timestamp_lastwritten = now; \/\* reset so we can flush more \*\/\n      }/conn->timestamp_lastwritten = now; \/\* reset so we can flush more \*\/\n      } else if(sz == 0) { \/\* retval is 0 \*\/\n        \/\* wants to flush, but is rate limited \*\/\n        conn->write_blocked_on_bw = 1;\n        if (connection_is_reading(conn))\n        	connection_stop_reading(conn);\n        if (connection_is_writing(conn))\n        	connection_stop_writing(conn);\n 	  }/g' src/or/main.c > src/or/main.c.patch
#mv src/or/main.c.patch src/or/main.c

# single line static function declaration
sed 's/static void refill_callback/void refill_callback/g' src/or/main.c > src/or/main.c.patch
mv src/or/main.c.patch src/or/main.c

## no longer needed, we intercept the function that deals with this
#echo "Patching or/routerlist.c to make sure the consensus contains correct bandwidth for our nodes"
#sed 's/#define DEFAULT_MAX_BELIEVABLE_BANDWIDTH 10000000.*$/#define DEFAULT_MAX_BELIEVABLE_BANDWIDTH 1000000000 \/\* 1 GB\/sec \*\//g' src/or/routerlist.c > src/or/routerlist.c.patch
#mv src/or/routerlist.c.patch src/or/routerlist.c

echo "Patching or/or.h to write cell stats more often (every 15 minutes instead of every 24 hours)"
sed 's/#define WRITE_STATS_INTERVAL (24\*60\*60)/#define WRITE_STATS_INTERVAL (900)/g' src/or/or.h > src/or/or.h.patch
mv src/or/or.h.patch src/or/or.h

