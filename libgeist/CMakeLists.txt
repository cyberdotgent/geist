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
  LANGUAGES CXX
)

find_package(PNG REQUIRED)
find_package(GIF REQUIRED)

set(GEIST_SOURCES
  src/document.cpp
  src/ebcdic.cpp
  src/img/gdf.cpp
  src/img/gdf_raster.cpp
  src/io.cpp
  src/logical.cpp
  src/markup.cpp
  src/markdown.cpp
  src/img/mmr.cpp
  src/img/mmr_tables.cpp
  src/page_runs.cpp
  src/img/png.cpp
  src/properties.cpp
  src/resources.cpp
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
  target_link_libraries(${example} PRIVATE geist::geist)
  if(WIN32)
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
