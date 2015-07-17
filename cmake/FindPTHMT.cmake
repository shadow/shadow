# - Check for the presence of PTHMT
#
# The following variables are set when PTHMT is found:
#  HAVE_PTHMT       = Set to true, if all components of PTHMT
#                          have been found.
#  PTHMT_INCLUDES   = Include path for the header files of PTHMT
#  PTHMT_LIBRARIES  = Link these to use PTHMT

## -----------------------------------------------------------------------------
## Check for the header files

find_path (PTHMT_INCLUDES pthmt.h
  PATHS ${CMAKE_EXTRA_INCLUDES} NO_DEFAULT_PATH
  )
if(NOT PTHMT_INCLUDES)
    find_path (PTHMT_INCLUDES pthmt.h
      PATHS /usr/local/include /usr/include /include /sw/include /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu/ ${CMAKE_EXTRA_INCLUDES}
      )
endif(NOT PTHMT_INCLUDES)

## -----------------------------------------------------------------------------
## Check for the library

find_library (PTHMT_LIBRARIES NAMES pthmt
  PATHS ${CMAKE_EXTRA_LIBRARIES} NO_DEFAULT_PATH
  )
if(NOT PTHMT_LIBRARIES)
    find_library (PTHMT_LIBRARIES NAMES pthmt
      PATHS /usr/local/lib /usr/lib /lib /sw/lib ${CMAKE_EXTRA_LIBRARIES}
      )
endif(NOT PTHMT_LIBRARIES)

## -----------------------------------------------------------------------------
## Actions taken when all components have been found

if (PTHMT_INCLUDES AND PTHMT_LIBRARIES)
  set (HAVE_PTHMT TRUE)
  if(EXISTS "${PTHMT_INCLUDES}/pthmt.h")
    set(PTHMT_VERSION_STRING_LINE_TEST "NONE")
    file(READ ${PTHMT_INCLUDES}/pthmt.h PTHMT_VFILE)
    string(REGEX MATCH "\#define PTH_VERSION_STR \"[0-9]+\\.[0-9]+\\.[0-9]+ \\(..-...-....\\)\"" PTHMT_VERSION_STRING_LINE_TEST ${PTHMT_VFILE})
    if(NOT ${PTHMT_VERSION_STRING_LINE_TEST} STREQUAL "NONE")
      set(PTHMT_VERSION_STRING_LINE ${PTHMT_VERSION_STRING_LINE_TEST})
      string(REGEX MATCHALL "([0-9]+)" PTHMT_VERSION_LIST ${PTHMT_VERSION_STRING_LINE})
      list(GET PTHMT_VERSION_LIST 0 PTHMT_VERSION_MAJOR_GUESS)
      list(GET PTHMT_VERSION_LIST 1 PTHMT_VERSION_MINOR_GUESS)
      list(GET PTHMT_VERSION_LIST 2 PTHMT_VERSION_PATCH_GUESS)
    endif(NOT ${PTHMT_VERSION_STRING_LINE_TEST} STREQUAL "NONE")
  endif()
else (PTHMT_INCLUDES AND PTHMT_LIBRARIES)
  if (NOT PTHMT_FIND_QUIETLY)
    if (NOT PTHMT_INCLUDES)
      message (STATUS "Unable to find PTHMT header files!")
    endif (NOT PTHMT_INCLUDES)
    if (NOT PTHMT_LIBRARIES)
      message (STATUS "Unable to find PTHMT library files!")
    endif (NOT PTHMT_LIBRARIES)
  endif (NOT PTHMT_FIND_QUIETLY)
endif (PTHMT_INCLUDES AND PTHMT_LIBRARIES)

if (HAVE_PTHMT)
  if (NOT PTHMT_FIND_QUIETLY)
    message (STATUS "Found components for PTHMT")
    message (STATUS "PTHMT_INCLUDES = ${PTHMT_INCLUDES}")
    message (STATUS "PTHMT_LIBRARIES = ${PTHMT_LIBRARIES}")
    message (STATUS "PTHMT_VERSION_STRING_LINE = ${PTHMT_VERSION_STRING_LINE}")
    message (STATUS "PTHMT_VERSION_MAJOR_GUESS = ${PTHMT_VERSION_MAJOR_GUESS}")
    message (STATUS "PTHMT_VERSION_MINOR_GUESS = ${PTHMT_VERSION_MINOR_GUESS}")
    message (STATUS "PTHMT_VERSION_PATCH_GUESS = ${PTHMT_VERSION_PATCH_GUESS}")
  endif (NOT PTHMT_FIND_QUIETLY)
else (HAVE_PTHMT)
  if (PTHMT_FIND_REQUIRED)
    message (FATAL_ERROR "Could not find PTHMT!")
  endif (PTHMT_FIND_REQUIRED)
endif (HAVE_PTHMT)

mark_as_advanced (
  HAVE_PTHMT
  PTHMT_LIBRARIES
  PTHMT_INCLUDES
  )
