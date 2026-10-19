# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0

set(contents "${GEIST_BUNDLE}/Contents")
set(frameworks "${contents}/Frameworks")
set(webengine "${frameworks}/QtWebEngineCore.framework/Versions/A")
set(binaries
  "${contents}/MacOS/geist-hardcopy"
  "${frameworks}/libgeist.0.dylib"
  "${frameworks}/libpng16.16.dylib"
  "${frameworks}/libgif.dylib"
  "${frameworks}/QtCore.framework/Versions/A/QtCore"
  "${webengine}/Helpers/QtWebEngineProcess.app/Contents/MacOS/QtWebEngineProcess"
)
foreach(binary IN LISTS binaries)
  if(NOT EXISTS "${binary}")
    message(FATAL_ERROR "macOS reader bundle is missing ${binary}")
  endif()
  execute_process(COMMAND otool -L "${binary}"
    RESULT_VARIABLE status OUTPUT_VARIABLE links ERROR_VARIABLE error)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "Cannot inspect ${binary}: ${error}")
  endif()
  string(REGEX MATCHALL "\n[ \t]+/[^ \t(\n]+" absolute_links "${links}")
  foreach(link IN LISTS absolute_links)
    string(STRIP "${link}" link)
    if(NOT link MATCHES "^/System/Library/|^/usr/lib/")
      message(FATAL_ERROR "${binary} still depends on ${link}")
    endif()
  endforeach()
  if(binary MATCHES "/QtCore\\.framework/" AND
     links MATCHES "@executable_path/\\.\\./Frameworks/libicu")
    message(FATAL_ERROR "QtCore libraries cannot load in the WebEngine helper")
  endif()
endforeach()

if(NOT EXISTS "${webengine}/Resources/qtwebengine_resources.pak")
  message(FATAL_ERROR "macOS reader bundle is missing WebEngine resources")
endif()
if(NOT EXISTS "${contents}/PlugIns/platforms/libqcocoa.dylib")
  message(FATAL_ERROR "macOS reader bundle is missing the Cocoa platform plugin")
endif()

execute_process(COMMAND codesign --verify --deep --strict "${GEIST_BUNDLE}"
  RESULT_VARIABLE status ERROR_VARIABLE error)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "macOS reader bundle has an invalid signature: ${error}")
endif()
