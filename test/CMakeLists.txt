## this project defines tests for DVN
ENABLE_TESTING()

include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/src/utility)

add_definitions(-fPIC -g3 -DDEBUG -D_GNU_SOURCE)

set(UTIL_DIR ${CMAKE_SOURCE_DIR}/src/utility)

add_executable(test_linkedbuffer test_linkedbuffer.c ${UTIL_DIR}/linkedbuffer.c)
target_link_libraries(test_linkedbuffer ${GLIB_LIBRARIES})
ADD_TEST(shadowtest test_linkedbuffer)

add_executable(test_orderedlist test_orderedlist.c ${UTIL_DIR}/orderedlist.c)
target_link_libraries(test_orderedlist ${GLIB_LIBRARIES})
ADD_TEST(shadowtest test_orderedlist)
