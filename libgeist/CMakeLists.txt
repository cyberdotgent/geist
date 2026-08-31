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
  src/book_topic_catalog_ir.cpp
  src/book_ir.cpp
  src/comment_delivery_ir.cpp
  src/comment_delivery_document_lowering.cpp
  src/control_ir.cpp
  src/display_lines.cpp
  src/document.cpp
  src/document_ir.cpp
  src/document_markdown_renderer.cpp
  src/ebcdic.cpp
  src/fixed_display.cpp
  src/fixed_prose_document_lowering.cpp
  src/fixed_table_block_ir.cpp
  src/fixed_table_document_lowering.cpp
  src/fixed_prose_ir.cpp
  src/fixed_prose_topic_ir.cpp
  src/font_span_ir.cpp
  src/generated_list_document_lowering.cpp
  src/generated_list_topic_ir.cpp
  src/generated_toc_index_document_lowering.cpp
  src/generated_toc_index_ir.cpp
  src/glossary_catalog_ir.cpp
  src/glossary_document_lowering.cpp
  src/glossary_ir.cpp
  src/implicit_grid.cpp
  src/img/gdf.cpp
  src/img/gdf_raster.cpp
  src/io.cpp
  src/layout_ir.cpp
  src/logical.cpp
  src/decoded_record_scan.cpp
  src/message_ir.cpp
  src/message_prose_rows.cpp
  src/message_section_blocks_ir.cpp
  src/figure_block_ir.cpp
  src/figure_document_lowering.cpp
  src/message_document_lowering.cpp
  src/message_topic_ir.cpp
  src/trap_catalog_ir.cpp
  src/trap_catalog_document_lowering.cpp
  src/menu_document_lowering.cpp
  src/menu_ir.cpp
  src/menu_topic_ir.cpp
  src/topic_render.cpp
  src/ownership_ir.cpp
  src/img/mmr.cpp
  src/img/mmr_tables.cpp
  src/page_runs.cpp
  src/img/png.cpp
  src/properties.cpp
  src/render_diagnostic.cpp
  src/procedure_rows.cpp
  src/prose_topic_blocks.cpp
  src/prose_topic_boxes.cpp
  src/prose_topic_cz.cpp
  src/prose_topic_document_lowering.cpp
  src/prose_topic_ir.cpp
  src/prose_topic_lines.cpp
  src/prose_topic_spans.cpp
  src/prose_topic_stream.cpp
  src/publication_document_lowering.cpp
  src/publication_ir.cpp
  src/render_trace.cpp
  src/resources.cpp
  src/selector_display_ir.cpp
  src/selector_ir.cpp
  src/selector_link_ir.cpp
  src/source_rows.cpp
  src/source_slice_text.cpp
  src/source_segments.cpp
  src/strings.cpp
  src/toc.cpp
  src/topic_document_lowering.cpp
  src/topic_link_targets.cpp
  src/topic_header_title.cpp
  src/topic_identity.cpp
  src/typed_route_inventory.cpp
  src/util.cpp
  src/verbatim_cross_references.cpp
)

add_library(geist SHARED ${GEIST_SOURCES})
add_library(geist::geist ALIAS geist)

add_library(geist_static STATIC ${GEIST_SOURCES})
add_library(geist::geist_static ALIAS geist_static)

set_target_properties(geist_static PROPERTIES OUTPUT_NAME geist_static)

foreach(target geist geist_static)
  target_compile_features(${target} PUBLIC cxx_std_17)

  target_include_directories(${target}
    PUBLIC
      "${CMAKE_CURRENT_SOURCE_DIR}/src"
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

enable_testing()

# Only `packet.boo` may be redistributed, so it is the only BOO fixture in the
# tree and it lives inside the published library (issue #59). Tests that need a
# real container resolve it through GEIST_FIXTURE_DIR; a published `libgeist/`
# therefore configures, builds and tests with no parent directory present.
set(GEIST_FIXTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

# Registers a test executable. Pass FIXTURE to give it GEIST_FIXTURE_DIR.
function(geist_add_test name)
  cmake_parse_arguments(GEIST_TEST "FIXTURE" "" "" ${ARGN})
  add_executable(${name}_test tests/${name}.cpp)
  target_link_libraries(${name}_test PRIVATE geist::geist_static)
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
    source_rows_synthetic
    layout_ir_synthetic
    ownership_ir_synthetic
    publication_document_lowering_synthetic
    document_ir_synthetic
    document_markdown_renderer_synthetic
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
    glossary_catalog_ir_synthetic
    message_catalog_ir_synthetic
    verbatim_cross_references_synthetic
    trap_catalog_ir_synthetic)
  geist_add_test(${synthetic_test})
endforeach()

# Tests that read the packet.boo fixture.
foreach(fixture_test
    packet_markdown
    packet_cross_references
    lazy_open_packet
    logical_source_index
    topic_link_targets
    topic_document_lowering
    figure_block_ir_synthetic
    fixed_table_block_ir_synthetic
    prose_topic_ir_synthetic
    generated_list_topic_ir_synthetic
    generated_toc_index_ir_synthetic
    generated_list_production
    glossary_production
    selector_display_ir_synthetic
    render_diagnostic)
  geist_add_test(${fixture_test} FIXTURE)
endforeach()

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
