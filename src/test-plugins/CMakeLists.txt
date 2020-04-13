add_executable(sleepy sleepy.c)
install(TARGETS sleepy DESTINATION bin)

target_link_libraries(sleepy rt shadow-shmem)
