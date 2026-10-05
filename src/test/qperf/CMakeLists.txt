# ------------------------------------------------------------------------
# Helper Function: instantiate_template
# ------------------------------------------------------------------------
#   TEMPLATE   – path to the yaml template (relative to the source dir).
#   OUTPUT     – file name (relative to ${CMAKE_CURRENT_BINARY_DIR}) to write
#                the processed config.
#   VARS...    – list of "NAME=VALUE" entries; each `${NAME}` in the template
#                will be replaced by VALUE.
#
# Example:
#   instantiate_template(
#       ${CMAKE_SOURCE_DIR}/config/template.yaml
#       cfg_runA.yaml
#       BWDOWN=100Mbps   BWUP=100Mbps   LATENCY=10ms
#       PACKETLOSS=0.0   NUMBYTES=1048576
#   )
#
function(instantiate_template TEMPLATE OUTPUT)
    # --------------------------------------------------------------------
    # Verify the template exists.
    # --------------------------------------------------------------------
    if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${TEMPLATE}")
        message(FATAL_ERROR "Template file '${CMAKE_CURRENT_SOURCE_DIR}/${TEMPLATE}' not found.")
    endif()

    # --------------------------------------------------------------------
    # Read the whole template file contents into a variable.
    # --------------------------------------------------------------------
    file(READ "${TEMPLATE}" _contents)

    # --------------------------------------------------------------------
    # Apply each NAME=VALUE replacement.
    # --------------------------------------------------------------------
    foreach(pair IN LISTS ARGN)
        string(FIND "${pair}" "=" _eq)
        if (_eq EQUAL -1)
            message(FATAL_ERROR "Bad variable definition '${pair}'. Use NAME=VALUE.")
        endif()

        # Get the value part of the key=value pair.
        string(SUBSTRING "${pair}" 0 ${_eq} _name)
        math(EXPR _val_start "${_eq}+1")
        string(SUBSTRING "${pair}" ${_val_start} -1 _value)

        # Replace the pattern ${NAME} with the supplied value.
        string(REGEX REPLACE "\\$\\{${_name}\\}" "${_value}" _contents "${_contents}")
    endforeach()

    # --------------------------------------------------------------------
    # Write the processed contents into the destination file.
    # --------------------------------------------------------------------
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT}" "${_contents}")
endfunction()

# Instantiates the qperf template with the given params and adds a shadow test for it.
# Bandwidths are in Mbit, and latency is in ms.
function(add_qperf_test BWDOWN BWUP LATENCY PACKETLOSS NUMBYTES NAME)
    instantiate_template(
        perf.yaml.template
        ${NAME}.yaml
        "BWDOWN=\"${BWDOWN} Mbit\""
        "BWUP=\"${BWUP} Mbit\""
        "LATENCY=\"${LATENCY} ms\""
        "PACKETLOSS=${PACKETLOSS}"
        "NUMBYTES=${NUMBYTES}"
    )

    add_shadow_tests(
        BASENAME ${NAME}
        SHADOW_CONFIG ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.yaml
        ARGS
            --strace-logging-mode=off
            --parallelism 2
        LOGLEVEL info
        CONFIGURATIONS extra
        PROPERTIES
          TIMEOUT 300
          LABELS quic
    )
endfunction()

if(SHADOW_EXTRA_TESTS STREQUAL ON)
    # Test a single quic connection across a variety of network characteristics.
    add_qperf_test(1 1  10 0.0 1048576 qperf_1mbit_10ms)
    add_qperf_test(1 1 100 0.0 1048576 qperf_1mbit_100ms)
    add_qperf_test(1 1 300 0.0 1048576 qperf_1mbit_300ms)

    add_qperf_test(10 10  10 0.0 1048576 qperf_10mbit_10ms)
    add_qperf_test(10 10 100 0.0 1048576 qperf_10mbit_100ms)
    add_qperf_test(10 10 300 0.0 1048576 qperf_10mbit_300ms)
endif()
