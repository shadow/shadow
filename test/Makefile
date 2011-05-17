CFLAGS=-I../core -I../shared -pg -L../shared  --std=c99

all: nbdf_test dsim_tester test_btree test_evtracker test_preload test_simple_udp test_linkedbuffer

test_evtracker: test_evtracker.c ../dvn-core/evtracker.o 
	gcc $(CFLAGS) -o test_evtracker test_evtracker.c ../dvn-core/evtracker.o ../dvn-core/heap.o ../dvn-shared/log.o

nbdf_test: nbdf_test.o ../dvn-shared/libdvn-shared.la
	libtool gcc $(CFLAGS) nbdf_test.o ../dvn-shared/libdvn-shared.la -o nbdf_test

nbdf_test.o: nbdf_test.c 
	gcc $(CFLAGS) -c nbdf_test.c

#test_pipecloud: test_pipecloud.o pipecloud.o
#	libtool gcc $(CFLAGS) ../dvn-shared/libdvn-shared.la test_pipecloud.o -o test_pipecloud
test_pipecloud: test_pipecloud.c pipecloud.c list.c ../shared/pipecloud.h ../shared/list.h utility.c ../shared/utility.h
	gcc $(CFLAGS) -o test_pipecloud test_pipecloud.c pipecloud.c list.c utility.c
	
test_linkedbuffer: test_linkedbuffer.c ../shared/linkedbuffer.c ../shared/bufferlink.c
	gcc $(CFLAGS) -o test_linkedbuffer test_linkedbuffer.c ../shared/linkedbuffer.c ../shared/bufferlink.c

test_multiso: test_multiso_lib.so test_multiso.o
	gcc -o test_multiso test_multiso.o -ldl

test_multiso.o: test_multiso.c
	gcc -c test_multiso.c

test_multiso_lib.so: test_multiso_lib.o
	gcc -shared -Wl,--export-dynamic -Wl,-soname,test_multiso_lib.so -o test_multiso_lib.so test_multiso_lib.o

test_multiso_lib.o: test_multiso_lib.c
	gcc -c test_multiso_lib.c
	
test_preload: test_preload_lib.so test_preload.o
	gcc -o test_preload test_preload.o
	
test_preload_lib.so: test_preload_lib.c
	gcc -Wall -D _GNU_SOURCE -fPIC -shared -ldl -o test_preload_lib.so test_preload_lib.c

test_preload.o: test_preload.c
	gcc -c test_preload.c
	
test_simple_udp: test_simple_udp.c ../module/simple_transport/simple_transport_lib.c
	gcc -I../module/simple_transport -o test_simple_udp test_simple_udp.c ../module/simple_transport/simple_transport_lib.c

test_simple_tcp: test_simple_tcp.c ../module/simple_transport/simple_transport_lib.c
	gcc -I../module/simple_transport -o test_simple_tcp test_simple_tcp.c ../module/simple_transport/simple_transport_lib.c

clean:
	rm *.o *.so test_multiso test_preload test_simple_udp test_simple_tcp test_linkedbuffer
