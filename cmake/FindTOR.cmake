# - Check for the presence of TOR
#
# The following variables are set when TOR is found:
#  HAVE_TOR       = Set to true, if all components of TOR
#                          have been found.
#  TOR_INCLUDES   = Include path for the header files of TOR
#  TOR_LIBRARIES  = Link these to use TOR

## -----------------------------------------------------------------------------
## Check for the header files
find_path (TOR_OR_INCLUDES or.h
  PATHS /usr/local/include /usr/include ${CMAKE_EXTRA_INCLUDES}
  )
  
find_path (TOR_COMMON_INCLUDES util.h
  PATHS /usr/local/include /usr/include ${CMAKE_EXTRA_INCLUDES}
  )
  
mark_as_advanced(${TOR_OR_INCLUDES} ${TOR_COMMON_INCLUDES})
set(TOR_INCLUDES ${TOR_OR_INCLUDES} ${TOR_COMMON_INCLUDES})

## -----------------------------------------------------------------------------
## Check for the libraries
set(FIND_TOR_PATHS "/usr/local/lib /usr/lib /lib ${CMAKE_EXTRA_LIBRARIES}")

find_library (TOR_TOR_LIBRARIES NAMES libtor.a tor PATHS ${FIND_TOR_PATHS})
find_library (TOR_OR_LIBRARIES NAMES libor.a or PATHS ${FIND_TOR_PATHS})
find_library (TOR_OR_CRYPTO_LIBRARIES NAMES libor-crypto.a or-crypto PATHS ${FIND_TOR_PATHS})
find_library (TOR_OR_EVENT_LIBRARIES NAMES libor-event.a or-event PATHS ${FIND_TOR_PATHS})
find_file (TOR_VAR_DEFINITIONS var_definitions.o PATHS ${FIND_TOR_PATHS})

mark_as_advanced(${TOR_TOR_LIBRARIES} ${TOR_OR_LIBRARIES} ${TOR_OR_CRYPTO_LIBRARIES} ${TOR_OR_EVENT_LIBRARIES} ${TOR_VAR_DEFINITIONS})
set(TOR_LIBRARIES ${TOR_TOR_LIBRARIES} ${TOR_OR_LIBRARIES} ${TOR_OR_CRYPTO_LIBRARIES} ${TOR_OR_EVENT_LIBRARIES} ${TOR_VAR_DEFINITIONS})

## -----------------------------------------------------------------------------
## Actions taken when all components have been found

if (TOR_INCLUDES AND TOR_LIBRARIES)
  set (HAVE_TOR TRUE)
else (TOR_INCLUDES AND TOR_LIBRARIES)
  if (NOT TOR_FIND_QUIETLY)
    if (NOT TOR_INCLUDES)
      message (STATUS "Unable to find TOR header files!")
    endif (NOT TOR_INCLUDES)
    if (NOT TOR_LIBRARIES)
      message (STATUS "Unable to find TOR library files!")
    endif (NOT TOR_LIBRARIES)
  endif (NOT TOR_FIND_QUIETLY)
endif (TOR_INCLUDES AND TOR_LIBRARIES)

if (HAVE_TOR)
  if (NOT TOR_FIND_QUIETLY)
    message (STATUS "Found components for TOR")
    message (STATUS "TOR_INCLUDES = ${TOR_INCLUDES}")
    message (STATUS "TOR_LIBRARIES     = ${TOR_LIBRARIES}")
  endif (NOT TOR_FIND_QUIETLY)
else (HAVE_TOR)
  if (TOR_FIND_REQUIRED)
    message (FATAL_ERROR "Could not find TOR!")
  endif (TOR_FIND_REQUIRED)
endif (HAVE_TOR)

mark_as_advanced (
  HAVE_TOR
  TOR_LIBRARIES
  TOR_INCLUDES
  )