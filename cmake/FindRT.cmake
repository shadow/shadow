# - Check for the presence of RT
#
# The following variables are set when RT is found:
#  HAVE_RT       = Set to true, if all components of RT
#                          have been found.
#  RT_INCLUDES   = Include path for the header files of RT
#  RT_LIBRARIES  = Link these to use RT

## -----------------------------------------------------------------------------
## Check for the header files

find_path (RT_INCLUDES time.h
  PATHS /usr/local/include /usr/include ${CMAKE_EXTRA_INCLUDES}
  )

## -----------------------------------------------------------------------------
## Check for the library

find_library (RT_LIBRARIES rt
  PATHS /usr/local/lib /usr/lib /lib ${CMAKE_EXTRA_LIBRARIES}
  )

## -----------------------------------------------------------------------------
## Actions taken when all components have been found

if (RT_INCLUDES AND RT_LIBRARIES)
  set (HAVE_RT TRUE)
else (RT_INCLUDES AND RT_LIBRARIES)
  if (NOT RT_FIND_QUIETLY)
    if (NOT RT_INCLUDES)
      message (STATUS "Unable to find RT header files!")
    endif (NOT RT_INCLUDES)
    if (NOT RT_LIBRARIES)
      message (STATUS "Unable to find RT library files!")
    endif (NOT RT_LIBRARIES)
  endif (NOT RT_FIND_QUIETLY)
endif (RT_INCLUDES AND RT_LIBRARIES)

if (HAVE_RT)
  if (NOT RT_FIND_QUIETLY)
    message (STATUS "Found components for RT")
    message (STATUS "RT_INCLUDES = ${RT_INCLUDES}")
    message (STATUS "RT_LIBRARIES = ${RT_LIBRARIES}")
  endif (NOT RT_FIND_QUIETLY)
else (HAVE_RT)
  if (RT_FIND_REQUIRED)
    message (FATAL_ERROR "Could not find RT!")
  endif (RT_FIND_REQUIRED)
endif (HAVE_RT)

mark_as_advanced (
  HAVE_RT
  RT_LIBRARIES
  RT_INCLUDES
  )