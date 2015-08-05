# - Check for the presence of RPTH
#
# The following variables are set when RPTH is found:
#  HAVE_RPTH       = Set to true, if all components of RPTH
#                          have been found.
#  RPTH_INCLUDES   = Include path for the header files of RPTH
#  RPTH_LIBRARIES  = Link these to use RPTH

## -----------------------------------------------------------------------------
## Check for the header files

find_path (RPTH_INCLUDES rpth.h
  PATHS ${CMAKE_EXTRA_INCLUDES} NO_DEFAULT_PATH
  )
if(NOT RPTH_INCLUDES)
    find_path (RPTH_INCLUDES rpth.h
      PATHS /usr/local/include /usr/include /include /sw/include /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu/ ${CMAKE_EXTRA_INCLUDES}
      )
endif(NOT RPTH_INCLUDES)

## -----------------------------------------------------------------------------
## Check for the library

find_library (RPTH_LIBRARIES NAMES rpth
  PATHS ${CMAKE_EXTRA_LIBRARIES} NO_DEFAULT_PATH
  )
if(NOT RPTH_LIBRARIES)
    find_library (RPTH_LIBRARIES NAMES rpth
      PATHS /usr/local/lib /usr/lib /lib /sw/lib ${CMAKE_EXTRA_LIBRARIES}
      )
endif(NOT RPTH_LIBRARIES)

## -----------------------------------------------------------------------------
## Actions taken when all components have been found

if (RPTH_INCLUDES AND RPTH_LIBRARIES)
  set (HAVE_RPTH TRUE)
  if(EXISTS "${RPTH_INCLUDES}/rpth.h")
    set(RPTH_VERSION_STRING_LINE_TEST "NONE")
    file(READ ${RPTH_INCLUDES}/rpth.h RPTH_VFILE)
    string(REGEX MATCH "\#define PTH_VERSION_STR \"[0-9]+\\.[0-9]+\\.[0-9]+ \\(..-...-....\\)\"" RPTH_VERSION_STRING_LINE_TEST ${RPTH_VFILE})
    if(NOT ${RPTH_VERSION_STRING_LINE_TEST} STREQUAL "NONE")
      set(RPTH_VERSION_STRING_LINE ${RPTH_VERSION_STRING_LINE_TEST})
      string(REGEX MATCHALL "([0-9]+)" RPTH_VERSION_LIST ${RPTH_VERSION_STRING_LINE})
      list(GET RPTH_VERSION_LIST 0 RPTH_VERSION_MAJOR_GUESS)
      list(GET RPTH_VERSION_LIST 1 RPTH_VERSION_MINOR_GUESS)
      list(GET RPTH_VERSION_LIST 2 RPTH_VERSION_PATCH_GUESS)
    endif(NOT ${RPTH_VERSION_STRING_LINE_TEST} STREQUAL "NONE")
  endif()
else (RPTH_INCLUDES AND RPTH_LIBRARIES)
  if (NOT RPTH_FIND_QUIETLY)
    if (NOT RPTH_INCLUDES)
      message (STATUS "Unable to find RPTH header files!")
    endif (NOT RPTH_INCLUDES)
    if (NOT RPTH_LIBRARIES)
      message (STATUS "Unable to find RPTH library files!")
    endif (NOT RPTH_LIBRARIES)
  endif (NOT RPTH_FIND_QUIETLY)
endif (RPTH_INCLUDES AND RPTH_LIBRARIES)

if (HAVE_RPTH)
  if (NOT RPTH_FIND_QUIETLY)
    message (STATUS "Found components for RPTH")
    message (STATUS "RPTH_INCLUDES = ${RPTH_INCLUDES}")
    message (STATUS "RPTH_LIBRARIES = ${RPTH_LIBRARIES}")
    message (STATUS "RPTH_VERSION_STRING_LINE = ${RPTH_VERSION_STRING_LINE}")
    message (STATUS "RPTH_VERSION_MAJOR_GUESS = ${RPTH_VERSION_MAJOR_GUESS}")
    message (STATUS "RPTH_VERSION_MINOR_GUESS = ${RPTH_VERSION_MINOR_GUESS}")
    message (STATUS "RPTH_VERSION_PATCH_GUESS = ${RPTH_VERSION_PATCH_GUESS}")
  endif (NOT RPTH_FIND_QUIETLY)
else (HAVE_RPTH)
  if (RPTH_FIND_REQUIRED)
    message (FATAL_ERROR "Could not find RPTH!")
  endif (RPTH_FIND_REQUIRED)
endif (HAVE_RPTH)

mark_as_advanced (
  HAVE_RPTH
  RPTH_LIBRARIES
  RPTH_INCLUDES
  )
