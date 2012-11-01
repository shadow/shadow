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

## ADD_BITCODE ##
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
    if(NOT ${CMAKE_C_FLAGS} STREQUAL "")
        string(REPLACE " " ";" srcflags ${CMAKE_C_FLAGS})
    endif()

    set(srcincludes "")
    get_directory_property(INCLUDE_DIRECTORIES INCLUDE_DIRECTORIES)
    foreach(DIRECTORY ${INCLUDE_DIRECTORIES})
        list(APPEND srcincludes -I${DIRECTORY})
    endforeach()

    ## the command to generate the bitcode for this file
    add_custom_command(OUTPUT ${srcfile}.bc
      COMMAND ${LLVM_BC_C_COMPILER} -emit-llvm ${srcdefs} ${srcflags} ${srcincludes}
        -c ${CMAKE_CURRENT_SOURCE_DIR}/${srcfile}
        -o ${CMAKE_CURRENT_BINARY_DIR}/${srcfile}.bc
      COMMENT "Building LLVM bitcode ${srcfile}.bc"
      VERBATIM
    )
    set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${srcfile}.bc)

    ## keep track of every bitcode file we need to create
    list(APPEND bcfiles ${CMAKE_CURRENT_BINARY_DIR}/${srcfile}.bc)
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

## ADD_PLUGIN ##
macro(add_plugin target)

set(bctargets "")
foreach(bctarget ${ARGN})
    get_property(bcpath TARGET ${bctarget} PROPERTY LOCATION)
    list(APPEND bctargets ${bcpath})
endforeach(bctarget)

## link all the bitcode targets together to the target, then hoist the globals
add_custom_command(OUTPUT ${target}.bc
    COMMAND ${LLVM_BC_LINK} ${BC_LD_FLAGS} -o ${target}.bc ${bctargets}
    DEPENDS ${bctargets}
    COMMENT "Linking LLVM bitcode ${target}.bc"
)
set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${target}.bc)
add_custom_command(OUTPUT ${target}.hoisted.bc
    COMMAND ${LLVM_BC_OPT} -load=${LLVMHoistGlobalsPATH} -hoist-globals ${target}.bc -o ${target}.hoisted.bc
    DEPENDS ${target}.bc LLVMHoistGlobals
    COMMENT "Hoisting globals from ${target}.bc to ${target}.hoisted.bc"
)
set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${target}.hoisted.bc)

## now we need the actual .so to be built
add_library(${target} MODULE ${target}.hoisted.bc)
add_dependencies(${target} ${target}.hoisted.bc)

## trick cmake so it builds the bitcode into a shared library
set_property(TARGET ${target} PROPERTY LINKER_LANGUAGE C)
set_property(SOURCE ${target}.hoisted.bc PROPERTY EXTERNAL_OBJECT TRUE)

endmacro(add_plugin)

#####

find_program(LLVM_BC_C_COMPILER clang)
find_program(LLVM_BC_CXX_COMPILER clang++)
find_program(LLVM_BC_AR llvm-ar)
find_program(LLVM_BC_RANLIB llvm-ranlib)
find_program(LLVM_BC_LINK llvm-link)
find_program(LLVM_BC_ANALYZER llvm-bcanalyzer)
find_program(LLVM_BC_OPT opt)

if(NOT (LLVM_BC_C_COMPILER AND LLVM_BC_CXX_COMPILER AND LLVM_BC_AR AND
        LLVM_BC_RANLIB AND LLVM_BC_LINK AND LLVM_BC_ANALYZER))
  message(SEND_ERROR "Some of following tools have not been found:")
endif()

CheckEmitLlvmFlag()

get_property(LLVMHoistGlobalsPATH TARGET LLVMHoistGlobals PROPERTY LOCATION)
message(STATUS "LLVMHoistGlobalsPATH = ${LLVMHoistGlobalsPATH}")

#####