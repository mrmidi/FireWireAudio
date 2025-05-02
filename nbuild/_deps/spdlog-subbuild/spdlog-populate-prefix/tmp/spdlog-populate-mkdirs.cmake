# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/mrmidi/DEV/firewire-audio-daemon/nbuild/_deps/spdlog-src")
  file(MAKE_DIRECTORY "/Users/mrmidi/DEV/firewire-audio-daemon/nbuild/_deps/spdlog-src")
endif()
file(MAKE_DIRECTORY
  "/Users/mrmidi/DEV/firewire-audio-daemon/nbuild/_deps/spdlog-build"
  "/Users/mrmidi/DEV/firewire-audio-daemon/nbuild/_deps/spdlog-subbuild/spdlog-populate-prefix"
  "/Users/mrmidi/DEV/firewire-audio-daemon/nbuild/_deps/spdlog-subbuild/spdlog-populate-prefix/tmp"
  "/Users/mrmidi/DEV/firewire-audio-daemon/nbuild/_deps/spdlog-subbuild/spdlog-populate-prefix/src/spdlog-populate-stamp"
  "/Users/mrmidi/DEV/firewire-audio-daemon/nbuild/_deps/spdlog-subbuild/spdlog-populate-prefix/src"
  "/Users/mrmidi/DEV/firewire-audio-daemon/nbuild/_deps/spdlog-subbuild/spdlog-populate-prefix/src/spdlog-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/mrmidi/DEV/firewire-audio-daemon/nbuild/_deps/spdlog-subbuild/spdlog-populate-prefix/src/spdlog-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/mrmidi/DEV/firewire-audio-daemon/nbuild/_deps/spdlog-subbuild/spdlog-populate-prefix/src/spdlog-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
