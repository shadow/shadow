## resources useful for simulations

macro(xzexe xzfilename)
    set(xz_output "${CMAKE_CURRENT_SOURCE_DIR}/${xzfilename}")
    set(xz_output_target "${CMAKE_CURRENT_BINARY_DIR}/${xzfilename}")
    set(xz_input "${xz_output}.xz")

    if(NOT EXISTS ${xz_output_target})
        if(NOT EXISTS ${xz_output})
            message(STATUS "Decompressing ${xz_input}")
            execute_process(COMMAND "xz" "-dk" "${xz_input}" RESULT_VARIABLE XZRESULT)
            if(NOT (${XZRESULT} EQUAL 0))
                message(FATAL_ERROR "xz failed. Is xz installed (xz-utils) and in your path?")
            endif(NOT (${XZRESULT} EQUAL 0))
            execute_process(COMMAND "sleep" "1" RESULT_VARIABLE SLEEPRESULT)
        endif(NOT EXISTS ${xz_output})
        execute_process(COMMAND "mv" "${xz_output}" "${xz_output_target}" RESULT_VARIABLE MVRESULT)
    endif(NOT EXISTS ${xz_output_target})
    
    install(FILES ${xz_output_target} DESTINATION share/)
endmacro()

xzexe(topology.graphml.xml)

