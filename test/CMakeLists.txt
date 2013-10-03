## this project defines tests
ENABLE_TESTING()

find_package(RT REQUIRED)
find_package(DL REQUIRED)
find_package(M REQUIRED)
find_package(GLIB REQUIRED)

include_directories(${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES} ${GLIB_INCLUDES})

include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/src/utility)

add_definitions(-fPIC -g3 -DDEBUG -D_GNU_SOURCE)

add_executable(test_eventqueue test_eventqueue.c 
    ${CMAKE_SOURCE_DIR}/src/utility/shd-async-priority-queue.c
    ${CMAKE_SOURCE_DIR}/src/utility/shd-priority-queue.c
    ${CMAKE_SOURCE_DIR}/src/engine/shd-event-queue.c)
target_link_libraries(test_eventqueue ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})
ADD_TEST(shadowtest test_eventqueue)
