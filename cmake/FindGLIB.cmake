# - Check for the presence of GLIB
#
# The following variables are set when GLIB is found:
#  HAVE_GLIB       = Set to true, if all components of GLIB
#                          have been found.
#  GLIB_INCLUDES   = Include path for the header files of GLIB
#  GLIB_LIBRARIES  = Link these to use GLIB

## -----------------------------------------------------------------------------
## Check for the header files

find_path (GLIB_CORE_INCLUDES glib.h
  PATHS ${CMAKE_EXTRA_INCLUDES} PATH_SUFFIXES glib-2.0/ glib-2.0/include NO_DEFAULT_PATH
  )
if(NOT GLIB_CORE_INCLUDES)
    find_path (GLIB_CORE_INCLUDES glib.h
      PATHS /usr/local/include /usr/include /include /sw/include /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu/ ${CMAKE_EXTRA_INCLUDES} PATH_SUFFIXES glib-2.0/ glib-2.0/include
      )
endif(NOT GLIB_CORE_INCLUDES)

## glibconfig is actually under the lib/ directory, so also use LIB directories
find_path (GLIB_CONFIG_INCLUDES glibconfig.h
  PATHS ${CMAKE_EXTRA_INCLUDES} ${CMAKE_EXTRA_LIBRARIES} PATH_SUFFIXES glib-2.0/ glib-2.0/include NO_DEFAULT_PATH
  )
if(NOT GLIB_CONFIG_INCLUDES)
    find_path (GLIB_CONFIG_INCLUDES glibconfig.h
      PATHS /usr/local/include /usr/include /include /sw/include /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu/ /usr/lib/i386-linux-gnu ${CMAKE_EXTRA_INCLUDES} ${CMAKE_EXTRA_LIBRARIES} PATH_SUFFIXES glib-2.0 glib-2.0/include
      )
endif(NOT GLIB_CONFIG_INCLUDES)

## we need both include directories
if(GLIB_CORE_INCLUDES)
    if(GLIB_CONFIG_INCLUDES)
        SET(GLIB_INCLUDES ${GLIB_CORE_INCLUDES} ${GLIB_CONFIG_INCLUDES})
    endif(GLIB_CONFIG_INCLUDES)
endif(GLIB_CORE_INCLUDES)

## -----------------------------------------------------------------------------
## Check for the library

find_library (GLIB_CORE_LIBRARIES NAMES glib-2.0
  PATHS ${CMAKE_EXTRA_LIBRARIES} PATH_SUFFIXES glib-2.0/ NO_DEFAULT_PATH
  )
if(NOT GLIB_CORE_LIBRARIES)
    find_library (GLIB_CORE_LIBRARIES NAMES glib-2.0
      PATHS /usr/local/lib /usr/lib /lib /sw/lib ${CMAKE_EXTRA_LIBRARIES} PATH_SUFFIXES glib-2.0/
      )
endif(NOT GLIB_CORE_LIBRARIES)

find_library (GLIB_GTHREAD_LIBRARIES NAMES gthread-2.0
  PATHS ${CMAKE_EXTRA_LIBRARIES} PATH_SUFFIXES glib-2.0/ NO_DEFAULT_PATH
  )
if(NOT GLIB_GTHREAD_LIBRARIES)
    find_library (GLIB_GTHREAD_LIBRARIES NAMES gthread-2.0
      PATHS /usr/local/lib /usr/lib /lib /sw/lib ${CMAKE_EXTRA_LIBRARIES} PATH_SUFFIXES glib-2.0/
      )
endif(NOT GLIB_GTHREAD_LIBRARIES)

find_library (GLIB_GMODULE_LIBRARIES NAMES gmodule-2.0
  PATHS ${CMAKE_EXTRA_LIBRARIES} PATH_SUFFIXES glib-2.0/ NO_DEFAULT_PATH
  )
if(NOT GLIB_GMODULE_LIBRARIES)
    find_library (GLIB_GMODULE_LIBRARIES NAMES gmodule-2.0
      PATHS /usr/local/lib /usr/lib /lib /sw/lib ${CMAKE_EXTRA_LIBRARIES} PATH_SUFFIXES glib-2.0/
      )
endif(NOT GLIB_GMODULE_LIBRARIES)

MARK_AS_ADVANCED(GLIB_CORE_LIBRARIES GLIB_GTHREAD_LIBRARIES GLIB_GMODULE_LIBRARIES)
SET(GLIB_LIBRARIES ${GLIB_CORE_LIBRARIES} ${GLIB_GTHREAD_LIBRARIES} ${GLIB_GMODULE_LIBRARIES})

## -----------------------------------------------------------------------------
## Actions taken when all components have been found

if (GLIB_INCLUDES AND GLIB_CORE_LIBRARIES AND GLIB_GTHREAD_LIBRARIES AND GLIB_GMODULE_LIBRARIES)
  set (HAVE_GLIB TRUE)
else (GLIB_INCLUDES AND GLIB_CORE_LIBRARIES AND GLIB_GTHREAD_LIBRARIES AND GLIB_GMODULE_LIBRARIES)
  if (NOT GLIB_FIND_QUIETLY)
    if (NOT GLIB_INCLUDES)
      message (STATUS "Unable to find GLIB header files!")
    endif (NOT GLIB_INCLUDES)
    if (NOT GLIB_CORE_LIBRARIES)
      message (STATUS "Unable to find GLIB glib-2.0 library files!")
    endif (NOT GLIB_CORE_LIBRARIES)
    if (NOT GLIB_GTHREAD_LIBRARIES)
      message (STATUS "Unable to find GLIB gthread-2.0 library files!")
    endif (NOT GLIB_GTHREAD_LIBRARIES)
    if (NOT GLIB_GMODULE_LIBRARIES)
      message (STATUS "Unable to find GLIB gmodule-2.0 library files!")
    endif (NOT GLIB_GMODULE_LIBRARIES)
  endif (NOT GLIB_FIND_QUIETLY)
endif (GLIB_INCLUDES AND GLIB_CORE_LIBRARIES AND GLIB_GTHREAD_LIBRARIES AND GLIB_GMODULE_LIBRARIES)

if (HAVE_GLIB)
  if (NOT GLIB_FIND_QUIETLY)
    message (STATUS "Found components for GLIB")
    message (STATUS "GLIB_INCLUDES = ${GLIB_INCLUDES}")
    message (STATUS "GLIB_LIBRARIES = ${GLIB_LIBRARIES}")
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
