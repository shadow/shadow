cargo_build(NAME udp)
# The Rust library ends up depending on these if we use its standard library
# (e.g. call `println!`).
# FIXME: do a package search for -lpthread
target_link_libraries(udp INTERFACE ${DL_LIBRARIES} -lpthread)

add_executable(test-udp shd-test-udp.c)
target_link_libraries(test-udp udp)
add_test(NAME udp COMMAND test-udp)

add_shadow_plugin(shadow-plugin-test-udp shd-test-udp.c)
target_link_libraries(shadow-plugin-test-udp udp)
add_test(NAME udp-shadow COMMAND ${CMAKE_BINARY_DIR}/src/main/shadow -l debug -d udp.shadow.data ${CMAKE_CURRENT_SOURCE_DIR}/udp.test.shadow.config.xml)
