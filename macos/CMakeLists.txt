# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0

add_executable(geist-boo-reader MACOSX_BUNDLE
  main.mm
  BookSource.h BookSource.mm
  BookPrintJob.h BookPrintJob.mm
  ReaderWindow.h ReaderWindow.mm
  BookInformationWindow.h BookInformationWindow.mm
  PreferencesWindow.h PreferencesWindow.mm
  Info.plist.in
)

set(_geist_icon "${CMAKE_CURRENT_BINARY_DIR}/GeistBOOReader.icns")
add_custom_command(OUTPUT "${_geist_icon}"
  COMMAND ${CMAKE_COMMAND}
    "-DGEIST_SOURCE_ICON=${PROJECT_SOURCE_DIR}/gui/geist-hardcopy.svg"
    "-DGEIST_ICONSET_DIR=${CMAKE_CURRENT_BINARY_DIR}/GeistBOOReader.iconset"
    "-DGEIST_OUTPUT_ICON=${_geist_icon}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/create_icon.cmake"
  DEPENDS "${PROJECT_SOURCE_DIR}/gui/geist-hardcopy.svg"
          "${CMAKE_CURRENT_SOURCE_DIR}/create_icon.cmake"
  VERBATIM)
set_source_files_properties("${_geist_icon}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_sources(geist-boo-reader PRIVATE "${_geist_icon}")
set_target_properties(geist-boo-reader PROPERTIES
  OUTPUT_NAME "Geist BOO Reader"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
  MACOSX_BUNDLE_GUI_IDENTIFIER "gent.cyber.geist-viewer"
  MACOSX_BUNDLE_BUNDLE_NAME "Geist BOO Reader"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
)
target_compile_features(geist-boo-reader PRIVATE cxx_std_17)
target_compile_options(geist-boo-reader PRIVATE -fobjc-arc -Wall -Wextra)
target_compile_definitions(geist-boo-reader PRIVATE
  GEIST_READER_VERSION="${PROJECT_VERSION}"
  GEIST_READER_REVISION="${GEIST_GIT_REVISION}")
target_link_libraries(geist-boo-reader PRIVATE geist::geist_static
  "-framework AppKit" "-framework WebKit" "-framework PDFKit")

set_source_files_properties("${PROJECT_SOURCE_DIR}/doc/html-styling.css"
  "${PROJECT_SOURCE_DIR}/LICENSE" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_sources(geist-boo-reader PRIVATE
  "${PROJECT_SOURCE_DIR}/doc/html-styling.css" "${PROJECT_SOURCE_DIR}/LICENSE")

set(GEIST_MAC_SIGN_IDENTITY "-" CACHE STRING
  "Code signing identity for the native app; '-' signs locally")
option(GEIST_MAC_SANDBOX "Sign native reader with App Store sandbox entitlements" OFF)
set(GEIST_MAC_PROVISIONING_PROFILE "" CACHE FILEPATH
  "Provisioning profile to embed for signed distribution builds")
set_property(TARGET geist-boo-reader APPEND PROPERTY LINK_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/AppStore.entitlements"
  "${CMAKE_CURRENT_SOURCE_DIR}/deploy_runtime.cmake")
string(REPLACE ";" "," _geist_architectures "${CMAKE_OSX_ARCHITECTURES}")

add_custom_command(TARGET geist-boo-reader POST_BUILD
  COMMAND ${CMAKE_COMMAND}
    "-DGEIST_APP=$<TARGET_BUNDLE_DIR:geist-boo-reader>"
    "-DGEIST_EXECUTABLE=$<TARGET_FILE:geist-boo-reader>"
    "-DGEIST_SIGN_IDENTITY=${GEIST_MAC_SIGN_IDENTITY}"
    "-DGEIST_SANDBOX=${GEIST_MAC_SANDBOX}"
    "-DGEIST_ENTITLEMENTS=${CMAKE_CURRENT_SOURCE_DIR}/AppStore.entitlements"
    "-DGEIST_PROFILE=${GEIST_MAC_PROVISIONING_PROFILE}"
    "-DGEIST_ARCHITECTURES=${_geist_architectures}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/deploy_runtime.cmake"
  COMMENT "Bundling libpng and giflib with Geist BOO Reader"
  VERBATIM)

install(TARGETS geist-boo-reader BUNDLE DESTINATION "${CMAKE_INSTALL_BINDIR}")
