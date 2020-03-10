find_package(RT REQUIRED)
find_package(M REQUIRED)

add_executable(shd-test-shmem shd-test-shmem.c)
target_link_libraries(shd-test-shmem shd-shmem ${RT_LIBRARIES} ${M_LIBRARIES})
include_directories(${CMAKE_SOURCE_DIR}/src/main/shmem)
add_test(NAME shmem COMMAND shd-test-shmem)
