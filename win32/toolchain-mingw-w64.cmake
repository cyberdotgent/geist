# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0

# Cross-compile for 64-bit Windows with MinGW-w64 from Linux. Set
# GEIST_MINGW_HOST in the environment for another triplet prefix.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
if(DEFINED ENV{GEIST_MINGW_HOST})
  set(_geist_mingw "$ENV{GEIST_MINGW_HOST}")
else()
  set(_geist_mingw x86_64-w64-mingw32)
endif()
set(CMAKE_C_COMPILER ${_geist_mingw}-gcc)
set(CMAKE_CXX_COMPILER ${_geist_mingw}-g++)
set(CMAKE_RC_COMPILER ${_geist_mingw}-windres)
# Dependency prefixes (CMAKE_PREFIX_PATH) are search roots as well, so the
# static libraries built by build_mingw_dependencies.sh are found and the
# build host's own libpng never is.
set(CMAKE_FIND_ROOT_PATH /usr/${_geist_mingw} ${CMAKE_PREFIX_PATH})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
