macro(EXEC_DIFF_CHECK FILE1 FILE2)
    execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${FILE1} ${FILE2} RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT)
    if(RESULT)
        message(FATAL_ERROR "Error in diff: ${OUTPUT}")
    endif()
endmacro()
foreach(LOOPIDX RANGE 1 10)
	exec_diff_check(
		${CMAKE_BINARY_DIR}/determinism2a.shadow.data/hosts/peer${LOOPIDX}/peer${LOOPIDX}.testphold.1000.stdout
		${CMAKE_BINARY_DIR}/determinism2b.shadow.data/hosts/peer${LOOPIDX}/peer${LOOPIDX}.testphold.1000.stdout
	)
endforeach(LOOPIDX)
