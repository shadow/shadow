# - Check for the presence of OPENSSL
#
# The following variables are set when OPENSSL is found:
#  HAVE_OPENSSL       = Set to true, if all components of OPENSSL
#                          have been found.
#  OPENSSL_INCLUDES   = Include path for the header files of OPENSSL
#  OPENSSL_LIBRARIES  = Link these to use OPENSSL

## -----------------------------------------------------------------------------
## Check for the header files, prirotize user inputs

find_path (OPENSSL_INCLUDES ssl.h
  PATHS ${CMAKE_EXTRA_INCLUDES} NO_DEFAULT_PATH
  PATH_SUFFIXES openssl
  )
if(NOT OPENSSL_INCLUDES)
    find_path (OPENSSL_INCLUDES openssl/ssl.h
      PATHS /usr/local/include /usr/include ${CMAKE_EXTRA_INCLUDES}
      )
endif(NOT OPENSSL_INCLUDES)

## -----------------------------------------------------------------------------
## Check for the library, prirotize user inputs

find_library (OPENSSL_SSL_LIBRARIES NAMES ssl ssleay32 ssleay32MD
  PATHS ${CMAKE_EXTRA_LIBRARIES} NO_DEFAULT_PATH
  )
if(NOT OPENSSL_SSL_LIBRARIES)
    find_library (OPENSSL_SSL_LIBRARIES NAMES ssl ssleay32 ssleay32MD
      PATHS /usr/local/lib /usr/lib /lib ${CMAKE_EXTRA_LIBRARIES}
      )
endif(NOT OPENSSL_SSL_LIBRARIES)

find_library (OPENSSL_CRYPTO_LIBRARIES NAMES crypto
  PATHS ${CMAKE_EXTRA_LIBRARIES} NO_DEFAULT_PATH
  )
if(NOT OPENSSL_CRYPTO_LIBRARIES)
    find_library (OPENSSL_CRYPTO_LIBRARIES NAMES crypto
      PATHS /usr/local/lib /usr/lib /lib ${CMAKE_EXTRA_LIBRARIES}
      )
endif(NOT OPENSSL_CRYPTO_LIBRARIES)

MARK_AS_ADVANCED(OPENSSL_CRYPTO_LIBRARIES OPENSSL_SSL_LIBRARIES)
SET(OPENSSL_LIBRARIES ${OPENSSL_SSL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARIES})

## -----------------------------------------------------------------------------
## Actions taken when all components have been found

if (OPENSSL_INCLUDES AND OPENSSL_LIBRARIES)
  set (HAVE_OPENSSL TRUE)
else (OPENSSL_INCLUDES AND OPENSSL_LIBRARIES)
  if (NOT OPENSSL_FIND_QUIETLY)
    if (NOT OPENSSL_INCLUDES)
      message (STATUS "Unable to find OPENSSL header files!")
    endif (NOT OPENSSL_INCLUDES)
    if (NOT OPENSSL_LIBRARIES)
      message (STATUS "Unable to find OPENSSL library files!")
    endif (NOT OPENSSL_LIBRARIES)
  endif (NOT OPENSSL_FIND_QUIETLY)
endif (OPENSSL_INCLUDES AND OPENSSL_LIBRARIES)

if (HAVE_OPENSSL)
  if (NOT OPENSSL_FIND_QUIETLY)
    message (STATUS "Found components for OPENSSL")
    message (STATUS "OPENSSL_INCLUDES = ${OPENSSL_INCLUDES}")
    message (STATUS "OPENSSL_LIBRARIES     = ${OPENSSL_LIBRARIES}")
  endif (NOT OPENSSL_FIND_QUIETLY)
else (HAVE_OPENSSL)
  if (OPENSSL_FIND_REQUIRED)
    message (FATAL_ERROR "Could not find OPENSSL!")
  endif (OPENSSL_FIND_REQUIRED)
endif (HAVE_OPENSSL)

mark_as_advanced (
  HAVE_OPENSSL
  OPENSSL_LIBRARIES
  OPENSSL_INCLUDES
  )