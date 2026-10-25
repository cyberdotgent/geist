# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0

# Geist BOO Reader for Windows: plain Win32 and the MSHTML WebBrowser
# control, linked statically so the executable needs only Windows itself.
# MinGW-w64 and MSVC both build it.

set(GEIST_W32_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/geist-boo-reader.manifest")
set(GEIST_W32_ICON "${CMAKE_CURRENT_SOURCE_DIR}/geist-boo-reader.ico")
set(GEIST_W32_CSS "${PROJECT_SOURCE_DIR}/doc/html-styling.css")
set(GEIST_W32_IE_CSS "${CMAKE_CURRENT_SOURCE_DIR}/ie11-compat.css")
# Licence notices, embedded in the executable and shown by Help > Licences:
# this project's LICENSE and the texts of the statically linked libpng, zlib
# and giflib. They are looked for in GEIST_WIN32_LICENSE_DIR (<name>.txt),
# in <prefix>/licenses of each CMAKE_PREFIX_PATH entry (as
# build_mingw_dependencies.sh lays them out) and in vcpkg's share/<port>/
# copyright files.
set(GEIST_WIN32_LICENSE_DIR "" CACHE PATH
  "Directory holding libpng.txt, zlib.txt and giflib.txt for the Win32 reader")
set(GEIST_W32_NOTICES "${CMAKE_CURRENT_BINARY_DIR}/notices.txt")
file(READ "${PROJECT_SOURCE_DIR}/LICENSE" _geist_license)
set(_geist_notices "Geist BOO Reader is licensed under the Apache License, Version 2.0.\n\n${_geist_license}")
set(_geist_missing_notices "")
foreach(_geist_component libpng zlib giflib)
  set(_geist_candidates "")
  if(GEIST_WIN32_LICENSE_DIR)
    list(APPEND _geist_candidates "${GEIST_WIN32_LICENSE_DIR}/${_geist_component}.txt")
  endif()
  foreach(_geist_prefix ${CMAKE_PREFIX_PATH})
    list(APPEND _geist_candidates "${_geist_prefix}/licenses/${_geist_component}.txt")
  endforeach()
  if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
    list(APPEND _geist_candidates
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/${_geist_component}/copyright")
  endif()
  set(_geist_found "")
  foreach(_geist_candidate ${_geist_candidates})
    if(NOT _geist_found AND EXISTS "${_geist_candidate}")
      set(_geist_found "${_geist_candidate}")
    endif()
  endforeach()
  if(_geist_found)
    file(READ "${_geist_found}" _geist_text)
    string(APPEND _geist_notices "\n\n=== ${_geist_component} ===\n\n${_geist_text}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_geist_found}")
  else()
    list(APPEND _geist_missing_notices ${_geist_component})
  endif()
endforeach()
if(_geist_missing_notices)
  message(WARNING "Win32 reader: no licence text found for ${_geist_missing_notices}; "
    "set GEIST_WIN32_LICENSE_DIR. A build for distribution must embed them.")
  string(APPEND _geist_notices "\n\nLicence texts missing from this build: ${_geist_missing_notices}\n")
endif()
file(WRITE "${GEIST_W32_NOTICES}" "${_geist_notices}")

configure_file(geist-boo-reader.rc.in "${CMAKE_CURRENT_BINARY_DIR}/geist-boo-reader.rc" @ONLY)

add_executable(geist-boo-reader WIN32
  main.cpp
  app.cpp app.hpp
  reader_window.cpp reader_window.hpp
  browser_host.cpp browser_host.hpp
  protocol.cpp protocol.hpp
  settings.cpp settings.hpp
  dialogs.cpp dialogs.hpp
  win32_util.cpp win32_util.hpp
  book_source.cpp book_source.hpp
  resource.h
  "${CMAKE_CURRENT_BINARY_DIR}/geist-boo-reader.rc"
)
target_include_directories(geist-boo-reader PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/geist-boo-reader.rc" PROPERTIES
  OBJECT_DEPENDS "${GEIST_W32_MANIFEST};${GEIST_W32_ICON};${GEIST_W32_CSS};${GEIST_W32_IE_CSS};${CMAKE_CURRENT_SOURCE_DIR}/resource.h")
target_compile_features(geist-boo-reader PRIVATE cxx_std_17)
# Windows 7 is the floor; newer APIs (per-monitor DPI) are looked up at run
# time.
target_compile_definitions(geist-boo-reader PRIVATE
  UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0601 WINVER=0x0601 _WIN32_IE=0x0A00
  GEIST_READER_VERSION="${PROJECT_VERSION}"
  GEIST_READER_REVISION="${GEIST_GIT_REVISION}")
target_link_libraries(geist-boo-reader PRIVATE geist::geist_static
  ole32 oleaut32 uuid comctl32 comdlg32 shell32 shlwapi urlmon uxtheme gdi32 user32 advapi32)

if(MSVC)
  target_compile_options(geist-boo-reader PRIVATE /W4)
  # The manifest is a resource (geist-boo-reader.rc); a second, linker-made
  # one would collide with it.
  target_link_options(geist-boo-reader PRIVATE /MANIFEST:NO)
else()
  target_compile_options(geist-boo-reader PRIVATE -Wall -Wextra)
  # The C++ runtime, libgcc and winpthreads go into the executable; release
  # builds are stripped.
  target_link_options(geist-boo-reader PRIVATE -static -static-libgcc -static-libstdc++
    $<$<CONFIG:Release,MinSizeRel>:-s>)
endif()

install(TARGETS geist-boo-reader RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
