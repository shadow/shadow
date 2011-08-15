#!/bin/sh 

echo Patching common/log.c
sed '/static void/ {N;/\nlogv/ {s/static void\nlogv/void logv/}}' src/common/log.c > src/common/log.c.patch
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
