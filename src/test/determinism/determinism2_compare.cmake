macro(EXEC_DIFF_CHECK FILE1 FILE2)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E compare_files ${FILE1} ${FILE2}
        RESULT_VARIABLE RESULT
        OUTPUT_VARIABLE STDOUTPUT
        ERROR_VARIABLE STDERROR)
    message(STATUS "Diff returned ${RESULT} for 'diff ${FILE1} ${FILE2}'")
    if(RESULT)
        message(STATUS "Diff stdout is: ${STDOUTPUT}")
        message(STATUS "Diff stderr is: ${STDERROR}")
        message(FATAL_ERROR "Differences found; test failed")
    endif()
endmacro()
foreach(LOOPIDX RANGE 1 10)
    exec_diff_check(
        ${CMAKE_BINARY_DIR}/determinism2a-shadow.data/hosts/peer${LOOPIDX}/test-phold.1000.stdout
        ${CMAKE_BINARY_DIR}/determinism2b-shadow.data/hosts/peer${LOOPIDX}/test-phold.1000.stdout
    )
    exec_diff_check(
        ${CMAKE_BINARY_DIR}/determinism2a-shadow.data/hosts/peer${LOOPIDX}/test-phold.1000.stdout
        ${CMAKE_BINARY_DIR}/determinism2c-shadow.data/hosts/peer${LOOPIDX}/test-phold.1000.stdout
    )
    exec_diff_check(
        ${CMAKE_BINARY_DIR}/determinism2a-shadow.data/hosts/peer${LOOPIDX}/test-phold.1000.strace
        ${CMAKE_BINARY_DIR}/determinism2b-shadow.data/hosts/peer${LOOPIDX}/test-phold.1000.strace
    )
    exec_diff_check(
        ${CMAKE_BINARY_DIR}/determinism2a-shadow.data/hosts/peer${LOOPIDX}/test-phold.1000.strace
        ${CMAKE_BINARY_DIR}/determinism2c-shadow.data/hosts/peer${LOOPIDX}/test-phold.1000.strace
    )
    exec_diff_check(
        ${CMAKE_BINARY_DIR}/determinism2a-shadow.data/hosts/peer${LOOPIDX}/127.0.0.1.pcap
        ${CMAKE_BINARY_DIR}/determinism2b-shadow.data/hosts/peer${LOOPIDX}/127.0.0.1.pcap
    )
    exec_diff_check(
        ${CMAKE_BINARY_DIR}/determinism2a-shadow.data/hosts/peer${LOOPIDX}/127.0.0.1.pcap
        ${CMAKE_BINARY_DIR}/determinism2c-shadow.data/hosts/peer${LOOPIDX}/127.0.0.1.pcap
    )
    exec_diff_check(
        ${CMAKE_BINARY_DIR}/determinism2a-shadow.data/hosts/peer${LOOPIDX}/11.0.0.${LOOPIDX}.pcap
        ${CMAKE_BINARY_DIR}/determinism2b-shadow.data/hosts/peer${LOOPIDX}/11.0.0.${LOOPIDX}.pcap
    )
    exec_diff_check(
        ${CMAKE_BINARY_DIR}/determinism2a-shadow.data/hosts/peer${LOOPIDX}/11.0.0.${LOOPIDX}.pcap
        ${CMAKE_BINARY_DIR}/determinism2c-shadow.data/hosts/peer${LOOPIDX}/11.0.0.${LOOPIDX}.pcap
    )
endforeach(LOOPIDX)
