######################################################################################################
## CHECKS                                                                                           ##
######################################################################################################

function(CheckEmitLlvmFlag)
    file(WRITE "${CMAKE_BINARY_DIR}/emitllvm.test.c" "int main(int argc, char* argv[]){return 0;}\n\n")
    file(WRITE "${CMAKE_BINARY_DIR}/emitllvm.test.cpp" "int main(int argc, char* argv[]){return 0;}\n\n")

    message(STATUS "Checking for C LLVM compiler...")
    execute_process(COMMAND "${LLVM_BC_C_COMPILER}" "-emit-llvm" "-c" "emitllvm.test.c" "-o" "emitllvm.test.c.bc"
                  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                  OUTPUT_QUIET ERROR_QUIET)
    execute_process(COMMAND "${LLVM_BC_ANALYZER}" "emitllvm.test.c.bc"
                  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                  RESULT_VARIABLE AOUT_IS_NOT_BC
                  OUTPUT_QUIET ERROR_QUIET)
    if(AOUT_IS_NOT_BC)
        message(FATAL_ERROR "${LLVM_BC_C_COMPILER} is not valid LLVM compiler")
    endif()
    message(STATUS "Checking for C LLVM compiler... works.")

    message(STATUS "Checking for CXX LLVM compiler...")
    execute_process(COMMAND "${LLVM_BC_CXX_COMPILER}" "-emit-llvm" "-c" "emitllvm.test.cpp" "-o" "emitllvm.test.cpp.bc"
                  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                  OUTPUT_QUIET ERROR_QUIET)
    execute_process(COMMAND "${LLVM_BC_ANALYZER}" "emitllvm.test.cpp.bc"
                  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                  RESULT_VARIABLE AOUT_IS_NOT_BC
                  OUTPUT_QUIET ERROR_QUIET)
    if(AOUT_IS_NOT_BC)
        message(FATAL_ERROR "${LLVM_BC_CXX_COMPILER} is not valid LLVM compiler")
    endif()
    message(STATUS "Checking for CXX LLVM compiler... works.")
endfunction(CheckEmitLlvmFlag)

######################################################################################################
## ADD_BITCODE                                                                                      ##
######################################################################################################

macro(add_bitcode target)

    set(bcfiles "")
    foreach(srcfile ${ARGN})
        ## get the definitions, flags, and includes to use when compiling this file
        set(srcdefs "")
        get_directory_property(COMPILE_DEFINITIONS COMPILE_DEFINITIONS)
        foreach(DEFINITION ${COMPILE_DEFINITIONS})
            list(APPEND srcdefs -D${DEFINITION})
        endforeach()

        set(srcflags "")
        if(${srcfile} MATCHES "(.*).(cpp|cc)")
            separate_arguments(srcflags UNIX_COMMAND ${CMAKE_CXX_FLAGS})
            set(src_bc_compiler ${LLVM_BC_CXX_COMPILER})
        else()
            separate_arguments(srcflags UNIX_COMMAND ${CMAKE_C_FLAGS})
            set(src_bc_compiler ${LLVM_BC_C_COMPILER} )
        endif()
#    if(NOT ${CMAKE_C_FLAGS} STREQUAL "")
#        string(REPLACE " " ";" srcflags ${CMAKE_C_FLAGS})
#    endif()

        set(srcincludes "")
        get_directory_property(INCLUDE_DIRECTORIES INCLUDE_DIRECTORIES)
        foreach(DIRECTORY ${INCLUDE_DIRECTORIES})
            list(APPEND srcincludes -I${DIRECTORY})
        endforeach()
        
        get_filename_component(outfile ${srcfile} NAME)
        get_filename_component(infile ${srcfile} ABSOLUTE)

        ## the command to generate the bitcode for this file
        add_custom_command(OUTPUT ${outfile}.bc
          COMMAND ${src_bc_compiler} -emit-llvm ${srcdefs} ${srcflags} ${srcincludes}
            -c ${infile} -o ${outfile}.bc
          DEPENDS ${infile}
          IMPLICIT_DEPENDS CXX ${infile}
          COMMENT "Building LLVM bitcode ${outfile}.bc"
          VERBATIM
        )
        set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${outfile}.bc)

        ## keep track of every bitcode file we need to create
        list(APPEND bcfiles ${outfile}.bc)
    endforeach(srcfile)

    ## link all the bitcode files together to the target
    add_custom_command(OUTPUT ${target}.bc
        COMMAND ${LLVM_BC_LINK} ${BC_LD_FLAGS} -o ${CMAKE_CURRENT_BINARY_DIR}/${target}.bc ${bcfiles}
        DEPENDS ${bcfiles}
        COMMENT "Linking LLVM bitcode ${target}.bc"
    )
    set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${target}.bc)

    ## build all the bitcode files
    add_custom_target(${target} ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${target}.bc)
    set_property(TARGET ${target} PROPERTY LOCATION ${CMAKE_CURRENT_BINARY_DIR}/${target}.bc)

endmacro(add_bitcode)

######################################################################################################
## ADD_PLUGIN                                                                                       ##
######################################################################################################

macro(add_plugin target)

    set(bctargets "")
    foreach(bctarget ${ARGN})
        set(bcpath "")
        get_property(bcpath TARGET ${bctarget} PROPERTY LOCATION)
        if(${bcpath} STREQUAL "")
            message(FATAL_ERROR "Can't find property path for target '${bctarget}'")
        endif()
        list(APPEND bctargets ${bcpath})
    endforeach(bctarget)

    ## link all the bitcode targets together to the target, then hoist the globals
    add_custom_command(OUTPUT ${target}.bc
        COMMAND ${LLVM_BC_LINK} ${BC_LD_FLAGS} -o ${target}.bc ${bctargets}
        DEPENDS ${bctargets}
        COMMENT "Linking LLVM bitcode ${target}.bc"
    )
    set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${target}.bc)

    ## double check for correct path
    if((NOT DEFINED LLVMHoistGlobalsPATH) OR ( "${LLVMHoistGlobalsPATH}" STREQUAL ""))
        message(FATAL_ERROR "LLVMHoistGlobalsPATH is empty: have you added the path to LLVMHoistGlobals.so to the include path?")
    endif()
    ## cant use the following check, because the .so doesnt exist yet when cmake scans this file
    #if(NOT EXISTS "${LLVMHoistGlobalsPATH}")
    #    message(FATAL_ERROR "LLVMHoistGlobals.so does not exist at ${LLVMHoistGlobalsPATH}")
    #endif()

    add_custom_command(OUTPUT ${target}.hoisted.bc
        COMMAND ${LLVM_BC_OPT} -load=${LLVMHoistGlobalsPATH} -hoist-globals ${target}.bc -o ${target}.hoisted.bc
        DEPENDS ${target}.bc LLVMHoistGlobals ${LLVMHoistGlobalsPATH}
        COMMENT "Hoisting globals from ${target}.bc to ${target}.hoisted.bc"
    )
    set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${target}.hoisted.bc)

    ## now we need the actual .so to be built
    add_library(${target} SHARED ${target}.hoisted.bc)
    #add_dependencies(${target} ${target}.hoisted.bc)
    
    ## an alternative to the shared library is to build a position-independent executable as follows
    ## this can still be loaded by shadow, but the internal symbols will not be interposed by LD_PRELOAD
    #add_executable(${target} ${target}.hoisted.bc)
    
    set_target_properties(${target} PROPERTIES 
        INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib 
        INSTALL_RPATH_USE_LINK_PATH TRUE 
        LINK_FLAGS "-rdynamic -Wl,--no-as-needed"
        #LINK_FLAGS "-pie -rdynamic -Wl,--no-as-needed"
    )

    ## trick cmake so it builds the bitcode into a shared library
    set_property(TARGET ${target} PROPERTY LINKER_LANGUAGE C)
    set_property(SOURCE ${target}.hoisted.bc PROPERTY EXTERNAL_OBJECT TRUE)

    ## make sure we have the bitcode we need before building the .so
    foreach(bctarget ${ARGN})
        add_dependencies(${target} ${bctarget})
    endforeach(bctarget)

endmacro(add_plugin)

######################################################################################################
## ADD_SHADOW_PLUGIN                                                                                ##
######################################################################################################

macro(add_shadow_plugin target)
    add_bitcode(${target}-bitcode ${ARGN})
    add_plugin(${target} ${target}-bitcode)
endmacro(add_shadow_plugin)

######################################################################################################
## SETUP                                                                                            ##
######################################################################################################

find_program(LLVM_BC_C_COMPILER clang)
find_program(LLVM_BC_CXX_COMPILER clang++)
find_program(LLVM_BC_AR llvm-ar)
#find_program(LLVM_BC_RANLIB llvm-ranlib)
find_program(LLVM_BC_LINK llvm-link)
find_program(LLVM_BC_ANALYZER llvm-bcanalyzer)
find_program(LLVM_BC_OPT opt)

if (NOT (LLVM_BC_C_COMPILER AND LLVM_BC_CXX_COMPILER AND LLVM_BC_AR AND
      #LLVM_BC_RANLIB AND
	  LLVM_BC_LINK AND LLVM_BC_ANALYZER AND LLVM_BC_OPT))
  message(SEND_ERROR "Some of following tools have not been found:")
  if (NOT LLVM_BC_C_COMPILER)
     message(SEND_ERROR "LLVM_BC_C_COMPILER") 
  endif()
  if (NOT LLVM_BC_CXX_COMPILER) 
     message(SEND_ERROR "LLVM_BC_CXX_COMPILER")
  endif()
  if (NOT LLVM_BC_AR) 
     message(SEND_ERROR "LLVM_BC_AR") 
  endif()
#  if (NOT LLVM_BC_RANLIB) 
#     message(SEND_ERROR "LLVM_BC_RANLIB") 
#  endif()
  if (NOT LLVM_BC_LINK) 
     message(SEND_ERROR "LLVM_BC_LINK") 
  endif()
  if (NOT LLVM_BC_ANALYZER) 
     message(SEND_ERROR "LLVM_BC_ANALYZER") 
  endif()
  if (NOT LLVM_BC_OPT) 
     message(SEND_ERROR "LLVM_BC_OPT") 
  endif()
endif()

CheckEmitLlvmFlag()

#####
