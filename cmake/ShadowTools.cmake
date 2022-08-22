######################################################################################################
## ADD_CFLAGS                                                                                       ##
######################################################################################################

macro(add_cflags)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ARGN}")
endmacro(add_cflags)

######################################################################################################
## ADD_LDFLAGS                                                                                      ##
######################################################################################################

macro(add_ldflags)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${ARGN}")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${ARGN}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${ARGN}")
    # We do *not* add to CMAKE_STATIC_LINKER_FLAGS, since that is used when "linking" a static
    # library with `ar`, not when statically linking an executable.
endmacro(add_ldflags)

######################################################################################################
## COMPILE_TEST - helper for testing if a source file compiles correctly in our environment         ##
######################################################################################################

macro(compile_test resultvar srcfile)
    try_compile(${resultvar} "${CMAKE_BINARY_DIR}" "${srcfile}" COMPILE_DEFINITIONS "-D_GNU_SOURCE=1 -c")
    if(${resultvar} STREQUAL "TRUE")
        set(${resultvar} 1)
    else()
        set(${resultvar} 0)
    endif()
    MESSAGE(STATUS "${resultvar} = ${${resultvar}}")
endmacro()

######################################################################################################

function(link_flags_for_lib ARG_LIBRARY)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "FLAGS_VAR;DEPENDS_VAR;VISITED_VAR" "VISITED")
    if(DEFINED ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "Unrecognized arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()
    list(FIND ARG_VISITED "${ARG_LIBRARY}" IDX)
    if(NOT ${IDX} EQUAL -1)
        # Already visited. Skip.
        set(${ARG_FLAGS_VAR} "" PARENT_SCOPE)
        set(${ARG_DEPENDS_VAR} "" PARENT_SCOPE)
        set(${ARG_VISITED_VAR} "" PARENT_SCOPE)
    elseif(ARG_LIBRARY MATCHES "(.*)/lib(.*)\(.so|.a)")
        # LIBRARY is a full path to a library. Split into path and library name.
        set(FLAGS "")
        list(APPEND FLAGS "-L${CMAKE_MATCH_1}" "-l${CMAKE_MATCH_2}")
        set(${ARG_FLAGS_VAR} "${FLAGS}" PARENT_SCOPE)
        set(${ARG_DEPENDS_VAR} "" PARENT_SCOPE)
        set(${ARG_VISITED_VAR} "${ARG_LIBRARY}" PARENT_SCOPE)
    elseif(ARG_LIBRARY MATCHES "^-l.*")
        # LIBRARY is already a link flag. Use as-is.
        set(${ARG_FLAGS_VAR} "${ARG_LIBRARY}" PARENT_SCOPE)
        set(${ARG_DEPENDS_VAR} "" PARENT_SCOPE)
        set(${ARG_VISITED_VAR} "${ARG_LIBRARY}" PARENT_SCOPE)
    else()
        # ARG_LIBRARY is a target. We need to both link to ARG_LIBRARY itself,
        # and its recursive dependencies.

        # Init in this scope
        set(DEPENDS "${ARG_LIBRARY}")
        set(VISITED "${ARG_LIBRARY}")
        set(FLAGS "")

        get_target_property(TYPE "${ARG_LIBRARY}" TYPE)
        if ("${TYPE}" STREQUAL "STATIC_LIBRARY")
            string(TOUPPER "${CMAKE_BUILD_TYPE}" UPPER_BUILD_TYPE)
            get_target_property(IMPORTED_LOCATION "${ARG_LIBRARY}" IMPORTED_LOCATION_${UPPER_BUILD_TYPE})
            if (IMPORTED_LOCATION MATCHES "(.*)/lib(.*)\.a")
                list(APPEND FLAGS "-L${CMAKE_MATCH_1}" "-l${CMAKE_MATCH_2}")
            else()
                get_target_property(BINARY_DIR ${ARG_LIBRARY} BINARY_DIR)
                get_target_property(NAME ${ARG_LIBRARY} NAME)
                list(APPEND FLAGS "-L${BINARY_DIR}" "-l${NAME}")
            endif()
        elseif("${TYPE}" STREQUAL "INTERFACE_LIBRARY")
            # Nothing to do for the target itself, but we'll recurse
            # into its dependencies below.
        else()
            message(FATAL_ERROR "Unhandled TYPE:${TYPE} for ${ARG_LIBRARY}")
        endif()

        get_target_property(REC_INTERFACE_LINK_LIBRARIES "${ARG_LIBRARY}" INTERFACE_LINK_LIBRARIES)
        if (REC_INTERFACE_LINK_LIBRARIES)
            link_flags_for_libs(
                FLAGS_VAR REC_FLAGS
                DEPENDS_VAR REC_DEPENDS
                VISITED_VAR REC_VISITED
                VISITED ${VISITED} ${ARG_VISITED}
                LIBRARIES ${REC_INTERFACE_LINK_LIBRARIES})
            list(APPEND FLAGS ${REC_FLAGS})
            list(APPEND DEPENDS ${REC_DEPENDS})
            list(APPEND VISITED ${REC_VISITED})
        endif()

        set(${ARG_FLAGS_VAR} "${FLAGS}" PARENT_SCOPE)
        set(${ARG_DEPENDS_VAR} "${DEPENDS}" PARENT_SCOPE)
        set(${ARG_VISITED_VAR} "${VISITED}" PARENT_SCOPE)
    endif()
endfunction()

function(link_flags_for_libs)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "FLAGS_VAR;DEPENDS_VAR;VISITED_VAR" "LIBRARIES;VISITED")
    if(DEFINED ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "Unrecognized arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    set(FLAGS "")
    set(DEPENDS "")
    set(VISITED "")

    foreach(ARG_LIBRARY IN LISTS ARG_LIBRARIES)
        link_flags_for_lib(
            "${ARG_LIBRARY}"
            FLAGS_VAR THIS_FLAGS
            DEPENDS_VAR THIS_DEPENDS
            VISITED_VAR THIS_VISITED
            VISITED ${VISITED} ${ARG_VISITED}
            )
        if(THIS_FLAGS)
            list(INSERT FLAGS 0 ${THIS_FLAGS})
        endif()
        if(THIS_DEPENDS)
            list(INSERT DEPENDS 0 ${THIS_DEPENDS})
        endif()
        list(APPEND VISITED ${THIS_VISITED})
    endforeach()
    set(${ARG_FLAGS_VAR} "${FLAGS}" PARENT_SCOPE)
    set(${ARG_DEPENDS_VAR} "${DEPENDS}" PARENT_SCOPE)
    if(ARG_VISITED_VAR)
        set(${ARG_VISITED_VAR} "${VISITED}" PARENT_SCOPE)
    endif()
endfunction()

function(rust_static_lib TARGET)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "SOURCEDIR;TARGETDIR;CRATENAME;RUSTFLAGS;CARGO_ENV_VARS;FEATURES" "INTERFACE_LIBRARIES")
    if(DEFINED ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "Unrecognized arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    if(ARG_CRATENAME)
        set(CRATENAME ${ARG_CRATENAME})
    else()
        set(CRATENAME ${TARGET})
    endif()

    if(ARG_SOURCEDIR)
        set(SOURCEDIR ${ARG_SOURCEDIR})
    else()
        set(SOURCEDIR ${CMAKE_CURRENT_SOURCE_DIR})
    endif()

    if(ARG_TARGETDIR)
        set(TARGETDIR ${ARG_TARGETDIR})
    else()
        set(TARGETDIR "${CMAKE_CURRENT_BINARY_DIR}/target")
    endif()

    set(RUSTFLAGS ${ARG_RUSTFLAGS})
    set(CARGO_ENV_VARS ${ARG_CARGO_ENV_VARS})
    if(SHADOW_COVERAGE STREQUAL ON)
        # https://github.com/shadow/shadow/issues/1236
        set(RUSTFLAGS "${RUSTFLAGS} --remap-path-prefix \"=${CMAKE_CURRENT_SOURCE_DIR}/\"")

        ## from https://github.com/mozilla/grcov
        set(RUSTFLAGS "${RUSTFLAGS} -Zprofile -Ccodegen-units=1 -Copt-level=0 -Clink-dead-code \
                    -Coverflow-checks=off -Zpanic_abort_tests -Cpanic=abort")
        set(CARGO_ENV_VARS "CARGO_INCREMENTAL=0 RUSTDOCFLAGS=\"-Cpanic=abort\"")
    endif()

    string(REPLACE "-" "_" UNDERBARRED_CRATENAME ${CRATENAME})

    set(LIBFILE "lib${UNDERBARRED_CRATENAME}.a")

    if(CMAKE_BUILD_TYPE STREQUAL Debug)
        message(STATUS "Building Rust library in debug mode.")
        set(RUST_BUILD_TYPE "debug")
        set(RUST_BUILD_FLAG "")
    else()
        message(STATUS "Building Rust library in release mode.")
        set(RUST_BUILD_TYPE "release")
        set(RUST_BUILD_FLAG "--release")
    endif()

    set(CARGO_ENV_VARS "${CARGO_ENV_VARS} RUSTFLAGS=\"${RUSTFLAGS}\"")
    include(ExternalProject)

    if(ARG_INTERFACE_LIBRARIES)
        link_flags_for_libs(FLAGS_VAR LINK_FLAGS DEPENDS_VAR DEPENDS LIBRARIES ${ARG_INTERFACE_LIBRARIES})
        # Simulate the list JOIN operator for older versions of cmake.
        if(LINK_FLAGS)
            set(LINK_FLAGS_STR "")
            foreach(FLAG IN LISTS LINK_FLAGS)
                set(LINK_FLAGS_STR "${LINK_FLAGS_STR} ${FLAG}")
            endforeach()
            set(RUSTFLAGS "${RUSTFLAGS} ${LINK_FLAGS_STR}")
        endif()
        set(CARGO_ENV_VARS "${CARGO_ENV_VARS} RUSTFLAGS=\"${RUSTFLAGS}\"")
    endif()

    ## build the rust library
    ExternalProject_Add(
        ${TARGET}-test-project
        PREFIX ${CMAKE_CURRENT_BINARY_DIR}
        BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}
        BUILD_ALWAYS 1
        DOWNLOAD_COMMAND ""
        CONFIGURE_COMMAND ""
        BUILD_COMMAND bash -c "${CARGO_ENV_VARS} cargo build ${RUST_BUILD_FLAG} --lib --tests --target-dir \"${TARGETDIR}\" --features \"${ARG_FEATURES}\""
        BUILD_BYPRODUCTS ${TARGETDIR}/debug/${LIBFILE} ${TARGETDIR}/release/${LIBFILE}
        INSTALL_COMMAND ""
        LOG_BUILD OFF
        DEPENDS ${DEPENDS}
    )
    add_library(${TARGET} STATIC IMPORTED GLOBAL)
    add_dependencies(${TARGET} ${TARGET}-test-project)
    set_target_properties(${TARGET} PROPERTIES IMPORTED_LOCATION_DEBUG ${TARGETDIR}/debug/${LIBFILE})
    set_target_properties(${TARGET} PROPERTIES IMPORTED_LOCATION_RELEASE ${TARGETDIR}/release/${LIBFILE})
    if(ARG_INTERFACE_LIBRARIES)
        target_link_libraries(${TARGET} INTERFACE ${ARG_INTERFACE_LIBRARIES})
    endif()

    ## we can't predict exact executable names until this is fixed: https://github.com/rust-lang/cargo/issues/1924
    add_test(NAME ${TARGET}-unit-tests COMMAND bash -c "exec \"$(find ${TARGETDIR}/${RUST_BUILD_TYPE}/deps/ \
                                                -type f -executable -name '${UNDERBARRED_CRATENAME}*' -print | head -n 1)\" --color always")
    set_property(TEST ${TARGET}-unit-tests PROPERTY ENVIRONMENT "RUST_BACKTRACE=1")

endfunction()