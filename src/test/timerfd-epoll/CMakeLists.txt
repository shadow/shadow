## if this test needs any libraries, find and include them here
find_package(RT REQUIRED)
find_package(DL REQUIRED)
find_package(M REQUIRED)
find_package(GLIB REQUIRED)
include_directories(${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES} ${GLIB_INCLUDES})

## build the test as a dynamic executable that plugs into shadow
add_shadow_plugin(shadow-plugin-test-timerfd-epoll shd-test-timerfd-epoll.c)

## if the test needs any libraries, link them here
target_link_libraries(shadow-plugin-test-timerfd-epoll ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES} ${GLIB_LIBRARIES})

## register the tests
add_test(NAME test-timerfd-epoll COMMAND shadow-plugin-test-timerfd-epoll)
add_test(NAME test-timerfd-epoll-shadow COMMAND ${CMAKE_BINARY_DIR}/src/shadow  ${CMAKE_CURRENT_SOURCE_DIR}/timerfd-epoll.test.shadow.config.xml)
