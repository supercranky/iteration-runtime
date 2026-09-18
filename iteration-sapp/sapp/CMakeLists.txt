# select the 3D backend for sokol_gfx.h and sokol_app.h
# the sokol_config() macro is in the top-level CMakeLists.txt file!
sokol_config()

fips_ide_group(Iteration)

fips_begin_app(iteration-sapp windowed)
fips_add_subdirectory(quickjs)

fips_vs_warning_level(3)
fips_files(lib/)

fips_files(iteration-sapp.c)

fips_files(runtime.c)
fips_files(engines/engines.c)
fips_files(engines/wasm_plugins.c engines/wasm_plugins.h)
fips_files(plugins/iteration_plugin.h)
set(iteration_application_plugins "$ENV{ITERATION_APPLICATION_PLUGINS}")
if(FIPS_IOS AND CMAKE_BUILD_TYPE STREQUAL "Release" AND iteration_application_plugins)
  if(NOT EXISTS "${iteration_application_plugins}")
    message(FATAL_ERROR "Application plugin build file not found: ${iteration_application_plugins}")
  endif()
  include("${iteration_application_plugins}")
  add_definitions(-DITERATION_NATIVE_PLUGINS=1 -DITERATION_APPLICATION_PLUGINS=1)
  set(iteration_use_application_plugins ON)
endif()

fips_files(timers.c)

fips_files(soloud/src/c_api/soloud_c.cpp)

if(FIPS_IOS)
  fips_files("Launch Screen.storyboard")
  fips_files("Info.plist")

  # fips_files(data/ios-info.plist)
endif()

fips_dir(data)

if(FIPS_IOS)
else()
  fipsutil_copy(iteration-assets.yml)
endif()

# copy assets
file(
  COPY
  assets
  DESTINATION
  ${CMAKE_CURRENT_BINARY_DIR}
)

fips_deps(sokol fileutil quickjs nanovg soloud)

if(FIPS_IOS AND NOT iteration_use_application_plugins)
  set(WAMR_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../wasm-micro-runtime")
  set(WAMR_BUILD_PLATFORM darwin)
  set(WAMR_BUILD_TARGET AARCH64)
  set(WAMR_BUILD_INTERP 1)
  set(WAMR_BUILD_FAST_INTERP 1)
  # iOS rejects WAMR's 8 GiB guard-page reservation used by hardware bounds
  # checking. Software bounds checks avoid the failed mmap at module load.
  set(WAMR_DISABLE_HW_BOUND_CHECK 1)
  set(WAMR_BUILD_AOT 0)
  set(WAMR_BUILD_LIBC_BUILTIN 0)
  set(WAMR_BUILD_LIBC_WASI 0)
  include("${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake")
  add_library(iteration-wamr STATIC ${WAMR_RUNTIME_LIB_SOURCE})
  target_include_directories(iteration-wamr PUBLIC
    "${WAMR_ROOT_DIR}/core/iwasm/include")
  add_definitions(-DITERATION_WAMR=1)
  fips_deps(iteration-wamr)
endif()

fips_end_app()

if(FIPS_EMSCRIPTEN)
  set(iteration_example_wasm "${FIPS_PROJECT_DEPLOY_DIR}/example.wasm")
  add_custom_command(
    OUTPUT "${iteration_example_wasm}"
    COMMAND ${CMAKE_C_COMPILER}
      "${CMAKE_CURRENT_LIST_DIR}/plugins/example.c"
      -I"${CMAKE_CURRENT_LIST_DIR}/plugins"
      -Oz --no-entry
      -sSTANDALONE_WASM=1
      -sALLOW_MEMORY_GROWTH=0
      -sINITIAL_MEMORY=2097152
      -sERROR_ON_UNDEFINED_SYMBOLS=1
      "-sEXPORTED_FUNCTIONS=['_iteration_abi_version','_iteration_manifest_ptr','_iteration_manifest_len','_iteration_alloc','_iteration_free','_iteration_call']"
      -o "${iteration_example_wasm}"
    DEPENDS plugins/example.c plugins/iteration_plugin.h
    VERBATIM)
  add_custom_target(iteration-example-plugin ALL DEPENDS
    "${iteration_example_wasm}")
  add_dependencies(iteration-sapp iteration-example-plugin)
elseif(FIPS_IOS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(iteration_emcc
    "${CMAKE_CURRENT_LIST_DIR}/../../fips-sdks/emsdk/upstream/emscripten/emcc")
  find_program(iteration_host_python NAMES python3 python REQUIRED)
  set(iteration_example_wasm "${CMAKE_CURRENT_BINARY_DIR}/example.wasm")
  add_custom_command(
    OUTPUT "${iteration_example_wasm}"
    COMMAND ${CMAKE_COMMAND} -E env "EMSDK_PYTHON=${iteration_host_python}"
      "${iteration_emcc}" "${CMAKE_CURRENT_LIST_DIR}/plugins/example.c"
      -I"${CMAKE_CURRENT_LIST_DIR}/plugins"
      -Oz --no-entry
      -sSTANDALONE_WASM=1
      -sALLOW_MEMORY_GROWTH=0
      -sINITIAL_MEMORY=2097152
      -sERROR_ON_UNDEFINED_SYMBOLS=1
      "-sEXPORTED_FUNCTIONS=['_iteration_abi_version','_iteration_manifest_ptr','_iteration_manifest_len','_iteration_alloc','_iteration_free','_iteration_call']"
      -o "${iteration_example_wasm}"
    DEPENDS plugins/example.c plugins/iteration_plugin.h
    VERBATIM)
  add_custom_target(iteration-example-plugin ALL DEPENDS
    "${iteration_example_wasm}")
  add_dependencies(iteration-sapp iteration-example-plugin)
  set_source_files_properties("${iteration_example_wasm}" PROPERTIES
    GENERATED TRUE MACOSX_PACKAGE_LOCATION Resources)
  target_sources(iteration-sapp PUBLIC "${iteration_example_wasm}")
endif()

if(FIPS_IOS)
  target_link_libraries(iteration-sapp "-framework AVFoundation")
endif()
