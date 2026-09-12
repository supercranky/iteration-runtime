# Embedded QuickJS runtime.
cmake_minimum_required(VERSION 3.5)
project(quickjs C)

set(quickjs_version 2026-06-04)
set(quickjs_sources_root ${CMAKE_CURRENT_LIST_DIR}/sources/quickjs-${quickjs_version})

add_library(quickjs STATIC
  ${quickjs_sources_root}/quickjs.c
  ${quickjs_sources_root}/dtoa.c
  ${quickjs_sources_root}/libregexp.c
  ${quickjs_sources_root}/libunicode.c
  ${quickjs_sources_root}/cutils.c
  ${quickjs_sources_root}/quickjs-libc.c
)

target_compile_definitions(quickjs PRIVATE
  _GNU_SOURCE
  CONFIG_VERSION="${quickjs_version}"
)

if(FIPS_EMSCRIPTEN)
  target_compile_options(quickjs PRIVATE -sDISABLE_EXCEPTION_CATCHING=1)
endif()

target_include_directories(quickjs PUBLIC ${quickjs_sources_root})
