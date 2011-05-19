set(util_srcs
	btree.c
	clo.c
	hash.c
	hashtable.c
	heap.c
	ipcsync.c
	orderedlist.c
	linkedbuffer.c
	list.c
	nbdf.c
	pipecloud.c
	rand.c
	reference_counter.c
	rwlock.c
	rwlock_mgr.c
	shd-cdf.c
	shmcabinet.c
	shmcabinet_mgr.c
	socket.c
	socketset.c
	utility.c
	vector.c
)

add_library(shadow-util STATIC ${util_srcs})
target_link_libraries(shadow-util rt pthread)
install(TARGETS shadow-util EXPORT shadow-externals DESTINATION lib)
install(FILES global.h DESTINATION include/shadow)
install(FILES hashtable.h btree.h orderedlist.h hash.h shd-cdf.h utility.h DESTINATION include/shadow)
