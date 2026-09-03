# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0
#
# Every tool, and the reader, answer --version with the same version as the
# library they are built from.
#
# They report it separately -- the tool's own is compiled in, libgeist's is
# asked for at run time -- so that a tool running against a libgeist it was
# not built against says so. That is only useful if a tool built against the
# right one agrees with it, which is what this checks: one release, one
# version, said the same way by all of them.

set(expected "${GEIST_EXPECTED_VERSION}")
foreach(tool ${GEIST_TOOLS})
  if(NOT EXISTS "${tool}")
    message(FATAL_ERROR "tool was not built: ${tool}")
  endif()
  execute_process(COMMAND "${tool}" --version
    OUTPUT_VARIABLE reported
    RESULT_VARIABLE status
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "${tool} --version exited ${status}")
  endif()
  get_filename_component(name "${tool}" NAME)
  # `<tool>/<version>[ (<revision>)] libgeist/<version>[ (<revision>)]`
  if(NOT reported MATCHES "^([^/ ]+)/([0-9]+\\.[0-9]+\\.[0-9]+)( \\([^)]*\\))? libgeist/([0-9]+\\.[0-9]+\\.[0-9]+)( \\([^)]*\\))?$")
    message(FATAL_ERROR "${name} --version is not in the agreed shape: ${reported}")
  endif()
  set(tool_version "${CMAKE_MATCH_2}")
  set(library_version "${CMAKE_MATCH_4}")
  if(NOT tool_version STREQUAL expected)
    message(FATAL_ERROR
      "${name} reports version ${tool_version}, the project is ${expected}")
  endif()
  if(NOT library_version STREQUAL expected)
    message(FATAL_ERROR
      "${name} reports libgeist ${library_version}, the project is ${expected}")
  endif()
  message(STATUS "${reported}")
endforeach()
