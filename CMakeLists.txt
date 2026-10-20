# Copyright (C) 2026 Soni Romanov
# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20)
project(MUTHUR VERSION 0.0.5 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(WIN32)
  add_custom_target(app_version
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/scripts/write-version.ps1"
      -OutputDirectory "${CMAKE_BINARY_DIR}/generated"
    BYPRODUCTS "${CMAKE_BINARY_DIR}/generated/muthur_version.h"
    VERBATIM)
  add_executable(MU-TH-UR WIN32 src/main.cpp)
  add_dependencies(MU-TH-UR app_version)
  target_include_directories(MU-TH-UR PRIVATE "${CMAKE_BINARY_DIR}/generated")
  target_compile_definitions(MU-TH-UR PRIVATE UNICODE _UNICODE NOMINMAX)
  target_link_libraries(MU-TH-UR PRIVATE gdiplus user32 gdi32 xaudio2 ole32)
  add_custom_command(TARGET MU-TH-UR POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:MU-TH-UR>/assets")
  if(MSVC)
    target_compile_options(MU-TH-UR PRIVATE /W4 /utf-8)
  endif()
endif()
enable_testing()
add_executable(console_tests tests/console_tests.cpp)
add_test(NAME console_commands COMMAND console_tests)
if(WIN32)
  add_executable(render_tests tests/render_tests.cpp)
  add_dependencies(render_tests app_version)
  target_include_directories(render_tests PRIVATE "${CMAKE_BINARY_DIR}/generated")
  target_compile_definitions(render_tests PRIVATE UNICODE _UNICODE NOMINMAX)
  target_link_libraries(render_tests PRIVATE gdiplus user32 gdi32 xaudio2 ole32)
  if(MSVC)
    target_compile_options(render_tests PRIVATE /W4 /utf-8)
  endif()
  add_test(NAME offscreen_rendering COMMAND render_tests)
endif()
