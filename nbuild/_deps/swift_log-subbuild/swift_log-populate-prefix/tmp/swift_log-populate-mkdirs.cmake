# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/mrmidi/DEV/firewire-audio-daemon/nbuild/_deps/swift_log-src")
  file(MAKE_DIRECTORY "/Users/mrmidi/DEV/firewire-audio-daemon/nbuild/_deps/swift_log-src")
endif()
file(MAKE_DIRECTORY
  "/Users/mrmidi/DEV/firewire-audio-daemon/nbuild/_deps/swift_log-build"
  "/Users/mrmidi/DEV/firewire-audio-daemon/nbuild/_deps/swift_log-subbuild/swift_log-populate-prefix"
  "/Users/mrmidi/DEV/firewire-audio-daemon/nbuild/_deps/swift_log-subbuild/swift_log-populate-prefix/tmp"
  "/Users/mrmidi/DEV/firewire-audio-daemon/nbuild/_deps/swift_log-subbuild/swift_log-populate-prefix/src/swift_log-populate-stamp"
  "/Users/mrmidi/DEV/firewire-audio-daemon/nbuild/_deps/swift_log-subbuild/swift_log-populate-prefix/src"
  "/Users/mrmidi/DEV/firewire-audio-daemon/nbuild/_deps/swift_log-subbuild/swift_log-populate-prefix/src/swift_log-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/mrmidi/DEV/firewire-audio-daemon/nbuild/_deps/swift_log-subbuild/swift_log-populate-prefix/src/swift_log-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/mrmidi/DEV/firewire-audio-daemon/nbuild/_deps/swift_log-subbuild/swift_log-populate-prefix/src/swift_log-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
