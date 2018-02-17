add_cflags("-Wl,--warn-unresolved-symbols -L. -Wl,--no-as-needed")

SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)

if(NOT ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64" )
    add_cflags(-m32)
endif()

add_executable(hello hello.c)


add_library(a SHARED liba.c)
add_library(b SHARED libb.c)
add_library(d SHARED libd.c)
add_library(e SHARED libe.c)
add_library(f SHARED libf.c)
add_library(g SHARED libg.c)
add_library(h SHARED libh.c)
add_library(i SHARED libi.c)
add_library(j SHARED libj.c)
add_library(k SHARED libk.c)
add_library(l SHARED libl.c)
add_library(n SHARED libn.c)
add_library(o SHARED libo.c)
add_library(p SHARED libp.c)
add_library(q SHARED libq.c)
add_library(r SHARED libr.c)
add_library(s SHARED libs.c)
add_library(t SHARED libt.c)
add_library(efl SHARED libefl.c)


set_target_properties(p PROPERTIES
    LINK_FLAGS "-nostdlib -Wl,--no-as-needed"
)
set_target_properties(q PROPERTIES
    LINK_FLAGS "-nostdlib -Wl,--no-as-needed"
)


target_link_libraries(a b)
target_link_libraries(b d e)
target_link_libraries(d f)
target_link_libraries(e f)
target_link_libraries(g h)
target_link_libraries(j k)
target_link_libraries(k l)
target_link_libraries(p q)
target_link_libraries(efl f l)
target_link_libraries(s t)

# forced circular dependency
target_link_libraries(n -ldl)
target_link_libraries(o -ldl n)
file(COPY libn.c DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(RENAME ${CMAKE_CURRENT_BINARY_DIR}/libn.c ${CMAKE_CURRENT_BINARY_DIR}/libnn.c)
add_library(nn SHARED ${CMAKE_CURRENT_BINARY_DIR}/libnn.c)
target_link_libraries(nn -ldl o)




add_executable(test0 test0.c)
add_dependencies(test0 a)
target_link_libraries(test0 a -ldl)
add_test(NAME elfloader-test0 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test0 ${CMAKE_CURRENT_SOURCE_DIR})


add_executable(test0_1 test0_1.c)
add_dependencies(test0_1 b a)
target_link_libraries(test0_1 b a -ldl)
add_test(NAME elfloader-test0_1 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test0_1 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test0_2 test0_2.c)
target_link_libraries(test0_2 -ldl)
add_test(NAME elfloader-test0_2 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test0_2 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test1 test1.c)
target_link_libraries(test1 -ldl)
add_test(NAME elfloader-test1 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test1 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test2 test2.c)
target_link_libraries(test2 -ldl)
add_test(NAME elfloader-test2 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test2 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test3 test3.c)
target_link_libraries(test3 -ldl)
add_test(NAME elfloader-test3 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test3 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test4 test4.c)
target_link_libraries(test4 -ldl)
add_test(NAME elfloader-test4 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test4 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test5 test5.c)
target_link_libraries(test5 -ldl)
add_test(NAME elfloader-test5 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test5 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test6 test6.c)
target_link_libraries(test6 -ldl)
add_test(NAME elfloader-test6 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test6 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test7 test7.c)
target_link_libraries(test7 -ldl)
add_test(NAME elfloader-test7 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test7 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test8 test8.c)
target_link_libraries(test8 -ldl)
add_test(NAME elfloader-test8 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test8 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test8_5 test8_5.c)
target_link_libraries(test8_5 -lpthread -ldl)
add_test(NAME elfloader-test8_5 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test8_5 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test9 test9.c)
target_link_libraries(test9 -lpthread -ldl)
add_test(NAME elfloader-test9 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test9 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test10 test10.c)
target_link_libraries(test10 -lpthread -ldl)
add_test(NAME elfloader-test10 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test10 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test11 test11.c)
target_link_libraries(test11 -ldl)
add_test(NAME elfloader-test11 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test11 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test12 test12.c)
target_link_libraries(test12 -lpthread -ldl)
add_test(NAME elfloader-test12 COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P
  ${CMAKE_CURRENT_SOURCE_DIR}/test12.cmake
  )

add_executable(test13 test13.c)
add_dependencies(test13 f)
target_link_libraries(test13 f -ldl)
add_test(NAME elfloader-test13 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test13 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test14 test14.c)
add_custom_command(TARGET test14 POST_BUILD COMMAND mv ${CMAKE_CURRENT_BINARY_DIR}/libnn.so ${CMAKE_CURRENT_BINARY_DIR}/libn.so)
add_dependencies(test14 n nn)
target_link_libraries(test14 n -ldl)
add_test(NAME elfloader-test14 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test14 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test15 test15.c)
target_link_libraries(test15 -ldl)
add_test(NAME elfloader-test15 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test15 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test16 test16.cc)
target_link_libraries(test16 -ldl)
add_test(NAME elfloader-test16 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test16 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test17 test17.c)
target_link_libraries(test17 -ldl)
add_test(NAME elfloader-test17 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test17 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test18 test18.c)
target_link_libraries(test18 -ldl)
add_test(NAME elfloader-test18 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test18 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test19 test19.c)
target_link_libraries(test19 f -ldl)
add_test(NAME elfloader-test19 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test19 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test20 test20.c)
target_link_libraries(test20 -ldl)
set_target_properties(test20 PROPERTIES
    LINK_FLAGS "-pie -Wl,--no-as-needed"
)
add_test(NAME elfloader-test20 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test20 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test21 test21.cc)
target_link_libraries(test21 -ldl)
add_test(NAME elfloader-test21 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test21 ${CMAKE_CURRENT_SOURCE_DIR})

if( ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64" )
    add_library(lb22 SHARED lb22.c)
    set_target_properties(lb22 PROPERTIES
        PREFIX ""
        COMPILE_FLAGS "-Wall -mcmodel=large"
        LINK_FLAGS "-Wl,--no-as-needed"
    )

    add_executable(test22 test22.c)
    set_target_properties(test22 PROPERTIES
        COMPILE_FLAGS "-Wall"
        LINK_FLAGS ""
    )
    add_test(NAME elfloader-test22 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test22 ${CMAKE_CURRENT_SOURCE_DIR})


    target_link_libraries(test22 -ldl)
endif()

add_executable(test23 test23.c)
target_link_libraries(test23 -ldl)
add_test(NAME elfloader-test23 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test23 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test24 test24.c)
target_link_libraries(test24 -lpthread -ldl)
add_test(NAME elfloader-test24 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test24 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test25 test25.c)
target_link_libraries(test25 -lpthread -ldl)
add_test(NAME elfloader-test25 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test25 ${CMAKE_CURRENT_SOURCE_DIR})

#add_executable(test26 test26.c)
#target_link_libraries(test26 -lpthread -ldl)
#add_test(NAME elfloader-test26 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test26 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test27 test27.c)
target_link_libraries(test27 -ldl)
add_test(NAME elfloader-test27 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test27 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test28 test28.c)
target_link_libraries(test28 vdl -lpthread -ldl)
add_test(NAME elfloader-test28 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test28 ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test29 test29.c)
target_link_libraries(test29 vdl -lpthread -ldl)
add_test(NAME elfloader-test29 COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh test29 ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TEST elfloader-test29 PROPERTY ENVIRONMENT LD_STATIC_TLS_EXTRA=1000000)

add_test(NAME elfloader-registers COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/registers.sh)

set_tests_properties(
        elfloader-test15
        PROPERTIES RUN_SERIAL true
)
