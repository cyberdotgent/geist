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
  src/book_ir.cpp
  src/control_ir.cpp
  src/document.cpp
  src/ebcdic.cpp
  src/fixed_display.cpp
  src/implicit_grid.cpp
  src/img/gdf.cpp
  src/img/gdf_raster.cpp
  src/io.cpp
  src/layout_ir.cpp
  src/logical.cpp
  src/markup.cpp
  src/markdown.cpp
  src/ownership_ir.cpp
  src/img/mmr.cpp
  src/img/mmr_tables.cpp
  src/page_runs.cpp
  src/img/png.cpp
  src/properties.cpp
  src/procedure_rows.cpp
  src/publication_ir.cpp
  src/resources.cpp
  src/source_rows.cpp
  src/source_segments.cpp
  src/strings.cpp
  src/toc.cpp
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

# This executable freezes the remaining publication acceptance evidence for
# issue #48 / #58. It is intentionally not registered with CTest while red;
# the publication-IR milestone must make it pass before registering it.
add_executable(sc31_711_ir_publications_acceptance
  tests/sc31_711_ir_publications_acceptance.cpp
)
target_link_libraries(sc31_711_ir_publications_acceptance
  PRIVATE geist::geist_static
)
target_compile_definitions(sc31_711_ir_publications_acceptance PRIVATE
  GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)

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

add_executable(lazy_open_test
  tests/lazy_open.cpp
)
target_link_libraries(lazy_open_test PRIVATE geist::geist_static)
target_compile_definitions(lazy_open_test
  PRIVATE
    GEIST_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)

add_test(NAME lazy_open_test COMMAND lazy_open_test)

add_executable(geist_open_benchmark
  tests/open_benchmark.cpp
)
target_link_libraries(geist_open_benchmark PRIVATE geist::geist_static)
