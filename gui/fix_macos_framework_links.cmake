# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0

# macdeployqt leaves some third-party libraries in Qt frameworks linked via
# @executable_path. That resolves differently in the nested WebEngine helper.
# Resolve every bundled framework dependency from the binary loading it.
set(frameworks "${GEIST_BUNDLE}/Contents/Frameworks")
file(GLOB_RECURSE files LIST_DIRECTORIES FALSE "${frameworks}/*")
foreach(binary IN LISTS files)
  if(IS_SYMLINK "${binary}")
    continue()
  endif()
  execute_process(COMMAND otool -L "${binary}"
    RESULT_VARIABLE status OUTPUT_VARIABLE links ERROR_QUIET)
  if(NOT status EQUAL 0)
    continue()
  endif()
  string(REGEX MATCHALL "@executable_path/\\.\\./Frameworks/[^ \t()\n]+"
    old_links "${links}")
  get_filename_component(loader_dir "${binary}" DIRECTORY)
  foreach(old_link IN LISTS old_links)
    string(REPLACE "@executable_path/../Frameworks/" "" name "${old_link}")
    set(target "${frameworks}/${name}")
    if(NOT EXISTS "${target}")
      message(FATAL_ERROR "Bundled dependency is missing: ${target}")
    endif()
    file(RELATIVE_PATH relative_target "${loader_dir}" "${target}")
    execute_process(COMMAND install_name_tool -change "${old_link}"
      "@loader_path/${relative_target}" "${binary}"
      RESULT_VARIABLE status ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
      message(FATAL_ERROR "Cannot fix ${binary}: ${error}")
    endif()
  endforeach()
endforeach()
