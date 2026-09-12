# Copyright (c) 2020 Kosaki Mezumona
# This cmake script is distributed under the MIT License.
# See LICENSE for more details.
cmake_minimum_required(VERSION 3.1)
project(quickjs)

set(quickjs_version 2021-03-27)
set(quickjs_url https://bellard.org/quickjs/quickjs-${quickjs_version}.tar.xz)
set(quickjs_sha1 95836721cf3931a0043461db6c710415f05ed2cf)
set(quickjs_extras_url https://bellard.org/quickjs/quickjs-extras-${quickjs_version}.tar.xz)
set(quickjs_extras_sha1 06b293cbbbd2b4f4a42f303ae5ff4cd71e0c9715)
set(quickjs_extract_dir ${CMAKE_CURRENT_BINARY_DIR}/sources)
set(quickjs_sources_root ${quickjs_extract_dir}/quickjs-${quickjs_version})

set(quickjs_source_dir ${CMAKE_CURRENT_LIST_DIR}/sources/quickjs-${quickjs_version})

set(quickjs_sources
  ${quickjs_sources_root}/quickjs.h
  ${quickjs_sources_root}/quickjs-libc.h
  ${quickjs_sources_root}/quickjs.c
  ${quickjs_sources_root}/libregexp.c
  ${quickjs_sources_root}/libunicode.c
  ${quickjs_sources_root}/libbf.c
  ${quickjs_sources_root}/cutils.c
  ${quickjs_sources_root}/quickjs-libc.c
)

if(FIPS_EMSCRIPTEN)
  set(USE_FLAGS "-s WASM=1 -s EXTRA_EXPORTED_RUNTIME_METHODS='[\"cwrap\", \"addFunction\", \"removeFunction\", \"stringToUTF8\", \"lengthBytesUTF8\", \"_malloc\", \"_free\"]' -s NODEJS_CATCH_EXIT=0 -s MODULARIZE=1 -s EXPORT_NAME=QuickJSRaw -s INVOKE_RUN=0 -s ALLOW_MEMORY_GROWTH=1 -s ALLOW_TABLE_GROWTH=1 -s FILESYSTEM=0")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${USE_FLAGS}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${USE_FLAGS}")
add_definitions(-D_GNU_SOURCE)

set(get_quickjs_cmake_in ${CMAKE_CURRENT_LIST_DIR}/get_quickjs.cmake.in)
set(get_quickjs_cmake ${CMAKE_CURRENT_BINARY_DIR}/get_quickjs.cmake)

configure_file(
  ${get_quickjs_cmake_in}
  ${get_quickjs_cmake}
  @ONLY
)

file(
  COPY
  ${quickjs_source_dir}
  DESTINATION
  ${quickjs_extract_dir}
)

# add_custom_command(
# OUTPUT
# ${quickjs_sources}
# DEPENDS
# ${get_quickjs_cmake}
# COMMENT
# Get quickjs sources.
# COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/get_quickjs.cmake
# )
add_library(${PROJECT_NAME}
  ${quickjs_sources}
)
target_compile_definitions(${PROJECT_NAME}
  PRIVATE
  CONFIG_VERSION="${quickjs_version}"
)
target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${quickjs_sources_root}
)
