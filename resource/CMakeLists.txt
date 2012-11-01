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

macro(ddexe ddfilename ddkib)
    set(ddfilepath ${CMAKE_CURRENT_BINARY_DIR}/${ddfilename})
    if(NOT EXISTS ${ddfilepath})
        message(STATUS "Generating ${ddfilepath}")
        execute_process(COMMAND "dd" "if=/dev/urandom" "of=${ddfilepath}" "bs=1024" "count=${ddkib}" RESULT_VARIABLE DDRESULT OUTPUT_QUIET ERROR_QUIET)
        if(NOT (${DDRESULT} EQUAL 0))
            message(FATAL_ERROR "dd failed. Is it installed and in your path?")
        endif(NOT (${DDRESULT} EQUAL 0))
    endif(NOT EXISTS ${ddfilepath})
    install(FILES ${ddfilepath} DESTINATION share/)
endmacro()

xzexe(topology.xml)

ddexe("1KiB.urnd" 1)
ddexe("16KiB.urnd" 16)
ddexe("32KiB.urnd" 32)
ddexe("50KiB.urnd" 50)
ddexe("320KiB.urnd" 320)
ddexe("1MiB.urnd" 1024)
ddexe("5MiB.urnd" 5120)

