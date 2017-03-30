## testing for the shadow simulator
    
## these are common flags that are needed for shadow plugins
add_cflags("-fPIC -fno-inline -fno-strict-aliasing -U_FORTIFY_SOURCE -Wno-unused-command-line-argument")
add_cflags(-std=gnu99)
add_definitions(-fPIC -g3 -DDEBUG -D_GNU_SOURCE)

## ensure that the Host LLVM plugin is built first
#get_property(LLVMHoistGlobalsPATH TARGET LLVMHoistGlobals PROPERTY LOCATION)

## a helper program for tests needing multiple processes
add_executable(shadow-test-launcher shd-test-launcher.c shd-test-launcher-common.c)
add_executable(shadow-test-launcher-fail shd-test-launcher-fail.c shd-test-launcher-common.c)

## add the test directories
add_subdirectory(dynlink)
add_subdirectory(preload)

add_subdirectory(bind)
add_subdirectory(epoll)
add_subdirectory(file)
add_subdirectory(phold)
add_subdirectory(poll)
add_subdirectory(pthreads)
add_subdirectory(random)
add_subdirectory(signal)
add_subdirectory(sleep)
add_subdirectory(tcp)
add_subdirectory(timerfd)
