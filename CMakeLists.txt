﻿cmake_minimum_required (VERSION 3.15)
cmake_policy(SET CMP0091 NEW)

project (LaFlor C)

add_executable (LaFlor WIN32 LaFlor.c LaFlor.manifest LaFlor.rc)
set_target_properties(LaFlor PROPERTIES
  CMAKE_C_STANDARD 99
  CMAKE_C_STANDARD_REQUIRED TRUE
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

target_compile_definitions(LaFlor PRIVATE _UNICODE UNICODE)
target_link_libraries(LaFlor shlwapi)