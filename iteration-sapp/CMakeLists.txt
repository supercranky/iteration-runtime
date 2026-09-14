#
# project: sokol-samples
#
cmake_minimum_required(VERSION 3.5)
project(sokol-samples)
set(CMAKE_C_STANDARD 99)

set(SOLOUD_LIBMODPLUG 0)

if(CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
  set(CMAKE_CXX_STANDARD 17)
else()
  set(CMAKE_CXX_STANDARD 14)
endif()

if(SOKOL_USE_WGPU_DAWN)
  set(USE_DAWN_SDK ON)
endif()

# include the fips main cmake file
get_filename_component(FIPS_ROOT_DIR "../fips" ABSOLUTE)
include("${FIPS_ROOT_DIR}/cmake/fips.cmake")

fips_setup()

# Emscripten's default release optimization is -O3. Prefer the size-focused
# optimizer for all WebAssembly translation units and the final LTO link.
if(FIPS_EMSCRIPTEN AND CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(-Oz)
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -Oz")
endif()

fips_ide_group("Imports")

if(NOT USE_SOKOL_APP AND NOT SOKOL_USE_WGPU_DAWN AND NOT SOKOL_USE_D3D11 AND NOT SOKOL_USE_METAL)
  fips_import_fips_glfw_glfw3()
endif()

fips_import_fips_imgui_imgui()
fips_import_fips_cimgui_cimgui()
fips_import_fips_soloud_soloud()
fips_import_fips_nanovg_nanovg()

if(NOT FIPS_UWP)
  # fips_import_fips_libmodplug_libmodplug()
endif()

if(FIPS_IOS)
  set_target_properties(
    nanovg
    PROPERTIES
    MACOSX_BUNDLE YES
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
  )
endif()

fips_ide_group("")

# just suppress this pesky "missing field initializer warning" for now
if(FIPS_CLANG OR FIPS_GCC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-missing-field-initializers")
endif()

if(FIPS_GCC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-missing-braces")
endif()

# macro to select the sokol-backend and shader dialects
macro(sokol_config)
  add_definitions(-DWITH_MODPLUG=0)

  add_definitions(-DSOKOL_NO_DEPRECATED)

  if(FIPS_EMSCRIPTEN)
    if(FIPS_EMSCRIPTEN_USE_WEBGPU)
      add_definitions(-DSOKOL_WGPU)
      set(slang "wgpu")
    else()
      add_definitions(-DSOKOL_GLES3)
      set(slang "glsl300es")
    endif()
  elseif(FIPS_RASPBERRYPI)
    add_definitions(-DSOKOL_GLES2)
    set(slang "glsl100")
  elseif(FIPS_ANDROID)
    add_definitions(-DSOKOL_GLES3)
    set(slang "glsl300es")
  elseif(SOKOL_USE_D3D11)
    add_definitions(-DSOKOL_D3D11)
    set(slang "hlsl4")
  elseif(SOKOL_USE_METAL)
    add_definitions(-DSOKOL_METAL)

    if(FIPS_IOS)
      set(slang "metal_ios:metal_sim")
    else()
      set(slang "metal_macos")
    endif()
  else()
    if(FIPS_IOS)
      add_definitions(-DSOKOL_GLES3)
      set(slang "glsl300es")
    else()
      add_definitions(-DSOKOL_GLCORE33)
      set(slang "glsl330")
    endif()
  endif()
endmacro()

if(USE_SOKOL_APP)
  include_directories(libs)
  fips_ide_group("Libs")
  add_subdirectory(libs)
  fips_ide_group("Samples")
  add_subdirectory(sapp)

  if(FIPS_IOS)
    target_sources(iteration-sapp PUBLIC ${CMAKE_CURRENT_LIST_DIR}/sapp/assets)

    set_target_properties(iteration-sapp PROPERTIES
      MACOSX_BUNDLE TRUE
      MACOSX_FRAMEWORK_IDENTIFIER org.cmake.ExecutableTarget
      RESOURCE ${CMAKE_CURRENT_LIST_DIR}/sapp/assets)

    set_target_properties(iteration-sapp PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_LIST_DIR}/sapp/Info.plist)
  endif()

# set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/sapp/assets PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
else()
  if(FIPS_EMSCRIPTEN)
    if(FIPS_EMSCRIPTEN_USE_WEBGPU)
      add_subdirectory(wgpu)
    else()
      add_subdirectory(html5)
    endif()
  elseif(FIPS_RASPBERRYPI)
    add_subdirectory(raspi)
  elseif(SOKOL_USE_D3D11)
    add_subdirectory(d3d11)
  elseif(SOKOL_USE_METAL)
  # add_subdirectory(metal)
  elseif(SOKOL_USE_WGPU_DAWN)
    add_subdirectory(libs/dawnutil)
    add_subdirectory(wgpu)
  else()
    add_subdirectory(glfw)
  endif()
endif()

fips_finish()
