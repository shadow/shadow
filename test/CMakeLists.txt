## this project defines tests for DVN
ENABLE_TESTING()

include_directories(${CMAKE_SOURCE_DIR}/src/module)
include_directories(${CMAKE_SOURCE_DIR}/src/util)
include_directories(${CMAKE_SOURCE_DIR}/src/core)

add_definitions(-fPIC -g3 -DDEBUG -D_GNU_SOURCE)

set(UTIL_DIR ${CMAKE_SOURCE_DIR}/src/util)

#add_executable(test_evtracker test_evtracker.c ../src/core/evtracker.c ../src/core/heap.c ../src/core/log.c)
#add_executable(test_pipecloud test_pipecloud.c ../src/util/pipecloud.c ../src/util/list.c ../src/util/utility.c)

#add_executable(nbdf_test nbdf_test.c)
#add_dependencies(nbdf_test util)
#target_link_libraries(nbdf_test util)

find_library(LIBDL NAMES dl)
if(NOT EXISTS ${LIBDL})
	message(FATAL_ERROR "Cannot find dl library, please install libdl!")
endif()

add_executable(test_multiso test_multiso.c)
target_link_libraries(test_multiso dl)

add_executable(test_linkedbuffer test_linkedbuffer.c ../src/util/linkedbuffer.c)
ADD_TEST(shadowtest test_rwlock_mgr)

add_executable(test_linkedbuffer_nocow test_linkedbuffer_nocow.c ../src/util/linkedbuffer_nocow.c)
ADD_TEST(shadowtest test_linkedbuffer_nocow)

add_executable(test_orderedlist test_orderedlist.c ../src/util/orderedlist.c)
ADD_TEST(shadowtest test_orderedlist)

add_executable(test_hashtable test_hashtable.c ../src/util/hashtable.c ../src/util/hash.c ../src/util/btree.c ../src/util/utility.c)
ADD_TEST(shadowtest test_hashtable)

add_executable(test_shmcabinet test_shmcabinet.c ../src/util/shmcabinet.c ../src/util/rwlock_mgr.c ../src/util/rwlock.c)
target_link_libraries(test_shmcabinet rt pthread)
ADD_TEST(shadowtest test_shmcabinet)

add_executable(test_rwlock test_rwlock.c ../src/util/rwlock.c)
target_link_libraries(test_rwlock pthread)
ADD_TEST(shadowtest test_rwlock)

add_executable(test_rwlock_mgr test_rwlock_mgr.c ../src/util/rwlock_mgr.c ../src/util/rwlock.c)
target_link_libraries(test_rwlock_mgr pthread)
ADD_TEST(shadowtest test_rwlock_mgr)

add_executable(test_shares test_shares.c)
target_link_libraries(test_shares rt)

include_directories(${CMAKE_SOURCE_DIR}/src/core/vevent ${CMAKE_SOURCE_DIR}/src/core/node)
add_library(vevent_test MODULE vevent_test.c ../src/core/vevent/vevent.c)
target_link_libraries(vevent_test snri)

#include_directories(${CMAKE_SOURCE_DIR}/src/module/simple_transport)
#add_executable(test_simple_udp test_simple_udp.c ../src/module/simple_transport/simple_transport_lib.c)
#add_executable(test_simple_tcp test_simple_tcp.c ../src/module/simple_transport/simple_transport_lib.c)

add_library(test_preload test_preload_lib.c)
target_link_libraries(test_preload util dl)
