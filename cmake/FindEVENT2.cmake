# - Check for the presence of EVENT2
#
# The following variables are set when EVENT2 is found:
#  HAVE_EVENT2       = Set to true, if all components of EVENT2
#                          have been found.
#  EVENT2_INCLUDES   = Include path for the header files of EVENT2
#  EVENT2_LIBRARIES  = Link these to use EVENT2

## -----------------------------------------------------------------------------
## Check for the header files, prirotize user inputs

find_path (EVENT2_INCLUDES event2/event.h
  PATHS ${CMAKE_EXTRA_INCLUDES} NO_DEFAULT_PATH
  )
if(NOT EVENT2_INCLUDES)
    find_path (EVENT2_INCLUDES event2/event.h
      PATHS /usr/local/include /usr/include ${CMAKE_EXTRA_INCLUDES}
      )
endif(NOT EVENT2_INCLUDES)

## -----------------------------------------------------------------------------
## Check for the library, prirotize user inputs

find_library (EVENT2_CORE_LIBRARIES NAMES event-2.0 event
  PATHS ${CMAKE_EXTRA_LIBRARIES} NO_DEFAULT_PATH
  )
if(NOT EVENT2_CORE_LIBRARIES)
    find_library (EVENT2_CORE_LIBRARIES NAMES event-2.0 event
      PATHS /usr/local/lib /usr/lib /lib ${CMAKE_EXTRA_LIBRARIES}
      )
endif(NOT EVENT2_CORE_LIBRARIES)

find_library (EVENT2_THREAD_LIBRARIES NAMES event_pthreads
  PATHS ${CMAKE_EXTRA_LIBRARIES} NO_DEFAULT_PATH
  )
if(NOT EVENT2_THREAD_LIBRARIES)
    find_library (EVENT2_THREAD_LIBRARIES NAMES event_pthreads
      PATHS /usr/local/lib /usr/lib /lib ${CMAKE_EXTRA_LIBRARIES}
      )
endif(NOT EVENT2_THREAD_LIBRARIES)

MARK_AS_ADVANCED(EVENT2_CORE_LIBRARIES EVENT2_THREAD_LIBRARIES)
SET(EVENT2_LIBRARIES ${EVENT2_CORE_LIBRARIES} ${EVENT2_THREAD_LIBRARIES})

## -----------------------------------------------------------------------------
## Actions taken when all components have been found

if (EVENT2_INCLUDES AND EVENT2_LIBRARIES)
  set (HAVE_EVENT2 TRUE)
else (EVENT2_INCLUDES AND EVENT2_LIBRARIES)
  if (NOT EVENT2_FIND_QUIETLY)
    if (NOT EVENT2_INCLUDES)
      message (STATUS "Unable to find EVENT2 header files!")
    endif (NOT EVENT2_INCLUDES)
    if (NOT EVENT2_LIBRARIES)
      message (STATUS "Unable to find EVENT2 library files!")
    endif (NOT EVENT2_LIBRARIES)
  endif (NOT EVENT2_FIND_QUIETLY)
endif (EVENT2_INCLUDES AND EVENT2_LIBRARIES)

if (HAVE_EVENT2)
  if (NOT EVENT2_FIND_QUIETLY)
    message (STATUS "Found components for EVENT2")
    message (STATUS "EVENT2_INCLUDES = ${EVENT2_INCLUDES}")
    message (STATUS "EVENT2_LIBRARIES     = ${EVENT2_LIBRARIES}")
  endif (NOT EVENT2_FIND_QUIETLY)
else (HAVE_EVENT2)
  if (EVENT2_FIND_REQUIRED)
    message (FATAL_ERROR "Could not find EVENT2!")
  endif (EVENT2_FIND_REQUIRED)
endif (HAVE_EVENT2)

mark_as_advanced (
  HAVE_EVENT2
  EVENT2_LIBRARIES
  EVENT2_INCLUDES
  )