## CPack generator-specific CPack configuration
## http://www.cmake.org/Wiki/CMake:CPackPackageGenerators

if(CPACK_GENERATOR MATCHES "RPM")
  ## rpm package release (NOT the source release)
  set(CPACK_RPM_PACKAGE_RELEASE 1)
  set(CPACK_RPM_PACKAGE_GROUP "Applications/Emulators")
  set(CPACK_RPM_PACKAGE_REQUIRES "requires: cmake >= 2.8, python >= 2.7, flex, bison, libevent >= 2.0.10, openssl, zlib, rt, dl, m")
endif(CPACK_GENERATOR MATCHES "RPM")

if(CPACK_GENERATOR MATCHES "DEB")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Rob G. Jansen")
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "cmake >= 2.8, python >= 2.7, flex, bison, libevent >= 2.0.10, openssl, zlib1g-dev")
endif(CPACK_GENERATOR MATCHES "DEB")
