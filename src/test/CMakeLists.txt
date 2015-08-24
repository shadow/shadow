## testing for the shadow simulator
## http://www.cmake.org/Wiki/CMake_Testing_With_CTest

## this project defines tests
enable_testing()

## these are common flags that are needed for shadow plugins
add_cflags("-fPIC -fno-inline -fno-strict-aliasing -U_FORTIFY_SOURCE")
add_definitions(-fPIC -g3 -DDEBUG -D_GNU_SOURCE)

## ensure that the Host LLVM plugin is built first
get_property(LLVMHoistGlobalsPATH TARGET LLVMHoistGlobals PROPERTY LOCATION)

# add the test directories
add_subdirectory(sleep)
add_subdirectory(poll)
add_subdirectory(epoll)
add_subdirectory(timerfd-epoll)
add_subdirectory(file-io)
add_subdirectory(socket)
add_subdirectory(pthreads)
