# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/cowmuncher/Projects/LAPSE/lapse/build/_deps/libfvad-src")
  file(MAKE_DIRECTORY "/home/cowmuncher/Projects/LAPSE/lapse/build/_deps/libfvad-src")
endif()
file(MAKE_DIRECTORY
  "/home/cowmuncher/Projects/LAPSE/lapse/build/_deps/libfvad-build"
  "/home/cowmuncher/Projects/LAPSE/lapse/build/_deps/libfvad-subbuild/libfvad-populate-prefix"
  "/home/cowmuncher/Projects/LAPSE/lapse/build/_deps/libfvad-subbuild/libfvad-populate-prefix/tmp"
  "/home/cowmuncher/Projects/LAPSE/lapse/build/_deps/libfvad-subbuild/libfvad-populate-prefix/src/libfvad-populate-stamp"
  "/home/cowmuncher/Projects/LAPSE/lapse/build/_deps/libfvad-subbuild/libfvad-populate-prefix/src"
  "/home/cowmuncher/Projects/LAPSE/lapse/build/_deps/libfvad-subbuild/libfvad-populate-prefix/src/libfvad-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/cowmuncher/Projects/LAPSE/lapse/build/_deps/libfvad-subbuild/libfvad-populate-prefix/src/libfvad-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/cowmuncher/Projects/LAPSE/lapse/build/_deps/libfvad-subbuild/libfvad-populate-prefix/src/libfvad-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
