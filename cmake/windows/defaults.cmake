# CMake Windows defaults module

include_guard(GLOBAL)

# Enable find_package targets to become globally available targets
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL TRUE)

include(buildspec)

set(
  CMAKE_INSTALL_PREFIX
  "$ENV{ALLUSERSPROFILE}/obs-studio/plugins"
  CACHE STRING
  "Default plugin installation directory"
  FORCE
)

file(TO_CMAKE_PATH "${CMAKE_INSTALL_PREFIX}" CMAKE_INSTALL_PREFIX)
set(
  CMAKE_INSTALL_PREFIX
  "${CMAKE_INSTALL_PREFIX}"
  CACHE PATH
  "Default plugin installation directory"
  FORCE
)
