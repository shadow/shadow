# - Check for the presence of libtidy
#
# The following variables are set when TIDY is found:
#  HAVE_TIDY       = Set to true, if all components of TIDY
#                          have been found.
#  TIDY_INCLUDES   = Include path for the header files of TIDY
#  TIDY_LIBRARIES  = Link these to use TIDY

## -----------------------------------------------------------------------------
## Check for the header files

find_path (TIDY_INCLUDES tidy/tidy.h
  PATHS /usr/local/include /usr/include /sw/include
  PATH_SUFFIXES <optional path extension>
  )

## -----------------------------------------------------------------------------
## Check for the library

find_library (TIDY_LIBRARIES tidy
  PATHS /usr/local/lib /usr/lib /lib /sw/lib
  )

## -----------------------------------------------------------------------------
## Actions taken when all components have been found

if (TIDY_INCLUDES AND TIDY_LIBRARIES)
  set (HAVE_TIDY TRUE)
else (TIDY_INCLUDES AND TIDY_LIBRARIES)
  if (NOT TIDY_FIND_QUIETLY)
    if (NOT TIDY_INCLUDES)
      message (STATUS "Unable to find TIDY header files!")
    endif (NOT TIDY_INCLUDES)
    if (NOT TIDY_LIBRARIES)
      message (STATUS "Unable to find TIDY library files!")
    endif (NOT TIDY_LIBRARIES)
  endif (NOT TIDY_FIND_QUIETLY)
endif (TIDY_INCLUDES AND TIDY_LIBRARIES)

if (HAVE_TIDY)
  if (NOT TIDY_FIND_QUIETLY)
    message (STATUS "Found components for TIDY")
    message (STATUS "TIDY_INCLUDES = ${TIDY_INCLUDES}")
    message (STATUS "TIDY_LIBRARIES     = ${TIDY_LIBRARIES}")
  endif (NOT TIDY_FIND_QUIETLY)
else (HAVE_TIDY)
  if (TIDY_FIND_REQUIRED)
    message (FATAL_ERROR "Could not find TIDY!")
  endif (TIDY_FIND_REQUIRED)
endif (HAVE_TIDY)

mark_as_advanced (
  HAVE_TIDY
  TIDY_LIBRARIES
  TIDY_INCLUDES
  )