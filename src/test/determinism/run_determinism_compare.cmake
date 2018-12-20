macro(EXEC_DIFF_CHECK FILE1 FILE2)
    execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${FILE1} ${FILE2} RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT)
    if(RESULT)
        message(FATAL_ERROR "Error in diff: ${OUTPUT}")
    endif()
endmacro()
foreach(LOOPIDX RANGE 1 50)
	exec_diff_check(
		${CMAKE_BINARY_DIR}/determinism1.shadow.data/hosts/testnode${LOOPIDX}/stdout-testnode${LOOPIDX}.testdeterminism.1000.log
		${CMAKE_BINARY_DIR}/determinism2.shadow.data/hosts/testnode${LOOPIDX}/stdout-testnode${LOOPIDX}.testdeterminism.1000.log
	)
endforeach(LOOPIDX)