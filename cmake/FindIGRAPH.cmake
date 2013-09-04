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
  PATHS /usr/local/include /usr/include /sw/include
  PATH_SUFFIXES igraph/
  )

## -----------------------------------------------------------------------------
## Check for the library

find_library (IGRAPH_LIBRARIES igraph
  PATHS /usr/local/lib /usr/lib /lib /sw/lib
  )

## -----------------------------------------------------------------------------
## Actions taken when all components have been found

if (IGRAPH_INCLUDES AND IGRAPH_LIBRARIES)
  set (HAVE_IGRAPH TRUE)
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
    message (STATUS "IGRAPH_LIBRARIES     = ${IGRAPH_LIBRARIES}")
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