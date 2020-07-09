## resources useful for simulations

macro(xzexe xzfilename)
    set(xz_output "${CMAKE_CURRENT_BINARY_DIR}/${xzfilename}")
    set(xz_input "${CMAKE_CURRENT_SOURCE_DIR}/${xzfilename}.xz")

    if(NOT EXISTS ${xz_output})
        message(STATUS "Decompressing ${xz_input}")
        execute_process(COMMAND "/bin/sh" "-c" "xz -dk ${xz_input} --stdout > ${xz_output}" RESULT_VARIABLE XZRESULT)
        if(NOT (${XZRESULT} EQUAL 0))
           message(FATAL_ERROR "xz failed. Is xz installed (xz-utils) and in your path?")
        endif(NOT (${XZRESULT} EQUAL 0))
    endif(NOT EXISTS ${xz_output})
    
    install(FILES ${xz_output} DESTINATION share/)
endmacro()

xzexe(topology.graphml.xml)

