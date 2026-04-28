cmake_minimum_required(VERSION 3.20)

project(
  dosbundle
  VERSION 0.1.0
  DESCRIPTION "DOS application bundler targeting macOS first"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DOSBUNDLE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

add_executable(dosbundle
  src/bundle.cpp
  src/common.cpp
  src/config.cpp
  src/launch.cpp
  src/manifest.cpp
  src/main.cpp
  src/package.cpp
)

target_include_directories(dosbundle
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
  target_compile_options(dosbundle PRIVATE /W4 /permissive-)
  if(DOSBUNDLE_WARNINGS_AS_ERRORS)
    target_compile_options(dosbundle PRIVATE /WX)
  endif()
else()
  target_compile_options(dosbundle PRIVATE -Wall -Wextra -Wpedantic)
  if(DOSBUNDLE_WARNINGS_AS_ERRORS)
    target_compile_options(dosbundle PRIVATE -Werror)
  endif()
endif()

install(TARGETS dosbundle RUNTIME DESTINATION bin)
