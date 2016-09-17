## testing for the shadow simulator
## http://www.cmake.org/Wiki/CMake_Testing_With_CTest

## this project defines tests
enable_testing()

## these are common flags that are needed for shadow plugins
add_cflags("-fPIC -fno-inline -fno-strict-aliasing -U_FORTIFY_SOURCE -Wno-unused-command-line-argument")
add_definitions(-fPIC -g3 -DDEBUG -D_GNU_SOURCE)

## ensure that the Host LLVM plugin is built first
get_property(LLVMHoistGlobalsPATH TARGET LLVMHoistGlobals PROPERTY LOCATION)

## a helper program for tests needing multiple processes
add_executable(shadow-test-launcher shd-test-launcher.c)

## add the test directories
add_subdirectory(bind)
add_subdirectory(sleep)
add_subdirectory(timerfd)
add_subdirectory(poll)
add_subdirectory(epoll)
add_subdirectory(file)
add_subdirectory(tcp)
add_subdirectory(pthreads)
