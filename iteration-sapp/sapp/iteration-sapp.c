//------------------------------------------------------------------------------
//  loadpng-sapp.c
//  Asynchronously load a png file via sokol_fetch.h, decode via stb_image.h
//  (this is non-perfect since it happens on the main thread)
//  and create a sokol-gfx texture from the decoded pixel data.
//
//  The CMakeLists.txt entry for loadpng-sapp.c also demonstrates the
//  sokol_file_copy() macro to copy assets into the fips deployment directory.
//
//  This is a modified version of texcube-sapp.c
//------------------------------------------------------------------------------
#define HANDMADE_MATH_IMPLEMENTATION
#define HANDMADE_MATH_NO_SSE
#include "HandmadeMath.h"
#include "sokol_gfx.h"
#include "sokol_app.h"
#include "sokol_fetch.h"
#include "sokol_glue.h"
#define SOKOL_GL_IMPL
#include "sokol_gl.h"
#include "stb/stb_image.h"
#include "dbgui/dbgui.h"
#include "loadpng-sapp.glsl.h"
#include <string.h>
#include "util/fileutil.h"

// #include <CoreFoundation/CoreFoundation.h>

#include "engines/engines.h"

#include "quickjs/quickjs.h"
#include "quickjs/quickjs-libc.h"
#include "quickjs/cutils.h"

#include "runtime.h"

#if defined(__ANDROID__)
#include <android/native_activity.h>
#endif

#define SOKOL_LOG(...) printf(__VA_ARGS__);

#if defined(__EMSCRIPTEN__)
#include "emscripten.h"
#define SOKOL_LOG(s) emscripten_log(EM_LOG_CONSOLE, s);
#else
#if defined(__ANDROID__)
#include <android/log.h>
#define LOG_TAG "iteration"
#define SOKOL_LOG(s) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, s)
#endif
#endif

#define JS_ATOM_length 48

/*static struct
{
  float rx, ry;
  sg_pass_action pass_action;
  sg_pipeline pip;
  sg_bindings bind;
  uint8_t file_buffer[256 * 1024];
} state;*/

static struct
{
  sg_pass_action pass_action;
  sgl_pipeline pip;
  sg_bindings bind;
  uint8_t file_buffer[256 * 1024];
  uint8_t javascript_file_buffer[5 * 1024 * 1024];
  JSContext *ctx;
} state;

typedef struct
{
  float x, y, z;
  int16_t u, v;
} vertex_t;

typedef struct
{
  JSValue callback;
} load_callback;

static void fetch_callback(const sfetch_response_t *);
static void fetch_javascript_callback(const sfetch_response_t *);

static JSValue js_print(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv)
{
  int i;
  const char *str;

  for (i = 0; i < argc; i++)
  {
    if (i != 0)
      putchar(' ');
    str = JS_ToCString(ctx, argv[i]);
    if (!str)
      return JS_EXCEPTION;
    fputs(str, stdout);

    JS_FreeCString(ctx, str);
  }
  putchar('\n');
  return JS_UNDEFINED;
}

static int js_module_init(JSContext *ctx, JSModuleDef *m)
{
  return 0;
}

/*static JSModuleDef *js_module_loader(JSContext *ctx,
                                     const char *module_name, int opaque)
{
  JSModuleDef *m;
  m = JS_NewCModule(ctx, module_name, js_module_init);

  emscripten_log(EM_LOG_CONSOLE, "trying to load module");
  emscripten_log(EM_LOG_CONSOLE, module_name);

  return m;
}*/

static void init(void)
{
  SOKOL_LOG("HELLO FROM ITERATION");

#if defined(__ANDROID__)
  const ANativeActivity *native_activity = sapp_android_get_native_activity();
  const AAssetManager *assetManager = native_activity->assetManager;
  android_fopen_set_asset_manager(assetManager);
#endif

  /* setup sokol-gfx and the optional debug-ui*/
  sg_setup(&(sg_desc){
      .context = sapp_sgcontext()});
  __dbgui_setup(sapp_sample_count());

  /* setup sokol-fetch with the minimal "resource limits" */
  sfetch_setup(&(sfetch_desc_t){
      .max_requests = 256,
      .num_channels = 256,
      .num_lanes = 256});

  /* setup sokol-gl */

  // should be SG_PIXELFORMAT_BGRA8 for ios

#if defined(SOKOL_METAL)
  sgl_setup(&(sgl_desc_t){
      .sample_count = sapp_sample_count(),
      .color_format = SG_PIXELFORMAT_BGRA8,
      .max_vertices = 1000000,
      .max_commands = 1000000});
#else
  sgl_setup(&(sgl_desc_t){
      .sample_count = sapp_sample_count(),
      .color_format = SG_PIXELFORMAT_RGBA8,
      .max_vertices = 1000000,
      .max_commands = 1000000});
#endif

  /* pass action for clearing the framebuffer to some color */
  state.pass_action = (sg_pass_action){
      .colors[0] = {.action = SG_ACTION_CLEAR, .value = {0.125f, 0.25f, 0.35f, 1.0f}}};

  /* Allocate an image handle, but don't actually initialize the image yet,
       this happens later when the asynchronous file load has finished.
       Any draw calls containing such an "incomplete" image handle
       will be silently dropped.
    */

  /*for (int i = 0; i < 256; i++)
  {
    state.textures[i] = sg_alloc_image();
  }*/

  /* cube vertex buffer with packed texcoords */

  /* a pipeline state object */

  /*state.pip = sg_make_pipeline(&(sg_pipeline_desc){
      .shader = sg_make_shader(loadpng_shader_desc()),
      .blend = {
          .enabled = true,
          .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
          .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA},
      .layout = {.attrs = {[ATTR_vs_pos].format = SG_VERTEXFORMAT_FLOAT3, [ATTR_vs_texcoord0].format = SG_VERTEXFORMAT_SHORT2N}},
      .index_type = SG_INDEXTYPE_UINT16,
      //.depth_stencil = {.depth_compare_func = SG_COMPAREFUNC_LESS_EQUAL, .depth_write_enabled = true},
      .rasterizer = {
          .cull_mode = SG_CULLMODE_BACK,
      },
      .label = "cube-pipeline"});*/

  state.pip = sgl_make_pipeline(&(sg_pipeline_desc){
      .colors[0].blend = {
          .enabled = true,
          .src_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_BLEND_ALPHA,
          .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA}});
  //.depth_stencil = {.depth_write_enabled = true, .depth_compare_func = SG_COMPAREFUNC_LESS_EQUAL}});

  char *code = "console.log('Hello from quickJS WASM!');";

  JSRuntime *runtime = JS_NewRuntime();

  js_std_init_handlers(runtime);

  state.ctx = JS_NewContext(runtime);

  JS_SetModuleLoaderFunc(runtime, NULL, js_module_loader, NULL);

  JSContext *ctx = state.ctx;

  // js_std_add_helpers(ctx, 0, NULL);

  JSValue global_obj, console, engine;

  global_obj = JS_GetGlobalObject(ctx);

  console = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, js_print, "log", 1));
  JS_SetPropertyStr(ctx, console, "error", JS_NewCFunction(ctx, js_print, "error", 1));

  JS_SetPropertyStr(ctx, global_obj, "console", console);

  JSValue process = JS_NewObject(ctx);
  JSValue env = JS_NewObject(ctx);
  JSValue nodeEnv = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, process, "env", env);
  JS_SetPropertyStr(ctx, env, "NODE_ENV", JS_NewString(ctx, "dev"));

  JS_SetPropertyStr(ctx, global_obj, "process", process);

  JSValue result = JS_Eval(ctx, code, strlen(code), "eval.js", JS_EVAL_TYPE_GLOBAL);

  js_init_module_engine(ctx, "runtime");
  js_init_module_engines(ctx, "engines");
  js_init_module_std(ctx, "std");
  js_init_module_os(ctx, "os");

  SOKOL_LOG("Loading javascript");
  char path_buf[512];

  sfetch_send(&(sfetch_request_t){
      .path = fileutil_get_path("index.js", path_buf, sizeof(path_buf)),
      .callback = fetch_javascript_callback,
      .buffer = SFETCH_RANGE(state.javascript_file_buffer)});

  // printf(JS_ToCString(ctx,result));
}

/* The fetch-callback is called by sokol_fetch.h when the data is loaded,
   or when an error has occurred.
*/
static void fetch_callback(const sfetch_response_t *response)
{
  if (response->fetched)
  {
    /* the file data has been fetched, since we provided a big-enough
           buffer we can be sure that all data has been loaded here
        */

    printf("Funkade");

    int png_width, png_height, num_channels;
    const int desired_channels = 4;
    stbi_uc *pixels = stbi_load_from_memory(
        response->data.ptr,
        (int)response->data.size,
        &png_width, &png_height,
        &num_channels, desired_channels);
    if (pixels)
    {
      /* ok, time to actually initialize the sokol-gfx texture */
      sg_init_image(state.bind.fs_images[SLOT_tex], &(sg_image_desc){
                                                        .width = png_width,
                                                        .height = png_height,
                                                        .pixel_format = SG_PIXELFORMAT_RGBA8,
                                                        .min_filter = SG_FILTER_NEAREST,
                                                        .mag_filter = SG_FILTER_NEAREST,
                                                        .data.subimage[0][0] = {
                                                            .ptr = pixels,
                                                            .size = png_width * png_height * 4,
                                                        }});
      stbi_image_free(pixels);
    }
  }
  else if (response->failed)
  {

    printf("Funkade inte");

    // if loading the file failed, set clear color to red
    state.pass_action = (sg_pass_action){
        .colors[0] = {.action = SG_ACTION_CLEAR, .value = {1.0f, 0.0f, 0.0f, 1.0f}}};
  }
}

static void fetch_javascript_callback(const sfetch_response_t *response)
{
  if (response->fetched)
  {
    /* the file data has been fetched, since we provided a big-enough
           buffer we can be sure that all data has been loaded here
        */

    JSValue result = JS_Eval(state.ctx, response->data.ptr, (int)response->data.size, "index.js", JS_EVAL_TYPE_MODULE);

    JSValue exception = JS_GetException(state.ctx);

    JSValue stack = JS_GetPropertyStr(state.ctx, exception, "stack");

    SOKOL_LOG("JS_Eval exception on js load");
    // SOKOL_LOG(JS_ToCString(state.ctx, exception));
    // SOKOL_LOG(JS_ToCString(state.ctx, stack));
    printf("JS load Funkade");
  }

  if (response->failed)
  {
    SOKOL_LOG("JS Load error");
  }
}

static void draw_quad(void)
{
  static float angle_deg = 0.0f;
  float scale = 1.0f + sinf(sgl_rad(angle_deg)) * 0.5f;
  angle_deg += 1.0f;
  sgl_enable_texture();
  sgl_texture(state.bind.fs_images[SLOT_tex]);

  sgl_rotate(sgl_rad(angle_deg), 0.0f, 0.0f, 1.0f);
  sgl_scale(scale, scale, 1.0f);
  sgl_begin_quads();
  sgl_c4b(255, 255, 255, 255);

  sgl_v2f_t2f(-0.5f, -0.5f, 1, 0);
  sgl_v2f_t2f(0.5f, -0.5f, 0, 0);
  sgl_v2f_t2f(0.5f, 0.5f, 0, 1);
  sgl_v2f_t2f(-0.5f, 0.5f, 1, 1);
  sgl_end();
}

// js_execute_jobs to enable stuff like promises and setTimeout

void js_engine_dump_error(JSContext *ctx)
{
  JSValue exception_val, val;
  const char *stack;
  BOOL is_error;

  exception_val = JS_GetException(ctx);
  is_error = JS_IsError(ctx, exception_val);
  if (!is_error)
    printf("Throw: ");
  js_print(ctx, JS_NULL, 1, (JSValueConst *)&exception_val);
  if (is_error)
  {
    val = JS_GetPropertyStr(ctx, exception_val, "stack");
    if (!JS_IsUndefined(val))
    {
      stack = JS_ToCString(ctx, val);
      printf("%s\n", stack);
      JS_FreeCString(ctx, stack);
    }
    JS_FreeValue(ctx, val);
  }
  JS_FreeValue(ctx, exception_val);
}

static void js_execute_jobs(JSContext *ctx)
{
  JSContext *ctx1;
  int err;
  int poll;

  for (;;)
  {
    err = JS_ExecutePendingJob(JS_GetRuntime(ctx), &ctx1);
    if (err <= 0)
    {
      if (err < 0)
        js_engine_dump_error(ctx1);
      break;
    }
  }
}

/* The frame-function is fairly boring, note that no special handling is
   needed for the case where the texture isn't loaded yet.
   Also note the sfetch_dowork() function, this is usually called once a
   frame to pump the sokol-fetch message queues.
*/
static void frame(void)
{

  sfetch_dowork();
  // js_std_loop(state.ctx);
  js_execute_jobs(state.ctx);
  js_update_timers(state.ctx);
  engine_frame();
}

static void event(const sapp_event *e)
{
  assert((e->type >= 0) && (e->type < _SAPP_EVENTTYPE_NUM));

  engine_handle_event(e);

  // state.items[e->type].event = *e;
  // simgui_handle_event(e);

  /* special case: show/hide mouse cursor when pressing SPACE */
  if ((e->type == SAPP_EVENTTYPE_KEY_DOWN) && !e->key_repeat)
  {
    switch (e->key_code)
    {
    case SAPP_KEYCODE_SPACE:
      // sapp_show_mouse(false);
      break;
    case SAPP_KEYCODE_M:
      // sapp_lock_mouse(true);
      break;
    default:
      break;
    }
  }
  else if (e->type == SAPP_EVENTTYPE_KEY_UP)
  {
    switch (e->key_code)
    {
    case SAPP_KEYCODE_SPACE:
      // sapp_show_mouse(true);
      break;
    case SAPP_KEYCODE_M:
      // sapp_lock_mouse(false);
      break;
    default:
      break;
    }
  }
}

static void cleanup(void)
{
  __dbgui_shutdown();
  sfetch_shutdown();
  sg_shutdown();
}

sapp_desc sokol_main(int argc, char *argv[])
{
  (void)argc;
  (void)argv;
  return (sapp_desc){
      .init_cb = init,
      .frame_cb = frame,
      .cleanup_cb = cleanup,
      .event_cb = event,
      .width = 800,
      .height = 600,
      .fullscreen = true,
      .sample_count = 4,
      .gl_force_gles2 = true,
      .high_dpi = false,
      .window_title = "ITERATION",
  };
}
