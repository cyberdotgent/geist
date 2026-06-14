cmake_minimum_required(VERSION 3.16)

project(libgeist
  VERSION 0.1.0
  DESCRIPTION "Self-contained IBM BookManager BOO parser library"
  LANGUAGES CXX
)

add_library(geist STATIC
  src/boo.cpp
)

add_library(geist::geist ALIAS geist)

target_compile_features(geist PUBLIC cxx_std_17)

target_include_directories(geist
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(MSVC)
  target_compile_options(geist PRIVATE /W4)
else()
  target_compile_options(geist PRIVATE -Wall -Wextra -pedantic)
endif()

foreach(example booinfo bootoc boorsrc boorender)
  add_executable(${example}
    examples/${example}.cpp
  )
  target_link_libraries(${example} PRIVATE geist::geist)
endforeach()
