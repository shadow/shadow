#flex/bison
#set(FLEX_PREFIX_OUTPUTS true)
set(BISON_GENERATE_DEFINES true)
#set(BISON_PREFIX_OUTPUTS true)
include(CMakeUserUseBison)
include(CMakeUserUseFlex)

if(NOT BISON_EXECUTABLE OR NOT FLEX_EXECUTABLE)
	MESSAGE(SEND_ERROR "Bison and Flex are required")
endif()

BISON_FILE(dsim_parser.y)
FLEX_FILE(dsim_lexer.l)

SET_SOURCE_FILES_PROPERTIES(lex.yy.c dsim_parser.tab.c PROPERTIES COMPILE_FLAGS "-I \"${CMAKE_CURRENT_SOURCE_DIR}\"")

set(dsim_srcs
	dsim_utils.c
    dsim_parser.y 
    dsim_lexer.l 
    dsim_parser.tab.c 
    lex.yy.c
)

add_library(shadow-dsim STATIC ${dsim_srcs})
