# - Check for the presence of M
#
# The following variables are set when M is found:
#  HAVE_M       = Set to true, if all components of M
#                          have been found.
#  M_INCLUDES   = Include path for the header files of M
#  M_LIBRARIES  = Link these to use M

## -----------------------------------------------------------------------------
## Check for the header files

find_path (M_INCLUDES math.h
  PATHS /usr/local/include /usr/include ${CMAKE_EXTRA_INCLUDES}
  )

## -----------------------------------------------------------------------------
## Check for the library

find_library (M_LIBRARIES m
  PATHS /usr/local/lib /usr/lib /lib ${CMAKE_EXTRA_LIBRARIES}
  )

## -----------------------------------------------------------------------------
## Actions taken when all components have been found

if (M_INCLUDES AND M_LIBRARIES)
  set (HAVE_M TRUE)
else (M_INCLUDES AND M_LIBRARIES)
  if (NOT M_FIND_QUIETLY)
    if (NOT M_INCLUDES)
      message (STATUS "Unable to find M header files!")
    endif (NOT M_INCLUDES)
    if (NOT M_LIBRARIES)
      message (STATUS "Unable to find M library files!")
    endif (NOT M_LIBRARIES)
  endif (NOT M_FIND_QUIETLY)
endif (M_INCLUDES AND M_LIBRARIES)

if (HAVE_M)
  if (NOT M_FIND_QUIETLY)
    message (STATUS "Found components for M")
    message (STATUS "M_INCLUDES = ${M_INCLUDES}")
    message (STATUS "M_LIBRARIES = ${M_LIBRARIES}")
  endif (NOT M_FIND_QUIETLY)
else (HAVE_M)
  if (M_FIND_REQUIRED)
    message (FATAL_ERROR "Could not find M!")
  endif (M_FIND_REQUIRED)
endif (HAVE_M)

mark_as_advanced (
  HAVE_M
  M_LIBRARIES
  M_INCLUDES
  )