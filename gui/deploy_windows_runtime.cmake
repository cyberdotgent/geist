# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0

# Run after windeployqt, before the executable is tested or packaged.
file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES "${GEIST_EXECUTABLE}"
  DIRECTORIES "${GEIST_LIBRARY_DIR}" "${GEIST_QT_BIN}"
  RESOLVED_DEPENDENCIES_VAR resolved
  UNRESOLVED_DEPENDENCIES_VAR unresolved
  PRE_EXCLUDE_REGEXES "^[Aa][Pp][Ii]-" "^[Ee][Xx][Tt]-"
  POST_EXCLUDE_REGEXES ".*[/\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\].*"
)
if(unresolved)
  message(FATAL_ERROR "Reader runtime DLLs could not be resolved: ${unresolved}")
endif()

get_filename_component(destination "${GEIST_EXECUTABLE}" DIRECTORY)
foreach(dependency IN LISTS resolved)
  get_filename_component(source_dir "${dependency}" DIRECTORY)
  if(NOT source_dir STREQUAL destination)
    file(COPY "${dependency}" DESTINATION "${destination}")
    message(STATUS "Deployed reader runtime: ${dependency}")
  endif()
endforeach()
