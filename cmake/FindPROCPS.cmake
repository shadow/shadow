# - Check for the presence of PROCPS
#
# The following variables are set when PROCPS is found:
#  HAVE_PROCPS       = Set to true, if all components of PROCPS
#                          have been found.
#  PROCPS_INCLUDES   = Include path for the header files of PROCPS
#  PROCPS_LIBRARIES  = Link these to use PROCPS

## -----------------------------------------------------------------------------
## Check for the header files

find_path (PROCPS_INCLUDES procps.h
  PATHS /usr/local/include /usr/include /sw/include
  PATH_SUFFIXES proc/
  )

## -----------------------------------------------------------------------------
## Check for the library

find_library (PROCPS_LIBRARIES NAMES procps
    PATHS ${CMAKE_EXTRA_LIBRARIES}
  )

## -----------------------------------------------------------------------------
## Actions taken when all components have been found

if (PROCPS_INCLUDES AND PROCPS_LIBRARIES)
  set (HAVE_PROCPS TRUE)
else (PROCPS_INCLUDES AND PROCPS_LIBRARIES)
  if (NOT PROCPS_FIND_QUIETLY)
    if (NOT PROCPS_INCLUDES)
      message (STATUS "Unable to find PROCPS header files!")
    endif (NOT PROCPS_INCLUDES)
    if (NOT PROCPS_LIBRARIES)
      message (STATUS "Unable to find PROCPS library files!")
    endif (NOT PROCPS_LIBRARIES)
  endif (NOT PROCPS_FIND_QUIETLY)
endif (PROCPS_INCLUDES AND PROCPS_LIBRARIES)

if (HAVE_PROCPS)
  if (NOT PROCPS_FIND_QUIETLY)
    message (STATUS "Found components for PROCPS")
    message (STATUS "PROCPS_INCLUDES = ${PROCPS_INCLUDES}")
    message (STATUS "PROCPS_LIBRARIES     = ${PROCPS_LIBRARIES}")
  endif (NOT PROCPS_FIND_QUIETLY)
else (HAVE_PROCPS)
  if (PROCPS_FIND_REQUIRED)
    message (FATAL_ERROR "Could not find PROCPS!")
  endif (PROCPS_FIND_REQUIRED)
endif (HAVE_PROCPS)

mark_as_advanced (
  HAVE_PROCPS
  PROCPS_LIBRARIES
  PROCPS_INCLUDES
  )
