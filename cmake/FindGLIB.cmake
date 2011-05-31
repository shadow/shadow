# - Check for the presence of GLIB
#
# The following variables are set when GLIB is found:
#  HAVE_GLIB       = Set to true, if all components of GLIB
#                          have been found.
#  GLIB_INCLUDES   = Include path for the header files of GLIB
#  GLIB_LIBRARIES  = Link these to use GLIB

## -----------------------------------------------------------------------------
## Check for the header files

find_path (GLIB_INCLUDES glib.h
  PATHS /usr/local/include /usr/include /sw/include /usr/include/glib-2.0
  #PATH_SUFFIXES <optional path extension>
  )

## -----------------------------------------------------------------------------
## Check for the library

find_library (GLIB_LIBRARIES glib-2.0
  PATHS /usr/local/lib /usr/lib /lib /sw/lib
  )

## -----------------------------------------------------------------------------
## Actions taken when all components have been found

include_directories(/usr/include/glib-2.0 /usr/lib/glib-2.0/include /usr/lib64/glib-2.0/include/)

if (GLIB_INCLUDES AND GLIB_LIBRARIES)
  set (HAVE_GLIB TRUE)
else (GLIB_INCLUDES AND GLIB_LIBRARIES)
  if (NOT GLIB_FIND_QUIETLY)
    if (NOT GLIB_INCLUDES)
      message (STATUS "Unable to find GLIB header files!")
    endif (NOT GLIB_INCLUDES)
    if (NOT GLIB_LIBRARIES)
      message (STATUS "Unable to find GLIB library files!")
    endif (NOT GLIB_LIBRARIES)
  endif (NOT GLIB_FIND_QUIETLY)
endif (GLIB_INCLUDES AND GLIB_LIBRARIES)

if (HAVE_GLIB)
  if (NOT GLIB_FIND_QUIETLY)
    message (STATUS "Found components for GLIB")
    message (STATUS "GLIB_INCLUDES = ${GLIB_INCLUDES}")
    message (STATUS "GLIB_LIBRARIES     = ${GLIB_LIBRARIES}")
  endif (NOT GLIB_FIND_QUIETLY)
else (HAVE_GLIB)
  if (GLIB_FIND_REQUIRED)
    message (FATAL_ERROR "Could not find GLIB!")
  endif (GLIB_FIND_REQUIRED)
endif (HAVE_GLIB)

mark_as_advanced (
  HAVE_GLIB
  GLIB_LIBRARIES
  GLIB_INCLUDES
  )
