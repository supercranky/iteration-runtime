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

fips_files(timers.c)

# fips_files(soloud/src/c_api/soloud_c.cpp)
sokol_shader(loadpng-sapp.glsl ${slang})

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

fips_end_app()
