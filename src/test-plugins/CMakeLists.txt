add_executable(sleepy sleepy.c)
install(TARGETS sleepy DESTINATION bin)

# FIXME(rwails) I don't think should be required... but symbol resoultion for
# symbols in rt and shadow-shmem are failing without the links made to sleepy.
target_link_libraries(sleepy rt shadow-shmem)
