# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0

# Follow the executable's actual Mach-O imports. The static geist archive
# still imports libpng and giflib; copy those dylibs and any non-system
# transitive dylibs into the app, then make every import bundle-relative.
set(_frameworks "${GEIST_APP}/Contents/Frameworks")
file(MAKE_DIRECTORY "${_frameworks}")
set(_pending "${GEIST_EXECUTABLE}")
set(_visited)
while(_pending)
  list(POP_FRONT _pending _binary)
  if(_binary IN_LIST _visited)
    continue()
  endif()
  list(APPEND _visited "${_binary}")
  execute_process(COMMAND otool -L "${_binary}"
    OUTPUT_VARIABLE _imports RESULT_VARIABLE _result)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "otool failed for ${_binary}")
  endif()
  string(REGEX MATCHALL "\n[ \t]+[^ \t\n]+[ \t]+\\(" _lines "${_imports}")
  foreach(_line IN LISTS _lines)
    string(REGEX REPLACE "^[\n \t]+([^ \t\n]+)[ \t]+\\($" "\\1" _import "${_line}")
    if(_import MATCHES "^(/System/Library/|/usr/lib/|@executable_path/|@loader_path/)")
      continue()
    endif()
    if(NOT IS_ABSOLUTE "${_import}" OR NOT EXISTS "${_import}")
      message(FATAL_ERROR "Unresolved app dependency: ${_import} in ${_binary}")
    endif()
    get_filename_component(_name "${_import}" NAME)
    set(_copy "${_frameworks}/${_name}")
    file(COPY_FILE "${_import}" "${_copy}")
    execute_process(COMMAND codesign --remove-signature "${_copy}"
      OUTPUT_QUIET ERROR_QUIET)
    execute_process(COMMAND install_name_tool -id
      "@executable_path/../Frameworks/${_name}" "${_copy}"
      RESULT_VARIABLE _id_result)
    if(NOT _id_result EQUAL 0)
      message(FATAL_ERROR "Could not set install name on ${_copy}")
    endif()
    if(GEIST_ARCHITECTURES)
      execute_process(COMMAND lipo -archs "${_copy}"
        OUTPUT_VARIABLE _archs OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _lipo_result)
      if(NOT _lipo_result EQUAL 0)
        message(FATAL_ERROR "Could not inspect architectures of ${_copy}")
      endif()
      string(REPLACE " " ";" _arch_list "${_archs}")
      string(REPLACE "," ";" _required_archs "${GEIST_ARCHITECTURES}")
      foreach(_arch IN LISTS _required_archs)
        if(NOT _arch IN_LIST _arch_list)
          message(FATAL_ERROR "${_copy} lacks ${_arch}; use universal libpng/giflib libraries")
        endif()
      endforeach()
    endif()
    get_filename_component(_library_dir "${_import}" DIRECTORY)
    get_filename_component(_library_prefix "${_library_dir}" DIRECTORY)
    set(_licence "")
    foreach(_candidate IN ITEMS LICENSE COPYING LICENSE.txt)
      if(EXISTS "${_library_prefix}/${_candidate}")
        set(_licence "${_library_prefix}/${_candidate}")
        break()
      endif()
    endforeach()
    if(NOT _licence)
      message(FATAL_ERROR "No licence text found beside ${_import}")
    endif()
    file(MAKE_DIRECTORY "${GEIST_APP}/Contents/Resources/ThirdPartyLicenses")
    file(COPY_FILE "${_licence}"
      "${GEIST_APP}/Contents/Resources/ThirdPartyLicenses/${_name}.txt")
    execute_process(COMMAND install_name_tool -change "${_import}"
      "@executable_path/../Frameworks/${_name}" "${_binary}"
      RESULT_VARIABLE _change_result)
    if(NOT _change_result EQUAL 0)
      message(FATAL_ERROR "Could not rewrite ${_import} in ${_binary}")
    endif()
    list(APPEND _pending "${_copy}")
  endforeach()
endwhile()

# Re-sign modified Homebrew dylibs first, then seal the whole bundle. An
# ad-hoc identity keeps local builds launchable; distribution builds can pass
# their Apple Distribution identity at configure time.
file(GLOB _bundled_dylibs "${_frameworks}/*.dylib")
foreach(_dylib IN LISTS _bundled_dylibs)
  execute_process(COMMAND codesign --force --sign "${GEIST_SIGN_IDENTITY}"
    "${_dylib}" RESULT_VARIABLE _signed)
  if(NOT _signed EQUAL 0)
    message(FATAL_ERROR "Could not sign ${_dylib}")
  endif()
endforeach()
if(GEIST_PROFILE)
  file(COPY_FILE "${GEIST_PROFILE}"
    "${GEIST_APP}/Contents/embedded.provisionprofile")
endif()
set(_sign_args --force --sign "${GEIST_SIGN_IDENTITY}")
if(GEIST_SANDBOX)
  list(APPEND _sign_args --entitlements "${GEIST_ENTITLEMENTS}")
endif()
execute_process(COMMAND codesign ${_sign_args} "${GEIST_APP}"
  RESULT_VARIABLE _signed)
if(NOT _signed EQUAL 0)
  message(FATAL_ERROR "Could not sign ${GEIST_APP}")
endif()
