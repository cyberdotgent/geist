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
  src/document.cpp
  src/document_ir.cpp
  src/document_lowering.cpp
  src/document_markdown_renderer.cpp
  src/ebcdic.cpp
  src/fixed_display.cpp
  src/fixed_prose_document_lowering.cpp
  src/fixed_prose_ir.cpp
  src/fixed_prose_topic_ir.cpp
  src/font_span_ir.cpp
  src/generated_list_document_lowering.cpp
  src/generated_list_topic_ir.cpp
  src/glossary_catalog_ir.cpp
  src/glossary_document_lowering.cpp
  src/glossary_ir.cpp
  src/implicit_grid.cpp
  src/img/gdf.cpp
  src/img/gdf_raster.cpp
  src/io.cpp
  src/layout_ir.cpp
  src/logical.cpp
  src/markup.cpp
  src/message_ir.cpp
  src/message_prose_rows.cpp
  src/structural_marker_rows.cpp
  src/form_item_rows.cpp
  src/message_section_blocks_ir.cpp
  src/message_document_lowering.cpp
  src/message_topic_ir.cpp
  src/trap_catalog_ir.cpp
  src/trap_catalog_document_lowering.cpp
  src/menu_document_lowering.cpp
  src/menu_ir.cpp
  src/menu_topic_ir.cpp
  src/markdown.cpp
  src/ownership_ir.cpp
  src/img/mmr.cpp
  src/img/mmr_tables.cpp
  src/page_runs.cpp
  src/img/png.cpp
  src/properties.cpp
  src/procedure_rows.cpp
  src/publication_document_lowering.cpp
  src/publication_ir.cpp
  src/resources.cpp
  src/selector_display_ir.cpp
  src/selector_ir.cpp
  src/source_rows.cpp
  src/source_segments.cpp
  src/strings.cpp
  src/toc.cpp
  src/topic_document_lowering.cpp
  src/typed_route_inventory.cpp
  src/util.cpp
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

add_executable(gdf_synthetic_test
  tests/gdf_synthetic.cpp
)
target_link_libraries(gdf_synthetic_test PRIVATE geist::geist_static)

add_test(NAME gdf_synthetic_test COMMAND gdf_synthetic_test)

add_executable(markup_debug_test
  tests/markup_debug.cpp
)
target_link_libraries(markup_debug_test PRIVATE geist::geist_static)

add_test(NAME markup_debug_test COMMAND markup_debug_test)

add_executable(qs3x36cm_markdown_test
  tests/qs3x36cm_markdown.cpp
)
target_link_libraries(qs3x36cm_markdown_test PRIVATE geist::geist_static)
target_compile_definitions(qs3x36cm_markdown_test
  PRIVATE
    GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)

add_test(NAME qs3x36cm_markdown_test COMMAND qs3x36cm_markdown_test)

add_executable(packet_markdown_test
  tests/packet_markdown.cpp
)
target_link_libraries(packet_markdown_test PRIVATE geist::geist_static)
target_compile_definitions(packet_markdown_test
  PRIVATE
    GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)

add_test(NAME packet_markdown_test COMMAND packet_markdown_test)

add_executable(sc31_605_markdown_test
  tests/sc31_605_markdown.cpp
)
target_link_libraries(sc31_605_markdown_test PRIVATE geist::geist_static)
target_compile_definitions(sc31_605_markdown_test
  PRIVATE
    GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)

add_test(NAME sc31_605_markdown_test COMMAND sc31_605_markdown_test)

add_executable(qsysnewg_markdown_test
  tests/qsysnewg_markdown.cpp
)
target_link_libraries(qsysnewg_markdown_test PRIVATE geist::geist_static)
target_compile_definitions(qsysnewg_markdown_test
  PRIVATE
    GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)

add_test(NAME qsysnewg_markdown_test COMMAND qsysnewg_markdown_test)

add_executable(mmr_qsysnewg_test
  tests/mmr_qsysnewg.cpp
)
target_link_libraries(mmr_qsysnewg_test PRIVATE geist::geist_static)
target_compile_definitions(mmr_qsysnewg_test
  PRIVATE
    GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)

add_test(NAME mmr_qsysnewg_test COMMAND mmr_qsysnewg_test)

add_executable(markup_synthetic_test
  tests/markup_synthetic.cpp
)
target_link_libraries(markup_synthetic_test PRIVATE geist::geist_static)

add_test(NAME markup_synthetic_test COMMAND markup_synthetic_test)

add_executable(logical_synthetic_test
  tests/logical_synthetic.cpp
)
target_link_libraries(logical_synthetic_test PRIVATE geist::geist_static)

add_test(NAME logical_synthetic_test COMMAND logical_synthetic_test)

add_executable(logical_source_index_test
  tests/logical_source_index.cpp
)
target_link_libraries(logical_source_index_test PRIVATE geist::geist_static)
target_compile_definitions(logical_source_index_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME logical_source_index_test COMMAND logical_source_index_test)

add_executable(message_topic_ir_synthetic_test
  tests/message_topic_ir_synthetic.cpp
)
target_link_libraries(message_topic_ir_synthetic_test PRIVATE geist::geist_static)
target_compile_definitions(message_topic_ir_synthetic_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME message_topic_ir_synthetic_test
  COMMAND message_topic_ir_synthetic_test)
# Same binary without the whole-corpus sweep for the iteration tier.
add_test(NAME message_topic_ir_focused_test
  COMMAND message_topic_ir_synthetic_test)
set_tests_properties(message_topic_ir_focused_test PROPERTIES
  ENVIRONMENT GEIST_SKIP_CORPUS_SWEEP=1)

add_executable(message_section_blocks_ir_synthetic_test
  tests/message_section_blocks_ir_synthetic.cpp
)
target_link_libraries(message_section_blocks_ir_synthetic_test
  PRIVATE geist::geist_static)
target_compile_definitions(message_section_blocks_ir_synthetic_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME message_section_blocks_ir_synthetic_test
  COMMAND message_section_blocks_ir_synthetic_test)

add_executable(trap_catalog_ir_synthetic_test
  tests/trap_catalog_ir_synthetic.cpp
)
target_link_libraries(trap_catalog_ir_synthetic_test
  PRIVATE geist::geist_static)
target_compile_definitions(trap_catalog_ir_synthetic_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME trap_catalog_ir_synthetic_test
  COMMAND trap_catalog_ir_synthetic_test)

add_executable(message_document_lowering_synthetic_test
  tests/message_document_lowering_synthetic.cpp
)
target_link_libraries(message_document_lowering_synthetic_test
  PRIVATE geist::geist_static
)
target_compile_definitions(message_document_lowering_synthetic_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME message_document_lowering_synthetic_test
  COMMAND message_document_lowering_synthetic_test)

add_executable(message_production_test
  tests/message_production.cpp
)
target_link_libraries(message_production_test PRIVATE geist::geist_static)
target_compile_definitions(message_production_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME message_production_test COMMAND message_production_test)

add_executable(message_prose_rows_test
  tests/message_prose_rows.cpp
)
target_link_libraries(message_prose_rows_test PRIVATE geist::geist_static)
target_compile_definitions(message_prose_rows_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME message_prose_rows_test COMMAND message_prose_rows_test)

add_executable(implicit_grid_test
  tests/implicit_grid.cpp
)
target_link_libraries(implicit_grid_test PRIVATE geist::geist_static)
target_compile_definitions(implicit_grid_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME implicit_grid_test COMMAND implicit_grid_test)

add_executable(source_rows_synthetic_test
  tests/source_rows_synthetic.cpp
)
target_link_libraries(source_rows_synthetic_test PRIVATE geist::geist_static)
add_test(NAME source_rows_synthetic_test COMMAND source_rows_synthetic_test)

add_executable(layout_ir_synthetic_test
  tests/layout_ir_synthetic.cpp
)
target_link_libraries(layout_ir_synthetic_test PRIVATE geist::geist_static)
add_test(NAME layout_ir_synthetic_test COMMAND layout_ir_synthetic_test)

add_executable(selector_display_ir_synthetic_test
  tests/selector_display_ir_synthetic.cpp
)
target_link_libraries(selector_display_ir_synthetic_test
  PRIVATE geist::geist_static
)
target_compile_definitions(selector_display_ir_synthetic_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME selector_display_ir_synthetic_test
  COMMAND selector_display_ir_synthetic_test)

add_executable(comment_delivery_ir_synthetic_test
  tests/comment_delivery_ir_synthetic.cpp
)
target_link_libraries(comment_delivery_ir_synthetic_test
  PRIVATE geist::geist_static
)
target_compile_definitions(comment_delivery_ir_synthetic_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME comment_delivery_ir_synthetic_test
  COMMAND comment_delivery_ir_synthetic_test)

add_executable(comment_delivery_document_lowering_synthetic_test
  tests/comment_delivery_document_lowering_synthetic.cpp
)
target_link_libraries(comment_delivery_document_lowering_synthetic_test
  PRIVATE geist::geist_static
)
target_compile_definitions(comment_delivery_document_lowering_synthetic_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME comment_delivery_document_lowering_synthetic_test
  COMMAND comment_delivery_document_lowering_synthetic_test)

add_executable(publication_document_lowering_synthetic_test
  tests/publication_document_lowering_synthetic.cpp
)
target_link_libraries(publication_document_lowering_synthetic_test
  PRIVATE geist::geist_static
)
add_test(NAME publication_document_lowering_synthetic_test
  COMMAND publication_document_lowering_synthetic_test)

add_executable(fixed_prose_topic_ir_synthetic_test
  tests/fixed_prose_topic_ir_synthetic.cpp
)
target_link_libraries(fixed_prose_topic_ir_synthetic_test
  PRIVATE geist::geist_static
)
target_compile_definitions(fixed_prose_topic_ir_synthetic_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME fixed_prose_topic_ir_synthetic_test
  COMMAND fixed_prose_topic_ir_synthetic_test)

add_executable(generated_list_topic_ir_synthetic_test
  tests/generated_list_topic_ir_synthetic.cpp
)
target_link_libraries(generated_list_topic_ir_synthetic_test
  PRIVATE geist::geist_static
)
target_compile_definitions(generated_list_topic_ir_synthetic_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME generated_list_topic_ir_synthetic_test
  COMMAND generated_list_topic_ir_synthetic_test)

add_executable(glossary_catalog_ir_synthetic_test
  tests/glossary_catalog_ir_synthetic.cpp
)
target_link_libraries(glossary_catalog_ir_synthetic_test
  PRIVATE geist::geist_static
)
target_compile_definitions(glossary_catalog_ir_synthetic_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME glossary_catalog_ir_synthetic_test
  COMMAND glossary_catalog_ir_synthetic_test)

add_executable(glossary_document_lowering_synthetic_test
  tests/glossary_document_lowering_synthetic.cpp
)
target_link_libraries(glossary_document_lowering_synthetic_test
  PRIVATE geist::geist_static
)
target_compile_definitions(glossary_document_lowering_synthetic_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME glossary_document_lowering_synthetic_test
  COMMAND glossary_document_lowering_synthetic_test)

add_executable(glossary_production_test
  tests/glossary_production.cpp
)
target_link_libraries(glossary_production_test
  PRIVATE geist::geist_static
)
target_compile_definitions(glossary_production_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME glossary_production_test COMMAND glossary_production_test)

add_executable(menu_topic_ir_synthetic_test
  tests/menu_topic_ir_synthetic.cpp
)
target_link_libraries(menu_topic_ir_synthetic_test
  PRIVATE geist::geist_static
)
target_compile_definitions(menu_topic_ir_synthetic_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME menu_topic_ir_synthetic_test
  COMMAND menu_topic_ir_synthetic_test)

add_executable(menu_production_test
  tests/menu_production.cpp
)
target_link_libraries(menu_production_test
  PRIVATE geist::geist_static
)
target_compile_definitions(menu_production_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME menu_production_test COMMAND menu_production_test)

add_executable(generated_list_production_test
  tests/generated_list_production.cpp
)
target_link_libraries(generated_list_production_test
  PRIVATE geist::geist_static
)
target_compile_definitions(generated_list_production_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME generated_list_production_test
  COMMAND generated_list_production_test)

add_test(NAME boo2git_menu_links_test
  COMMAND ${CMAKE_COMMAND}
    -DBOO2GIT=$<TARGET_FILE:boo2git>
    -DBOOK=${CMAKE_CURRENT_SOURCE_DIR}/../BOO/FA1PLMM0.boo
    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/boo2git-menu-links
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/boo2git_menu_links.cmake
)

add_test(NAME boo2git_generated_list_links_test
  COMMAND ${CMAKE_COMMAND}
    -DBOO2GIT=$<TARGET_FILE:boo2git>
    -DBOOK=${CMAKE_CURRENT_SOURCE_DIR}/../BOO/GG24-395.boo
    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/boo2git-generated-list-output
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/boo2git_generated_list_links.cmake
)

add_test(NAME boo2git_glossary_test
  COMMAND ${CMAKE_COMMAND}
    -DBOO2GIT=$<TARGET_FILE:boo2git>
    -DBOOK=${CMAKE_CURRENT_SOURCE_DIR}/../BOO/SC31-711.boo
    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/boo2git-glossary-output
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/boo2git_glossary.cmake
)

add_test(NAME boo2git_message_links_test
  COMMAND ${CMAKE_COMMAND}
    -DBOO2GIT=$<TARGET_FILE:boo2git>
    -DBOOK=${CMAKE_CURRENT_SOURCE_DIR}/../BOO/SC31-711.boo
    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/boo2git-message-output
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/boo2git_message_links.cmake
)

add_executable(topic_document_lowering_test
  tests/topic_document_lowering.cpp
)
target_link_libraries(topic_document_lowering_test PRIVATE geist::geist_static)
target_compile_definitions(topic_document_lowering_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME topic_document_lowering_test COMMAND topic_document_lowering_test)

# Typed-route coverage ratchet for issue #58: lowers every TOC topic of every
# BOO fixture (no Markdown rendering) and fails when the typed count drops
# below the committed baseline.
add_executable(typed_route_inventory_test
  tests/typed_route_inventory.cpp
)
target_link_libraries(typed_route_inventory_test PRIVATE geist::geist_static)
target_compile_definitions(typed_route_inventory_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME typed_route_inventory_test COMMAND typed_route_inventory_test)

add_executable(document_ir_synthetic_test
  tests/document_ir_synthetic.cpp
)
target_link_libraries(document_ir_synthetic_test PRIVATE geist::geist_static)
add_test(NAME document_ir_synthetic_test COMMAND document_ir_synthetic_test)

add_executable(document_markdown_renderer_synthetic_test
  tests/document_markdown_renderer_synthetic.cpp
)
target_link_libraries(document_markdown_renderer_synthetic_test
  PRIVATE geist::geist_static
)
add_test(NAME document_markdown_renderer_synthetic_test
  COMMAND document_markdown_renderer_synthetic_test)

add_executable(procedure_rows_synthetic_test
  tests/procedure_rows_synthetic.cpp
)
target_link_libraries(procedure_rows_synthetic_test PRIVATE geist::geist_static)
add_test(NAME procedure_rows_synthetic_test COMMAND procedure_rows_synthetic_test)

add_executable(sc31_711_row_ownership_test
  tests/sc31_711_row_ownership.cpp
)
target_link_libraries(sc31_711_row_ownership_test PRIVATE geist::geist_static)
target_compile_definitions(sc31_711_row_ownership_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME sc31_711_row_ownership_test COMMAND sc31_711_row_ownership_test)

# Publication acceptance evidence for issue #48 / #58. M5 registers it only
# after every frozen residual is satisfied by verified semantic IR lowering.
add_executable(sc31_711_ir_publications_acceptance
  tests/sc31_711_ir_publications_acceptance.cpp
)
target_link_libraries(sc31_711_ir_publications_acceptance
  PRIVATE geist::geist_static
)
target_compile_definitions(sc31_711_ir_publications_acceptance PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME sc31_711_ir_publications_acceptance
  COMMAND sc31_711_ir_publications_acceptance)

add_executable(sc31_711_cross_references_test
  tests/sc31_711_cross_references.cpp
)
target_link_libraries(sc31_711_cross_references_test PRIVATE geist::geist_static)
target_compile_definitions(sc31_711_cross_references_test PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)
add_test(NAME sc31_711_cross_references_test
  COMMAND sc31_711_cross_references_test)

add_executable(fixed_display_synthetic_test
  tests/fixed_display_synthetic.cpp
)
target_link_libraries(fixed_display_synthetic_test PRIVATE geist::geist_static)

add_test(NAME fixed_display_synthetic_test COMMAND fixed_display_synthetic_test)

# Per-book lazy-open integration tests. Each executable opens only the books it
# asserts on so failures localize and CTest can run them in parallel.
foreach(lazy_open_book
    qs3x36cm gg24 xwebdemo sc41_485 sc34_425 prg1sort sh20_918 itppibok
    sh12_565 sc24_546 sc31_711_topics sc31_711_catalogs fixed_displays)
  add_executable(lazy_open_${lazy_open_book}_test
    tests/lazy_open_${lazy_open_book}.cpp
  )
  target_link_libraries(lazy_open_${lazy_open_book}_test
    PRIVATE geist::geist_static)
  target_compile_definitions(lazy_open_${lazy_open_book}_test
    PRIVATE
      GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
  )
  add_test(NAME lazy_open_${lazy_open_book}_test
    COMMAND lazy_open_${lazy_open_book}_test)
endforeach()

add_executable(geist_open_benchmark
  tests/open_benchmark.cpp
)
target_link_libraries(geist_open_benchmark PRIVATE geist::geist_static)

# Whole-corpus sweeps, full-book exports, and multi-minute production renders.
# They remain part of the full gate; iterate with `ctest -LE slow` and run
# `ctest -L slow -j4` (they contend for memory bandwidth, not CPU) before
# committing.
set_tests_properties(
  message_topic_ir_synthetic_test
  message_document_lowering_synthetic_test
  message_section_blocks_ir_synthetic_test
  message_production_test
  logical_source_index_test
  generated_list_topic_ir_synthetic_test
  glossary_catalog_ir_synthetic_test
  selector_display_ir_synthetic_test
  qs3x36cm_markdown_test
  sc31_711_cross_references_test
  lazy_open_sc34_425_test
  lazy_open_sc31_711_catalogs_test
  boo2git_glossary_test
  boo2git_message_links_test
  typed_route_inventory_test
  PROPERTIES LABELS slow)
