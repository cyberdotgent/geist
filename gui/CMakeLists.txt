# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0

# The Geist Hardcopy Reader: a Qt6 desktop reader for BOO books, named in
# tribute to IBM's BookManager SoftCopy Reader.
#
# The reader is a pure consumer of libgeist's public API. Qt appears only in
# this directory: the library itself never links or includes it.

# The published stylesheet is compiled into the reader so its rendering and
# the stylesheet shipped beside the spec can never drift apart.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../doc/html-styling.css" GEIST_READER_CSS)
configure_file(styling.hpp.in
  "${CMAKE_CURRENT_BINARY_DIR}/generated/styling.hpp" @ONLY
)

add_executable(geist-hardcopy
  main.cpp
  book_source.cpp
  book_source.hpp
  book_url.hpp
  reader_window.cpp
  reader_window.hpp
)

set_target_properties(geist-hardcopy PROPERTIES
  AUTOMOC ON
  # A GUI application: no console window on Windows, a bundle on macOS.
  WIN32_EXECUTABLE ON
  MACOSX_BUNDLE ON
)

target_compile_features(geist-hardcopy PRIVATE cxx_std_17)
target_compile_definitions(geist-hardcopy PRIVATE
  GEIST_READER_VERSION="${PROJECT_VERSION}"
)
target_include_directories(geist-hardcopy PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_link_libraries(geist-hardcopy PRIVATE
  geist::geist
  Qt6::Widgets
  Qt6::WebEngineWidgets
  Qt6::PrintSupport
)

if(MSVC)
  target_compile_options(geist-hardcopy PRIVATE /W4 /FS)
else()
  target_compile_options(geist-hardcopy PRIVATE -Wall -Wextra -pedantic)
endif()

# Qt WebEngine needs more beside the executable than the import libraries:
# the QtWebEngineProcess helper, its resource packs and locales. windeployqt
# is the only supported way to assemble that, so use it when it is present.
if(WIN32)
  get_target_property(_geist_qt_moc Qt6::moc IMPORTED_LOCATION)
  get_filename_component(_geist_qt_bin "${_geist_qt_moc}" DIRECTORY)
  find_program(GEIST_WINDEPLOYQT windeployqt HINTS "${_geist_qt_bin}")
  if(GEIST_WINDEPLOYQT)
    add_custom_command(TARGET geist-hardcopy POST_BUILD
      COMMAND "${GEIST_WINDEPLOYQT}" --no-translations --no-compiler-runtime
              "$<TARGET_FILE:geist-hardcopy>"
      COMMENT "Deploying Qt runtime beside geist-hardcopy"
    )
  else()
    message(WARNING
      "windeployqt not found; geist-hardcopy will not run from the build tree "
      "without Qt's bin directory on PATH."
    )
  endif()
  add_custom_command(TARGET geist-hardcopy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_RUNTIME_DLLS:geist-hardcopy> $<TARGET_FILE_DIR:geist-hardcopy>
    COMMAND_EXPAND_LISTS
  )
endif()

install(TARGETS geist-hardcopy
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  BUNDLE DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

# The desktop entry and icon a Linux desktop needs to show the reader in a
# menu, and that linuxdeploy reads to build the AppImage. Installed on every
# platform that uses freedesktop conventions; Windows and macOS carry their
# own metadata in the executable and the bundle.
if(UNIX AND NOT APPLE)
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/geist-hardcopy.desktop"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
  )
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/geist-hardcopy.svg"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
  )
endif()
