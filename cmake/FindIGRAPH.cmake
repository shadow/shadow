# - Check for the presence of IGRAPH
#
# The following variables are set when IGRAPH is found:
#  HAVE_IGRAPH       = Set to true, if all components of IGRAPH
#                          have been found.
#  IGRAPH_INCLUDES   = Include path for the header files of IGRAPH
#  IGRAPH_LIBRARIES  = Link these to use IGRAPH

## -----------------------------------------------------------------------------
## Check for the header files

find_path (IGRAPH_INCLUDES igraph.h
  PATHS ${CMAKE_EXTRA_INCLUDES} PATH_SUFFIXES igraph/ igraph/include NO_DEFAULT_PATH
  )
if(NOT IGRAPH_INCLUDES)
    find_path (IGRAPH_INCLUDES igraph.h
      PATHS /usr/local/include /usr/include /include /sw/include /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu/ ${CMAKE_EXTRA_INCLUDES} PATH_SUFFIXES igraph/ igraph/include
      )
endif(NOT IGRAPH_INCLUDES)

## -----------------------------------------------------------------------------
## Check for the library

find_library (IGRAPH_LIBRARIES NAMES igraph
  PATHS ${CMAKE_EXTRA_LIBRARIES} PATH_SUFFIXES igraph/ NO_DEFAULT_PATH
  )
if(NOT IGRAPH_LIBRARIES)
    find_library (IGRAPH_LIBRARIES NAMES igraph
      PATHS /usr/local/lib /usr/lib /lib /sw/lib ${CMAKE_EXTRA_LIBRARIES} PATH_SUFFIXES igraph/
      )
endif(NOT IGRAPH_LIBRARIES)

## -----------------------------------------------------------------------------
## Actions taken when all components have been found

if (IGRAPH_INCLUDES AND IGRAPH_LIBRARIES)
  set (HAVE_IGRAPH TRUE)
  if(EXISTS "${IGRAPH_INCLUDES}/igraph_version.h")
    set(IGRAPH_VERSION_STRING_LINE_TEST "NONE")
    file(READ ${IGRAPH_INCLUDES}/igraph_version.h IGRAPH_VFILE)
    string(REGEX MATCH "\#define IGRAPH_VERSION \"[0-9]+\\.[0-9]+\\.[0-9]+\"" IGRAPH_VERSION_STRING_LINE_TEST ${IGRAPH_VFILE})
    if(NOT ${IGRAPH_VERSION_STRING_LINE_TEST} STREQUAL "NONE")
      set(IGRAPH_VERSION_STRING_LINE ${IGRAPH_VERSION_STRING_LINE_TEST})
      string(REGEX MATCHALL "([0-9]+)" IGRAPH_VERSION_LIST ${IGRAPH_VERSION_STRING_LINE})
      list(GET IGRAPH_VERSION_LIST 0 IGRAPH_VERSION_MAJOR_GUESS)
      list(GET IGRAPH_VERSION_LIST 1 IGRAPH_VERSION_MINOR_GUESS)
      list(GET IGRAPH_VERSION_LIST 2 IGRAPH_VERSION_PATCH_GUESS)
    endif(NOT ${IGRAPH_VERSION_STRING_LINE_TEST} STREQUAL "NONE")
  endif()
else (IGRAPH_INCLUDES AND IGRAPH_LIBRARIES)
  if (NOT IGRAPH_FIND_QUIETLY)
    if (NOT IGRAPH_INCLUDES)
      message (STATUS "Unable to find IGRAPH header files!")
    endif (NOT IGRAPH_INCLUDES)
    if (NOT IGRAPH_LIBRARIES)
      message (STATUS "Unable to find IGRAPH library files!")
    endif (NOT IGRAPH_LIBRARIES)
  endif (NOT IGRAPH_FIND_QUIETLY)
endif (IGRAPH_INCLUDES AND IGRAPH_LIBRARIES)

if (HAVE_IGRAPH)
  if (NOT IGRAPH_FIND_QUIETLY)
    message (STATUS "Found components for IGRAPH")
    message (STATUS "IGRAPH_INCLUDES = ${IGRAPH_INCLUDES}")
    message (STATUS "IGRAPH_LIBRARIES = ${IGRAPH_LIBRARIES}")
    message (STATUS "IGRAPH_VERSION_STRING_LINE = ${IGRAPH_VERSION_STRING_LINE}")
    message (STATUS "IGRAPH_VERSION_MAJOR_GUESS = ${IGRAPH_VERSION_MAJOR_GUESS}")
    message (STATUS "IGRAPH_VERSION_MINOR_GUESS = ${IGRAPH_VERSION_MINOR_GUESS}")
    message (STATUS "IGRAPH_VERSION_PATCH_GUESS = ${IGRAPH_VERSION_PATCH_GUESS}")
  endif (NOT IGRAPH_FIND_QUIETLY)
else (HAVE_IGRAPH)
  if (IGRAPH_FIND_REQUIRED)
    message (FATAL_ERROR "Could not find IGRAPH!")
  endif (IGRAPH_FIND_REQUIRED)
endif (HAVE_IGRAPH)

mark_as_advanced (
  HAVE_IGRAPH
  IGRAPH_LIBRARIES
  IGRAPH_INCLUDES
  )
