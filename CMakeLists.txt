# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.16)

if(WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
      CACHE STRING "vcpkg toolchain file"
    )
  else()
    file(GLOB _geist_vs_vcpkg_toolchains
      "$ENV{ProgramFiles}/Microsoft Visual Studio/*/*/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
    )
    if(_geist_vs_vcpkg_toolchains)
      list(GET _geist_vs_vcpkg_toolchains 0 _geist_vcpkg_toolchain)
      set(CMAKE_TOOLCHAIN_FILE "${_geist_vcpkg_toolchain}"
        CACHE STRING "vcpkg toolchain file"
      )
    else()
      message(FATAL_ERROR
        "Windows libgeist builds require vcpkg for libpng/giflib. Set "
        "VCPKG_ROOT or pass -DCMAKE_TOOLCHAIN_FILE=<vcpkg.cmake>."
      )
    endif()
  endif()
endif()

project(libgeist
  VERSION 0.1.0
  DESCRIPTION "Self-contained IBM BookManager BOO parser library"
  # CMake's built-in FindGIF module probes the giflib C headers.
  LANGUAGES C CXX
)

# Every source in this tree is UTF-8 without a BOM. Without this MSVC decodes
# them in the system ANSI code page, so a non-ASCII literal such as u8"Titlé"
# compiles to the wrong bytes and only fails at run time. Set here rather than
# per target so tests, examples and the GUI get it too.
if(MSVC)
  add_compile_options(/utf-8)
endif()

# The build's provenance, for anything that reports its own version. A
# development build must be distinguishable from a release: forgetting to
# reinstall after a rebuild is easy, and a module that only says "0.1.0"
# cannot tell you which 0.1.0 is loaded.
#
# `git describe --always --dirty` gives a tag-relative description
# (v0.1-11-gce60cd44), falls back to a bare short hash in a repository with
# no tags, and appends -dirty for uncommitted changes. It resolves at
# configure time, so re-run cmake to refresh it.
set(GEIST_GIT_REVISION "unknown")
find_package(Git QUIET)
if(Git_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" describe --always --dirty --abbrev=8
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE _geist_git_revision
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _geist_git_result
  )
  if(_geist_git_result EQUAL 0 AND _geist_git_revision)
    set(GEIST_GIT_REVISION "${_geist_git_revision}")
  endif()
endif()
message(STATUS "Build revision: ${GEIST_GIT_REVISION}")

include(GNUInstallDirs)

find_package(PNG REQUIRED)
find_package(GIF REQUIRED)

# CMake's module finders can pair a library from one installation with headers
# from another (for example Homebrew libraries with Mono framework headers on
# macOS). Prefer headers in the prefix that owns the selected library.
function(geist_match_dependency_headers target header)
  get_target_property(_geist_library ${target} IMPORTED_LOCATION)
  if(NOT _geist_library)
    get_target_property(_geist_library ${target} IMPORTED_LOCATION_RELEASE)
  endif()
  if(NOT _geist_library)
    return()
  endif()

  get_filename_component(_geist_library_dir "${_geist_library}" DIRECTORY)
  get_filename_component(_geist_prefix "${_geist_library_dir}" DIRECTORY)
  find_path(_geist_include_dir
    NAMES "${header}"
    HINTS "${_geist_prefix}"
    PATH_SUFFIXES include
    NO_DEFAULT_PATH
  )
  if(_geist_include_dir)
    set_property(TARGET ${target} PROPERTY
      INTERFACE_INCLUDE_DIRECTORIES "${_geist_include_dir}"
    )
  endif()
endfunction()

geist_match_dependency_headers(PNG::PNG png.h)
geist_match_dependency_headers(GIF::GIF gif_lib.h)

set(GEIST_SOURCES
  # core/
  src/core/ebcdic.cpp
  src/core/io.cpp
  src/core/properties.cpp
  src/core/resources.cpp
  src/core/strings.cpp
  src/core/util.cpp

  # container/
  src/container/control_ir.cpp
  src/container/decoded_record_scan.cpp
  src/container/logical.cpp
  src/container/page_runs.cpp
  src/container/source_rows.cpp
  src/container/source_segments.cpp
  src/container/source_slice_text.cpp
  src/container/toc.cpp
  src/container/toc_entry_framing.cpp

  # layout/
  src/layout/display_lines.cpp
  src/layout/fixed_display.cpp
  src/layout/font_span_ir.cpp
  src/layout/implicit_grid.cpp
  src/layout/layout_ir.cpp
  src/layout/ownership_ir.cpp

  # img/
  src/img/gdf.cpp
  src/img/gdf_raster.cpp
  src/img/mmr.cpp
  src/img/mmr_tables.cpp
  src/img/png.cpp

  # ir/
  src/ir/book_ir.cpp
  src/ir/book_topic_catalog_ir.cpp
  src/ir/comment_delivery_ir.cpp
  src/ir/figure_block_ir.cpp
  src/ir/fixed_prose_ir.cpp
  src/ir/fixed_prose_topic_ir.cpp
  src/ir/fixed_table_block_ir.cpp
  src/ir/generated_list_topic_ir.cpp
  src/ir/generated_toc_index_ir.cpp
  src/ir/glossary_catalog_ir.cpp
  src/ir/glossary_ir.cpp
  src/ir/menu_ir.cpp
  src/ir/menu_topic_ir.cpp
  src/ir/message_ir.cpp
  src/ir/message_prose_rows.cpp
  src/ir/message_section_blocks_ir.cpp
  src/ir/message_topic_ir.cpp
  src/ir/procedure_rows.cpp
  src/ir/publication_ir.cpp
  src/ir/selector_display_ir.cpp
  src/ir/selector_ir.cpp
  src/ir/selector_link_ir.cpp
  src/ir/topic_header_title.cpp
  src/ir/trap_catalog_ir.cpp

  # ir/prose/
  src/ir/prose/prose_topic_blocks.cpp
  src/ir/prose/prose_topic_boxes.cpp
  src/ir/prose/prose_topic_cz.cpp
  src/ir/prose/prose_topic_ir.cpp
  src/ir/prose/prose_topic_lines.cpp
  src/ir/prose/prose_topic_spans.cpp
  src/ir/prose/prose_topic_stream.cpp

  # lowering/
  src/lowering/comment_delivery_document_lowering.cpp
  src/lowering/document_ir.cpp
  src/lowering/drawn_word_conservation.cpp
  src/lowering/figure_document_lowering.cpp
  src/lowering/fixed_prose_document_lowering.cpp
  src/lowering/fixed_table_document_lowering.cpp
  src/lowering/generated_list_document_lowering.cpp
  src/lowering/generated_toc_index_document_lowering.cpp
  src/lowering/glossary_document_lowering.cpp
  src/lowering/menu_document_lowering.cpp
  src/lowering/message_document_lowering.cpp
  src/lowering/prose_topic_document_lowering.cpp
  src/lowering/publication_document_lowering.cpp
  src/lowering/topic_document_lowering.cpp
  src/lowering/topic_identity.cpp
  src/lowering/trap_catalog_document_lowering.cpp
  src/lowering/typed_route_inventory.cpp
  src/lowering/verbatim_cross_references.cpp

  # render/
  src/render/document_html_renderer.cpp
  src/render/document_markdown_renderer.cpp
  src/render/render_diagnostic.cpp
  src/render/render_trace.cpp
  src/render/topic_html_render.cpp
  src/render/topic_render.cpp

  # api/
  src/api/document.cpp
  src/api/probe.cpp
  src/container/prologue.cpp
  src/api/topic_link_targets.cpp
)

add_library(geist SHARED ${GEIST_SOURCES})
add_library(geist::geist ALIAS geist)

# A versioned SONAME, so the runtime library can be packaged: a distribution
# names the package after the SONAME (libgeist0) and installs
# libgeist.so.0.1.0 with a libgeist.so.0 symlink, leaving the bare
# libgeist.so to the -dev package. Until the first tagged release the ABI
# moves freely, so this is 0 and stays 0 until something is promised.
set_target_properties(geist PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

add_library(geist_static STATIC ${GEIST_SOURCES})
add_library(geist::geist_static ALIAS geist_static)

set_target_properties(geist_static PROPERTIES OUTPUT_NAME geist_static)

foreach(target geist geist_static)
  target_compile_features(${target} PUBLIC cxx_std_17)

  target_include_directories(${target}
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )

  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /FS)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -pedantic)
  endif()
endforeach()

target_compile_definitions(geist PRIVATE GEIST_BUILDING_DLL)
target_compile_definitions(geist_static PUBLIC GEIST_STATIC)
target_link_libraries(geist PRIVATE PNG::PNG)
target_link_libraries(geist PRIVATE GIF::GIF)
target_link_libraries(geist_static PUBLIC PNG::PNG GIF::GIF)

foreach(example booinfo bootoc boorsrc boorender bootrace boo2git)
  add_executable(${example}
    examples/${example}.cpp
  )
  if(example STREQUAL "bootrace")
    target_link_libraries(${example} PRIVATE geist::geist_static)
  else()
    target_link_libraries(${example} PRIVATE geist::geist)
  endif()
  if(WIN32 AND NOT example STREQUAL "bootrace")
    add_custom_command(TARGET ${example} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:${example}>
        $<TARGET_FILE_DIR:${example}>
      COMMAND_EXPAND_LISTS
    )
  endif()
endforeach()

# The Geist Hardcopy Reader, an optional Qt6 GUI. AUTO (the default) builds it
# when Qt6 Widgets, WebEngineWidgets and PrintSupport are all present and skips
# it with an explanation otherwise; ON turns a missing Qt into a hard error,
# which is what CI should use; OFF never builds it. Qt is never a dependency of
# the library itself.
set(GEIST_BUILD_GUI "AUTO" CACHE STRING
  "Build the Geist Hardcopy Reader: AUTO, ON or OFF"
)
set_property(CACHE GEIST_BUILD_GUI PROPERTY STRINGS AUTO ON OFF)

if(NOT GEIST_BUILD_GUI STREQUAL "OFF")
  set(_geist_qt_components Widgets WebEngineWidgets PrintSupport)
  if(GEIST_BUILD_GUI STREQUAL "ON")
    find_package(Qt6 REQUIRED COMPONENTS ${_geist_qt_components})
  else()
    find_package(Qt6 QUIET COMPONENTS ${_geist_qt_components})
  endif()

  set(_geist_qt_ready TRUE)
  foreach(_geist_qt_component ${_geist_qt_components})
    if(NOT TARGET Qt6::${_geist_qt_component})
      set(_geist_qt_ready FALSE)
    endif()
  endforeach()

  if(_geist_qt_ready)
    add_subdirectory(gui)
  else()
    message(STATUS
      "Qt6 (Widgets, WebEngineWidgets, PrintSupport) not found - skipping the "
      "Geist Hardcopy Reader.\n"
      "    Linux:   sudo apt install qt6-base-dev qt6-webengine-dev\n"
      "    macOS:   brew install qt\n"
      "    Windows: pip install aqtinstall && aqt install-qt windows desktop "
      "6.8.1 win64_msvc2022_64 -m qtwebengine qtwebchannel qtpositioning\n"
      "             then re-run cmake with -DCMAKE_PREFIX_PATH=<qt>/6.8.1/msvc2022_64\n"
      "    Use -DGEIST_BUILD_GUI=ON to make this a hard error instead."
    )
  endif()
endif()

# Installation -------------------------------------------------------------
#
# Standard GNU locations: libraries in lib, headers in include/geist, the
# command-line tools in bin. The Apache module is installed where httpd keeps
# its modules, which is outside this prefix by nature; see apache/.

install(TARGETS geist geist_static
  EXPORT geistTargets
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

# Only the public surface. The detail/ headers are implementation and are
# never included by a public header.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/geist/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/geist"
  FILES_MATCHING
    PATTERN "*.hpp"
    PATTERN "detail" EXCLUDE
)

install(TARGETS booinfo bootoc boorsrc boorender bootrace boo2git
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
  "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
  DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)

# So a consumer can find_package(geist) and link geist::geist.
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/geistConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/geistConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/geistConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/geist"
)
install(EXPORT geistTargets
  NAMESPACE geist::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/geist"
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/geistConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/geistConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/geist"
)

# mod_geist, an optional Apache 2.4 module. AUTO builds it when apxs and the
# APR headers are present and skips it otherwise; ON makes a missing httpd
# toolchain a hard error; OFF never builds it. Like the GUI, httpd is never a
# dependency of the library itself.
set(GEIST_BUILD_APACHE "AUTO" CACHE STRING
  "Build the Apache module: AUTO, ON or OFF"
)
set_property(CACHE GEIST_BUILD_APACHE PROPERTY STRINGS AUTO ON OFF)

if(NOT GEIST_BUILD_APACHE STREQUAL "OFF" AND NOT WIN32)
  find_program(GEIST_APXS NAMES apxs apxs2)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(GEIST_APR QUIET apr-1 apr-util-1)
  endif()

  if(GEIST_APXS AND GEIST_APR_FOUND)
    execute_process(COMMAND "${GEIST_APXS}" -q INCLUDEDIR
      OUTPUT_VARIABLE GEIST_APACHE_INCLUDEDIR OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND "${GEIST_APXS}" -q LIBEXECDIR
      OUTPUT_VARIABLE GEIST_APXS_LIBEXECDIR OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND "${GEIST_APXS}" -q SYSCONFDIR
      OUTPUT_VARIABLE GEIST_APXS_SYSCONFDIR OUTPUT_STRIP_TRAILING_WHITESPACE)

    # httpd owns these paths, so they sit outside CMAKE_INSTALL_PREFIX by
    # nature -- exactly as apxs itself installs. Both honour DESTDIR, and
    # either can be overridden for packaging.
    set(GEIST_APACHE_LIBEXECDIR "${GEIST_APXS_LIBEXECDIR}" CACHE PATH
      "Directory httpd loads modules from")
    set(GEIST_APACHE_SYSCONFDIR "${GEIST_APXS_SYSCONFDIR}" CACHE PATH
      "httpd configuration directory")
    set(GEIST_APACHE_INCLUDE_DIRS
      "${GEIST_APACHE_INCLUDEDIR}" ${GEIST_APR_INCLUDE_DIRS}
    )
    set(GEIST_APACHE_LIBRARIES ${GEIST_APR_LIBRARIES})
    # The module is a shared object, so everything linked into it must be
    # position independent -- including the static library.
    set_property(TARGET geist_static PROPERTY POSITION_INDEPENDENT_CODE ON)
    add_subdirectory(apache)
  elseif(GEIST_BUILD_APACHE STREQUAL "ON")
    message(FATAL_ERROR
      "Apache module requested but the httpd toolchain was not found "
      "(apxs: ${GEIST_APXS}, APR: ${GEIST_APR_FOUND})."
    )
  else()
    message(STATUS
      "apxs or the APR headers not found - skipping mod_geist.\n"
      "    Debian/Ubuntu: sudo apt install apache2-dev libapr1-dev libaprutil1-dev\n"
      "    Fedora/RHEL:   sudo dnf install httpd-devel apr-devel apr-util-devel\n"
      "    Use -DGEIST_BUILD_APACHE=ON to make this a hard error instead."
    )
  endif()
endif()

# The BOO format specification. Optional: it defines a documentation target
# only when an AsciiDoc toolchain is installed, and is never part of `all`.
add_subdirectory(doc/boo-spec)

enable_testing()

# Only `packet.boo` may be redistributed, so it is the only BOO fixture in the
# tree and it lives inside the published library (issue #59). Tests that need a
# real container resolve it through GEIST_FIXTURE_DIR; a published `libgeist/`
# therefore configures, builds and tests with no parent directory present.
set(GEIST_FIXTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

# Registers a test executable. Pass FIXTURE to give it GEIST_FIXTURE_DIR, and
# THREADS for a test that starts threads of its own.
function(geist_add_test name)
  cmake_parse_arguments(GEIST_TEST "FIXTURE;THREADS" "" "" ${ARGN})
  add_executable(${name}_test tests/${name}.cpp)
  target_link_libraries(${name}_test PRIVATE geist::geist_static)
  if(GEIST_TEST_THREADS)
    find_package(Threads REQUIRED)
    target_link_libraries(${name}_test PRIVATE Threads::Threads)
  endif()
  if(GEIST_TEST_FIXTURE)
    target_compile_definitions(${name}_test PRIVATE
      GEIST_FIXTURE_DIR="${GEIST_FIXTURE_DIR}"
    )
  endif()
  add_test(NAME ${name}_test COMMAND ${name}_test)
endfunction()

# Purely synthetic tests: they build their own records and touch no BOO file.
foreach(synthetic_test
    gdf_synthetic
    logical_synthetic
    display_line_control_extent_synthetic
    toc_entry_framing_synthetic
    topic_envelope_ir_synthetic
    source_rows_synthetic
    layout_ir_synthetic
    ownership_ir_synthetic
    publication_document_lowering_synthetic
    document_ir_synthetic
    document_markdown_renderer_synthetic
    document_html_renderer_synthetic
    procedure_rows_synthetic
    fixed_display_synthetic
    implicit_grid
    inline_picture_ir_synthetic
    generated_index_term_synthetic
    prose_index_term_line_synthetic
    span_row_geometry_synthetic
    cz_artwork_region_synthetic
    cz_degraded_region_synthetic
    font_span_trace_synthetic
    cz_unopened_region_closer_synthetic
    menu_wrapped_title_synthetic
    glossary_catalog_ir_synthetic
    message_catalog_ir_synthetic
    verbatim_cross_references_synthetic
    trap_catalog_ir_synthetic
    layout_row_coverage_synthetic)
  geist_add_test(${synthetic_test})
endforeach()

# The HTML renderer's class scheme is documented, not just implemented: the
# test checks the shipped example stylesheet against what the renderer emits,
# in both directions, so a class cannot ship undocumented and the stylesheet
# cannot name one that does not exist.
target_compile_definitions(document_html_renderer_synthetic_test PRIVATE
  GEIST_DOC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/doc"
)

# Tests that read the packet.boo fixture.
foreach(fixture_test
    packet_markdown
    packet_html
    packet_cross_references
    lazy_open_packet
    logical_source_index
    topic_link_targets
    topic_document_lowering
    trace_source_reader
    figure_block_ir_synthetic
    fixed_table_block_ir_synthetic
    prose_topic_ir_synthetic
    generated_list_topic_ir_synthetic
    generated_toc_index_ir_synthetic
    generated_list_production
    glossary_production
    selector_display_ir_synthetic
    drawn_word_conservation_synthetic
    render_diagnostic
    probe_matches_open)
  geist_add_test(${fixture_test} FIXTURE)
endforeach()

# Concurrent `const` access to one opened document, and the determinism the
# lock-free cache publication rests on (issue #49). Build it with
# `-fsanitize=thread` for the ThreadSanitizer pass:
#   cmake -S libgeist -B build_tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
#     -DCMAKE_CXX_FLAGS=-fsanitize=thread \
#     -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
geist_add_test(concurrent_const_access FIXTURE THREADS)

# `boo2git` exporter acceptance: link resolution and typed menu/generated-list
# export, driven end to end through the example binary.
add_test(NAME boo2git_menu_links_test
  COMMAND ${CMAKE_COMMAND}
    -DBOO2GIT=$<TARGET_FILE:boo2git>
    -DBOOK=${GEIST_FIXTURE_DIR}/packet.boo
    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/boo2git-menu-links
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/boo2git_menu_links.cmake
)

add_test(NAME boo2git_generated_list_links_test
  COMMAND ${CMAKE_COMMAND}
    -DBOO2GIT=$<TARGET_FILE:boo2git>
    -DBOOK=${GEIST_FIXTURE_DIR}/packet.boo
    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/boo2git-generated-list-output
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/boo2git_generated_list_links.cmake
)

add_executable(geist_open_benchmark
  tests/open_benchmark.cpp
)
target_link_libraries(geist_open_benchmark PRIVATE geist::geist_static)

# Whole-book exports and multi-second production renders. They remain part of
# the full gate; iterate with `ctest -LE slow` and run `ctest -L slow -j4`
# (they contend for memory bandwidth, not CPU) before committing.
set_tests_properties(
  logical_source_index_test
  generated_list_topic_ir_synthetic_test
  selector_display_ir_synthetic_test
  lazy_open_packet_test
  boo2git_generated_list_links_test
  PROPERTIES LABELS slow)
