# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0

file(MAKE_DIRECTORY "${GEIST_ICONSET_DIR}")
file(READ "${GEIST_SOURCE_ICON}" _svg)
string(REPLACE "width=\"256\" height=\"256\""
  "width=\"1024\" height=\"1024\"" _svg "${_svg}")
set(_scaled_svg "${GEIST_ICONSET_DIR}/source.svg")
file(WRITE "${_scaled_svg}" "${_svg}")
execute_process(COMMAND sips -s format png "${_scaled_svg}"
  --out "${GEIST_ICONSET_DIR}/icon_512x512@2x.png"
  OUTPUT_QUIET RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
  message(FATAL_ERROR "Could not render the Geist icon")
endif()
foreach(_size IN ITEMS 16 32 128 256 512)
  math(EXPR _double "${_size} * 2")
  foreach(_pixels IN ITEMS ${_size} ${_double})
    if(_pixels EQUAL 1024)
      continue()
    endif()
    if(_pixels EQUAL _size)
      set(_name "icon_${_size}x${_size}.png")
    else()
      set(_name "icon_${_size}x${_size}@2x.png")
    endif()
    execute_process(COMMAND sips -Z "${_pixels}"
      "${GEIST_ICONSET_DIR}/icon_512x512@2x.png"
      --out "${GEIST_ICONSET_DIR}/${_name}"
      OUTPUT_QUIET RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
      message(FATAL_ERROR "Could not size the Geist icon to ${_pixels}")
    endif()
  endforeach()
endforeach()
execute_process(COMMAND iconutil -c icns "${GEIST_ICONSET_DIR}"
  -o "${GEIST_OUTPUT_ICON}" RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
  message(FATAL_ERROR "Could not assemble the Geist icon")
endif()
