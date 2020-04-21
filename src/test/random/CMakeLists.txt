## if this test needs any libraries, find and include them here
#find_package(RT REQUIRED)
#find_package(DL REQUIRED)
#find_package(M REQUIRED)
#include_directories(${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES})

## build the test as a dynamic executable that plugs into shadow
add_shadow_exe(shadow-plugin-test-random test_random.c)

## if the test needs any libraries, link them here
#target_link_libraries(shadow-plugin-test-random ${M_LIBRARIES} ${DL_LIBRARIES} ${RT_LIBRARIES})

## register the tests
add_test(NAME random COMMAND shadow-plugin-test-random)
add_test(NAME random-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d random.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/random.test.shadow.config.xml)
