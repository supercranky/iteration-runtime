
#include "HandmadeMath.h"
#include "sokol_gfx.h"
#include "sokol_app.h"
#include "sokol_fetch.h"
#include "sokol_glue.h"
#include "sokol_gl.h"
#include "stb/stb_image.h"
#include "stb/stb_image_resize.h"
#include "util/fileutil.h"

#include "dbgui/dbgui.h"

#define SOKOL_LOG(...) printf(__VA_ARGS__);

#if defined(__EMSCRIPTEN__)
#include "emscripten.h"
#define SOKOL_LOG(...) emscripten_log(EM_LOG_CONSOLE, __VA_ARGS__);
#else
#if defined(__ANDROID__)
#include <android/log.h>
#define LOG_TAG "iteration"
#define SOKOL_LOG(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#endif
#endif

#include <string.h>

#include "quickjs/quickjs.h"
#include "quickjs/quickjs-libc.h"

#include "quickjs/cutils.h"
#include "runtime.h"
#include "soloud/soloud_c.h"

#include "nanovg.h"

#if defined(SOKOL_METAL)
#include "nanovg_mtl.h"

#else
#define GLFW_INCLUDE_ES3
#define GLFW_INCLUDE_GLEXT
#include <GLFW/glfw3.h>

#define NANOVG_GLES2_IMPLEMENTATION

#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"

#endif
#include "engines/engines.h"

#define FETCH_BUFFER_SIZE 1024 * 1024

typedef struct
{
  int widthSource;
  int heightSource;
  int widthTexture;
  int heightTexture;

} image_sizes;

static struct
{
  sg_pass_action pass_action;
  sg_pass_action pass_action_no_clear;

  sgl_pipeline pip;
  sg_bindings bind;
  sg_image textures[256];
  image_sizes texture_sizes[256];
  uint8_t file_buffer[1024 * 1024];
  JSValue frame_callback;
  JSValue resize_callback;

  Wav *sounds[256];
  JSValue keydown_callback;
  JSValue keyup_callback;
  JSValue mousedown_callback;
  JSValue mouseup_callback;
  JSValue mousemove_callback;
  JSValue touchstart_callback;
  JSValue touchend_callback;
  JSValue touchmove_callback;

  JSContext *ctx;
  int loaded_textures;
  int current_texture;
  int loaded_sounds;

  int viewport_mode;
  Soloud *soloud;
  int count;

  NVGcontext *vg;
} state;

typedef struct
{
  JSValue callback;
  JSValue parameter;
} load_callback;

JSValue engine_get_frame_callback()
{
  return state.frame_callback;
}

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

static float dpi_scale()
{
#if defined(__EMSCRIPTEN__)
  return 1;
#else
  return sapp_dpi_scale();
#endif
}

static float convert_screen_x_to_local(float _x)
{

  const int w = sapp_width();
  const int h = sapp_height();
  float real_w = w;
  float real_h = h;
  float scale;
  float size, x, y;

  switch (state.viewport_mode)
  {
  case ITER_VIEWPORT_COVER:
    size = w;
    x = 0;
    y = (h - w) / 2;

    if (h > w)
    {
      size = h;
      y = 0;
      x = (w - h) / 2;
    }

    return ((_x - w / 2) / size) * 1000;
  case ITER_VIEWPORT_FIXED_WIDTH:
    size = w;
    x = 0;
    y = (h - w) / 2;
    scale = 1;
    if (h > w)
    {
      size = h;
      y = 0;
      x = (w - h) / 2;
      scale = (real_h / real_w);
    }

    return ((_x - w / 2) / size) * 1000 * scale;
  case ITER_VIEWPORT_FIXED_HEIGHT:
    size = w;
    x = 0;
    y = (h - w) / 2;
    scale = (real_w / real_h);
    if (h > w)
    {
      size = h;
      y = 0;
      x = (w - h) / 2;
      scale = (real_h / real_w);
      size *= scale;
    }

    return ((_x - w / 2) / size) * 1000 * scale;
  }
}
static float convert_screen_y_to_local(float _y)
{
  const int w = sapp_width();
  const int h = sapp_height();
  float scale;
  float real_w = w;
  float real_h = h;
  int size, x, y;
  switch (state.viewport_mode)
  {
  case ITER_VIEWPORT_COVER:
    size = w;
    x = 0;
    y = (h - w) / 2;

    if (h > w)
    {
      size = h;
      y = 0;
      x = (w - h) / 2;
    }

    return ((_y - h / 2) / size) * 1000;
  case ITER_VIEWPORT_FIXED_WIDTH:
    size = w;
    x = 0;
    y = (h - w) / 2;
    scale = 1;
    if (h > w)
    {
      size = h;
      y = 0;
      x = (w - h) / 2;
      scale = (real_h / real_w);
    }

    return ((_y - h / 2) / size) * 1000 * scale;
  case ITER_VIEWPORT_FIXED_HEIGHT:
    size = w;
    x = 0;
    y = (h - w) / 2;
    scale = (real_w / real_h);
    if (h > w)
    {
      size = h;
      y = 0;
      x = (w - h) / 2;
      scale = 1;
    }

    return ((_y - h / 2) / size) * 1000 * scale;
  }
}

static float scale_screen_to_local(float value)
{

  const int w = sapp_width();
  const int h = sapp_height();
  float real_w = w;
  float real_h = h;
  float scale;
  float size, x, y;

  switch (state.viewport_mode)
  {
  case ITER_VIEWPORT_COVER:
    size = w;
    if (h > w)
    {
      size = h;
    }

    return (value / size) * 1000;
  case ITER_VIEWPORT_FIXED_WIDTH:
    size = w;
    scale = 1;
    if (h > w)
    {
      size = h;
      scale = (real_h / real_w);
    }

    return (value / size) * 1000 * scale;
  case ITER_VIEWPORT_FIXED_HEIGHT:
    size = w;
    scale = (real_w / real_h);
    if (h > w)
    {
      size = h;
      scale = 1;
    }

    return (value / size) * 1000 * scale;
  }
}

static float convert_local_x_to_screen(float _x)
{

  const float w = (float)sapp_width() / dpi_scale();
  const float h = (float)sapp_height() / dpi_scale();
  float real_w = w;
  float real_h = h;
  float scale;
  float size, x, y;

  switch (state.viewport_mode)
  {
  case ITER_VIEWPORT_COVER:
    size = w;
    x = 0;
    y = (h - w) / 2;

    if (h > w)
    {
      size = h;
      y = 0;
      x = (w - h) / 2;
    }

    return ((_x - w / 2) / size) * 1000;
  case ITER_VIEWPORT_FIXED_WIDTH:
    size = w;
    x = 0;
    y = (h - w) / 2;
    scale = 1;
    if (h > w)
    {
      size = h;
      y = 0;
      x = (w - h) / 2;
      scale = (real_h / real_w);
    }

    return ((_x - w / 2) / size) * 1000 * scale;
  case ITER_VIEWPORT_FIXED_HEIGHT:
    size = w;
    x = 0;
    y = (h - w) / 2;
    scale = (real_w / real_h);
    if (h > w)
    {
      size = h;
      y = 0;
      x = (w - h) / 2;
      scale = (real_h / real_w);
      size *= scale;
    }

    return (w / 2) + (_x / (1000 * scale)) * size;

    // return ((_x - w / 2) / size) * 1000 * scale;
  }
}
static float convert_local_y_to_screen(float _y)
{

  const float w = (float)sapp_width() / dpi_scale();
  const float h = (float)sapp_height() / dpi_scale();

  float scale;
  float real_w = w;
  float real_h = h;
  float size, x, y;

  switch (state.viewport_mode)
  {
  case ITER_VIEWPORT_COVER:
    size = w;
    x = 0;
    y = (h - w) / 2;

    if (h > w)
    {
      size = h;
      y = 0;
      x = (w - h) / 2;
    }

    return ((_y - h / 2) / size) * 1000;
  case ITER_VIEWPORT_FIXED_WIDTH:
    size = w;
    x = 0;
    y = (h - w) / 2;
    scale = 1;
    if (h > w)
    {
      size = h;
      y = 0;
      x = (w - h) / 2;
      scale = (real_h / real_w);
    }

    return ((_y - h / 2) / size) * 1000 * scale;
  case ITER_VIEWPORT_FIXED_HEIGHT:
    size = w;
    x = 0;
    y = (h - w) / 2;
    scale = (real_w / real_h);
    if (h > w)
    {
      size = h;
      y = 0;
      x = (w - h) / 2;
      scale = 1;
    }

    return (h / 2) + (_y / 1000) * h;

    // return ((_y - h / 2) / size) * 1000 * scale;
  }
}

static float scale_local_to_screen(float value)
{

  const float w = (float)sapp_width() / dpi_scale();
  const float h = (float)sapp_height() / dpi_scale();
  float real_w = w;
  float real_h = h;
  float scale;
  float size, x, y;

  switch (state.viewport_mode)
  {
  case ITER_VIEWPORT_COVER:
    size = w;
    if (h > w)
    {
      size = h;
    }

    return (value / size) * 1000;
  case ITER_VIEWPORT_FIXED_WIDTH:
    size = w;
    scale = 1;
    if (h > w)
    {
      size = h;
      scale = (real_h / real_w);
    }

    return (value / size) * 1000 * scale;
  case ITER_VIEWPORT_FIXED_HEIGHT:
    size = w;
    scale = (real_w / real_h);
    if (h > w)
    {
      size = h;
      scale = 1;
    }

    return (value / 1000) * h;
  }
}

static void event_callback_call(JSContext *ctx, JSValueConst func, JSValueConst *event)
{
  JSValue ret;
  ret = JS_Call(ctx, func, JS_UNDEFINED, 1, event);
  if (JS_IsException(ret))
    js_std_dump_error(ctx);
  JS_FreeValue(ctx, ret);
}

void set_viewport()
{
  const float w = sapp_width();
  const float h = sapp_height();

  int size, x, y;

  switch (engine_get_viewport_mode())
  {
  case ITER_VIEWPORT_COVER:
    size = w;
    x = 0;
    y = (h - w) / 2;

    if (h > w)
    {
      size = h;
      y = 0;
      x = (w - h) / 2;
    }

    sgl_viewport(x, y, size, size, true);
    break;
  case ITER_VIEWPORT_FIXED_WIDTH:
    x = 0;
    y = (h - w) / 2;

    sgl_load_identity();
    sgl_viewport(0, 0, w, h, true);
    sgl_scale(1, w / h, 1);
    break;
  case ITER_VIEWPORT_FIXED_HEIGHT:

    size = h;
    y = 0;
    x = (w - h) / 2;
    sgl_load_identity();
    sgl_viewport(0, 0, w, h, true);
    sgl_scale(h / w, 1, 1);
    break;
  }
}

void engine_handle_event(const sapp_event *e)
{
  set_viewport();

  if ((e->type == SAPP_EVENTTYPE_KEY_DOWN) && !e->key_repeat)
  {
    if (JS_IsFunction(state.ctx, state.keydown_callback))
    {
      JSValue event = JS_NewObject(state.ctx);
      JSValue keyCode = JS_NewInt32(state.ctx, e->key_code);
      JS_SetPropertyStr(state.ctx, event, "keyCode", keyCode);
      event_callback_call(state.ctx, state.keydown_callback, &event);
    }
  }

  if ((e->type == SAPP_EVENTTYPE_KEY_UP) && !e->key_repeat)
  {
    if (JS_IsFunction(state.ctx, state.keyup_callback))
    {
      JSValue event = JS_NewObject(state.ctx);
      JSValue keyCode = JS_NewInt32(state.ctx, e->key_code);
      JS_SetPropertyStr(state.ctx, event, "keyCode", keyCode);
      event_callback_call(state.ctx, state.keyup_callback, &event);
    }
  }

  if ((e->type == SAPP_EVENTTYPE_MOUSE_DOWN))
  {
    if (JS_IsFunction(state.ctx, state.mousedown_callback))
    {
      JSValue event = JS_NewObject(state.ctx);
      JSValue x = JS_NewInt32(state.ctx, convert_screen_x_to_local(e->mouse_x));
      JSValue y = JS_NewInt32(state.ctx, convert_screen_y_to_local(e->mouse_y));

      JS_SetPropertyStr(state.ctx, event, "x", x);
      JS_SetPropertyStr(state.ctx, event, "y", y);

      event_callback_call(state.ctx, state.mousedown_callback, &event);
    }
  }

  if ((e->type == SAPP_EVENTTYPE_MOUSE_UP))
  {
    if (JS_IsFunction(state.ctx, state.mouseup_callback))
    {
      JSValue event = JS_NewObject(state.ctx);
      JSValue x = JS_NewInt32(state.ctx, convert_screen_x_to_local(e->mouse_x));
      JSValue y = JS_NewInt32(state.ctx, convert_screen_y_to_local(e->mouse_y));

      JS_SetPropertyStr(state.ctx, event, "x", x);
      JS_SetPropertyStr(state.ctx, event, "y", y);

      event_callback_call(state.ctx, state.mouseup_callback, &event);
    }
  }

  if ((e->type == SAPP_EVENTTYPE_MOUSE_MOVE))
  {
    if (JS_IsFunction(state.ctx, state.mousemove_callback))
    {
      JSValue event = JS_NewObject(state.ctx);
      JSValue x = JS_NewInt32(state.ctx, convert_screen_x_to_local(e->mouse_x));
      JSValue y = JS_NewInt32(state.ctx, convert_screen_y_to_local(e->mouse_y));

      JS_SetPropertyStr(state.ctx, event, "x", x);
      JS_SetPropertyStr(state.ctx, event, "y", y);

      event_callback_call(state.ctx, state.mousemove_callback, &event);
    }
  }

  if ((e->type == SAPP_EVENTTYPE_TOUCHES_BEGAN))
  {
    /*Sfxr *fx = Sfxr_create();
    Sfxr_loadPreset(fx, SFXR_COIN, state.count);
    state.count++;
    Soloud_play(state.soloud, fx);*/

    if (JS_IsFunction(state.ctx, state.touchstart_callback))
    {
      JSValue event;
      JSValue x;
      JSValue y;
      JSValue id;
      for (int i = 0; i < e->num_touches; i++)
      {
        JSValue event = JS_NewObject(state.ctx);
        JSValue x = JS_NewInt32(state.ctx, convert_screen_x_to_local(e->touches[i].pos_x));
        JSValue y = JS_NewInt32(state.ctx, convert_screen_y_to_local(e->touches[i].pos_y));
        JSValue id = JS_NewInt32(state.ctx, e->touches[i].identifier);
        JS_SetPropertyStr(state.ctx, event, "x", x);
        JS_SetPropertyStr(state.ctx, event, "y", y);
        JS_SetPropertyStr(state.ctx, event, "touchId", id);

        event_callback_call(state.ctx, state.touchstart_callback, &event);
      }
    }
  }

  if ((e->type == SAPP_EVENTTYPE_TOUCHES_ENDED || e->type == SAPP_EVENTTYPE_TOUCHES_CANCELLED))
  {
    if (JS_IsFunction(state.ctx, state.touchend_callback))
    {
      JSValue event;
      JSValue x;
      JSValue y;
      JSValue id;
      for (int i = 0; i < e->num_touches; i++)
      {
        JSValue event = JS_NewObject(state.ctx);
        JSValue x = JS_NewInt32(state.ctx, convert_screen_x_to_local(e->touches[i].pos_x));
        JSValue y = JS_NewInt32(state.ctx, convert_screen_y_to_local(e->touches[i].pos_y));
        JSValue id = JS_NewInt32(state.ctx, e->touches[i].identifier);
        JS_SetPropertyStr(state.ctx, event, "x", x);
        JS_SetPropertyStr(state.ctx, event, "y", y);
        JS_SetPropertyStr(state.ctx, event, "touchId", id);

        event_callback_call(state.ctx, state.touchend_callback, &event);
      }
    }
  }

  if ((e->type == SAPP_EVENTTYPE_TOUCHES_MOVED))
  {
#if defined(__EMSCRIPTEN__)
    SOKOL_LOG("touch moved");
#endif
    if (JS_IsFunction(state.ctx, state.touchmove_callback))
    {

      JSValue event;
      JSValue x;
      JSValue y;
      JSValue id;

      for (int i = 0; i < e->num_touches; i++)
      {
        JSValue event = JS_NewObject(state.ctx);
        JSValue x = JS_NewInt32(state.ctx, convert_screen_x_to_local(e->touches[i].pos_x));
        JSValue y = JS_NewInt32(state.ctx, convert_screen_y_to_local(e->touches[i].pos_y));
        JSValue id = JS_NewInt32(state.ctx, e->touches[i].identifier);
        JS_SetPropertyStr(state.ctx, event, "x", x);
        JS_SetPropertyStr(state.ctx, event, "y", y);
        JS_SetPropertyStr(state.ctx, event, "touchId", id);

        event_callback_call(state.ctx, state.touchmove_callback, &event);
      }
    }
  }

  if ((e->type == SAPP_EVENTTYPE_RESIZED))
  {
    SOKOL_LOG("resized");

    if (JS_IsFunction(state.ctx, state.resize_callback))
    {
      JS_Call(state.ctx, state.resize_callback, JS_UNDEFINED, 0, NULL);
    }
  }
}

static float get_translated_x(float _x)
{
  float translated_x;
  float w = sapp_width();
  float h = sapp_height();

  switch (state.viewport_mode)
  {
  case ITER_VIEWPORT_COVER:
    if (w > h)
    {
      translated_x = _x;
    }
    else
    {
      translated_x = _x * (w / h);
    }
    break;
  case ITER_VIEWPORT_FIXED_WIDTH:
    translated_x = _x;
    break;
  case ITER_VIEWPORT_FIXED_HEIGHT:
    translated_x = _x * (w / h);
    break;
  }
  return translated_x;
}

static float get_translated_y(float _y)
{
  float translated_y;
  float w = sapp_width();
  float h = sapp_height();

  switch (state.viewport_mode)
  {
  case ITER_VIEWPORT_COVER:
    if (h > w)
    {
      translated_y = _y;
    }
    else
    {
      translated_y = _y * (h / w);
    }
    break;
  case ITER_VIEWPORT_FIXED_WIDTH:
    translated_y = _y * (h / w);
    break;
  case ITER_VIEWPORT_FIXED_HEIGHT:
    translated_y = _y;
    break;
  }
  return translated_y;
}

/*
    let perspective = 10000;
    let dist = 200;
    actor.Rect.x = (actor.StarAnimation.x / actor.StarAnimation.z) * perspective;
    actor.Rect.y = (actor.StarAnimation.y / actor.StarAnimation.z) * perspective;
    actor.Rect.scale = ((dist - actor.StarAnimation.z) / dist) * 0.2;
    actor.Sprite.alpha = ((dist - actor.StarAnimation.z) / dist) * 0.2;

    actor.StarAnimation.z -= 0.4;
    if (actor.StarAnimation.z < 0) {
      actor.StarAnimation.z = dist;
    }
*/

static JSValue js_engine_get_screen_left(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
  JSValue left_js = JS_NewFloat64(state.ctx, get_translated_x(-500));
  return left_js;
}

static JSValue js_engine_get_screen_right(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv)
{
  JSValue right_js = JS_NewFloat64(state.ctx, get_translated_x(500));
  return right_js;
}

static JSValue js_engine_get_screen_top(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv)
{
  JSValue top_js = JS_NewFloat64(state.ctx, get_translated_y(-500));
  return top_js;
}

static JSValue js_engine_get_screen_bottom(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv)
{
  JSValue bottom_js = JS_NewFloat64(state.ctx, get_translated_y(500));
  return bottom_js;
}

static void fetch_engine_load_text_callback(const sfetch_response_t *);
static void fetch_engine_load_texture_callback(const sfetch_response_t *);
static void fetch_engine_load_sound_callback(const sfetch_response_t *);
static void fetch_engine_load_font_callback(const sfetch_response_t *);

static JSValue js_engine_set_texture(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
  JSValueConst texture_id_js;
  texture_id_js = argv[0];
  int texture_id;

  JS_ToInt32(ctx, &texture_id, texture_id_js);

  state.current_texture = texture_id;

  sgl_texture(state.textures[texture_id]);

  return JS_UNDEFINED;
}

void engine_set_texture(int texture)
{
  sgl_texture(state.textures[texture]);
}

static JSValue js_engine_load_text(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
  const char *filename;

  JSValueConst func;

  load_callback *new_callback;

  func = argv[1];

  if (!JS_IsFunction(ctx, func))
    return JS_ThrowTypeError(ctx, "You need a onLoaded handler!");

  new_callback = js_mallocz(ctx, sizeof(*new_callback));
  new_callback->callback = JS_DupValue(ctx, func);

  filename = JS_ToCString(ctx, argv[0]);

  SOKOL_LOG("LOAD TEXT");
  // SOKOL_LOG(filename);

  char *buffer = malloc(FETCH_BUFFER_SIZE);
  char path_buf[512];

  sfetch_send(&(sfetch_request_t){
      .path = fileutil_get_path(filename, path_buf, sizeof(path_buf)),
      .callback = fetch_engine_load_text_callback,
      .buffer = {
          .ptr = buffer,
          .size = FETCH_BUFFER_SIZE},
      .user_data = {.ptr = new_callback, .size = sizeof(*new_callback)}});

  return JS_UNDEFINED;
}

static JSValue js_engine_load_texture(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
  const char *filename;

  JSValueConst func;

  load_callback *new_callback;

  func = argv[1];

  if (!JS_IsFunction(ctx, func))
    return JS_ThrowTypeError(ctx, "You need a onLoaded handler!");

  new_callback = js_mallocz(ctx, sizeof(*new_callback));
  new_callback->callback = JS_DupValue(ctx, func);
  new_callback->parameter = JS_DupValue(ctx, argv[0]);

  filename = JS_ToCString(ctx, argv[0]);

  SOKOL_LOG("LOAD TEXTURE");
  // SOKOL_LOG(filename);

  char *buffer = malloc(FETCH_BUFFER_SIZE);
  char path_buf[512];

  sfetch_send(&(sfetch_request_t){
      .path = fileutil_get_path(filename, path_buf, sizeof(path_buf)),
      .callback = fetch_engine_load_texture_callback,
      .buffer = {
          .ptr = buffer,
          .size = FETCH_BUFFER_SIZE},
      .user_data = {.ptr = new_callback, .size = sizeof(*new_callback)}});

  return JS_UNDEFINED;
}

static JSValue js_engine_load_sound(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
  const char *filename;

  JSValueConst func;

  load_callback *new_callback;

  func = argv[1];

  if (!JS_IsFunction(ctx, func))
    return JS_ThrowTypeError(ctx, "You need a onLoaded handler!");

  new_callback = js_mallocz(ctx, sizeof(*new_callback));
  new_callback->callback = JS_DupValue(ctx, func);

  filename = JS_ToCString(ctx, argv[0]);

  SOKOL_LOG("LOAD SOUND");
  // SOKOL_LOG(filename);

  char *buffer = malloc(FETCH_BUFFER_SIZE);
  char path_buf[512];

  sfetch_send(&(sfetch_request_t){
      .path = fileutil_get_path(filename, path_buf, sizeof(path_buf)),
      .callback = fetch_engine_load_sound_callback,
      .buffer = {
          .ptr = buffer,
          .size = FETCH_BUFFER_SIZE},
      .user_data = {.ptr = new_callback, .size = sizeof(*new_callback)}});

  return JS_UNDEFINED;
}

static JSValue js_engine_load_font(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
  const char *filename;

  JSValueConst func;

  load_callback *new_callback;

  func = argv[1];

  if (!JS_IsFunction(ctx, func))
    return JS_ThrowTypeError(ctx, "You need a onLoaded handler!");

  new_callback = js_mallocz(ctx, sizeof(*new_callback));
  new_callback->callback = JS_DupValue(ctx, func);
  new_callback->parameter = JS_DupValue(ctx, argv[0]);
  filename = JS_ToCString(ctx, argv[0]);

  SOKOL_LOG("LOAD FONT started");
  // SOKOL_LOG(filename);

  char *buffer = malloc(FETCH_BUFFER_SIZE);
  char path_buf[512];

  sfetch_send(&(sfetch_request_t){
      .path = fileutil_get_path(filename, path_buf, sizeof(path_buf)),
      .callback = fetch_engine_load_font_callback,
      .buffer = {
          .ptr = buffer,
          .size = FETCH_BUFFER_SIZE},
      .user_data = {.ptr = new_callback, .size = sizeof(*new_callback)}});

  return JS_UNDEFINED;
}

static JSValue js_engine_set_resize_callback(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv)
{
  JSValueConst func;

  func = argv[0];

  if (!JS_IsFunction(ctx, func))
    return JS_ThrowTypeError(ctx, "You need a callback function!");

  state.resize_callback = JS_DupValue(ctx, func);

  return JS_UNDEFINED;
}

static JSValue js_engine_set_frame_callback(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv)
{
  JSValueConst func;

  func = argv[0];

  if (!JS_IsFunction(ctx, func))
    return JS_ThrowTypeError(ctx, "You need a callback function!");

  SOKOL_LOG("setting frame callback!");

  state.frame_callback = JS_DupValue(ctx, func);

  return JS_UNDEFINED;
}

static JSValue js_engine_set_keydown_callback(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv)
{
  JSValueConst func = argv[0];
  if (!JS_IsFunction(ctx, func))
    return JS_ThrowTypeError(ctx, "You need a callback function!");
  state.keydown_callback = JS_DupValue(ctx, func);
  return JS_UNDEFINED;
}

static JSValue js_engine_set_keyup_callback(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv)
{
  JSValueConst func = argv[0];
  if (!JS_IsFunction(ctx, func))
    return JS_ThrowTypeError(ctx, "You need a callback function!");
  state.keyup_callback = JS_DupValue(ctx, func);
  return JS_UNDEFINED;
}

static JSValue js_engine_set_mousedown_callback(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv)
{
  JSValueConst func = argv[0];
  if (!JS_IsFunction(ctx, func))
    return JS_ThrowTypeError(ctx, "You need a callback function!");
  state.mousedown_callback = JS_DupValue(ctx, func);
  return JS_UNDEFINED;
}

static JSValue js_engine_set_mouseup_callback(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv)
{
  JSValueConst func = argv[0];
  if (!JS_IsFunction(ctx, func))
    return JS_ThrowTypeError(ctx, "You need a callback function!");
  state.mouseup_callback = JS_DupValue(ctx, func);
  return JS_UNDEFINED;
}

static JSValue js_engine_set_mousemove_callback(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv)
{
  JSValueConst func = argv[0];
  if (!JS_IsFunction(ctx, func))
    return JS_ThrowTypeError(ctx, "You need a callback function!");
  state.mousemove_callback = JS_DupValue(ctx, func);
  return JS_UNDEFINED;
}

static JSValue js_engine_set_touchstart_callback(JSContext *ctx, JSValueConst this_val,
                                                 int argc, JSValueConst *argv)
{
  JSValueConst func = argv[0];
  if (!JS_IsFunction(ctx, func))
    return JS_ThrowTypeError(ctx, "You need a callback function!");
  state.touchstart_callback = JS_DupValue(ctx, func);
  return JS_UNDEFINED;
}

static JSValue js_engine_set_touchend_callback(JSContext *ctx, JSValueConst this_val,
                                               int argc, JSValueConst *argv)
{
  JSValueConst func = argv[0];
  if (!JS_IsFunction(ctx, func))
    return JS_ThrowTypeError(ctx, "You need a callback function!");
  state.touchend_callback = JS_DupValue(ctx, func);
  return JS_UNDEFINED;
}

static JSValue js_engine_set_touchmove_callback(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv)
{
  JSValueConst func = argv[0];
  if (!JS_IsFunction(ctx, func))
    return JS_ThrowTypeError(ctx, "You need a callback function!");
  state.touchmove_callback = JS_DupValue(ctx, func);
  return JS_UNDEFINED;
}

static JSValue js_engine_draw_texture(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
  JSValueConst x, y, anchor_x, anchor_y, scale, alpha, rotation;

  x = argv[0];
  y = argv[1];
  anchor_x = argv[2];
  anchor_y = argv[3];
  rotation = argv[4];
  scale = argv[5];
  alpha = argv[6];

  static float angle_deg = 0.0f;
  // float scale_real = 1.0f + sinf(sgl_rad(angle_deg)) * 0.5f;
  double scale_real;
  double x_real;
  double y_real;
  double anchor_x_real;
  double anchor_y_real;
  double alpha_real;
  double rotation_real;

  double topLeftX, topLeftY, bottomLeftX, bottomLeftY, topRightX, topRightY, bottomRightX, bottomRightY;
  double topLeftXRot, topLeftYRot, bottomLeftXRot, bottomLeftYRot, topRightXRot, topRightYRot, bottomRightXRot, bottomRightYRot;

  double widthSource = state.texture_sizes[state.current_texture].widthSource;
  double heightSource = state.texture_sizes[state.current_texture].heightSource;

  double widthTexture = state.texture_sizes[state.current_texture].widthTexture;
  double heightTexture = state.texture_sizes[state.current_texture].heightTexture;

  if (widthSource == 0)
  {
    return JS_UNDEFINED;
  }

  double uv_x = widthSource / widthTexture;
  double uv_y = heightSource / heightTexture;

  // SOKOL_LOG("%d %d %d %d", widthSource, heightSource, widthTexture, heightTexture);

  JS_ToFloat64(ctx, &scale_real, scale);
  JS_ToFloat64(ctx, &x_real, x);
  JS_ToFloat64(ctx, &y_real, y);
  JS_ToFloat64(ctx, &anchor_x_real, anchor_x);
  JS_ToFloat64(ctx, &anchor_y_real, anchor_y);
  JS_ToFloat64(ctx, &alpha_real, alpha);
  JS_ToFloat64(ctx, &rotation_real, rotation);

  x_real = x_real * 0.002;
  y_real = -y_real * 0.002;

  angle_deg += 1.0f;

  double width = widthSource * 0.001;
  double height = heightSource * 0.001;

  /*sgl_rotate(sgl_rad(angle_deg), 0.0f, 0.0f, 1.0f);
  sgl_scale(scale_real, scale_real, 1.0f);
  sgl_translate(x_real, y_real, 1.0f);*/

  topLeftX = -scale_real * width - (scale_real * width * anchor_x_real);
  topLeftY = -scale_real * height - (scale_real * height * anchor_y_real);

  topRightX = scale_real * width - (scale_real * width * anchor_x_real);
  topRightY = -scale_real * height - (scale_real * height * anchor_y_real);

  bottomLeftX = scale_real * width - (scale_real * width * anchor_x_real);
  bottomLeftY = scale_real * height - (scale_real * height * anchor_y_real);

  bottomRightX = -scale_real * width - (scale_real * width * anchor_x_real);
  bottomRightY = scale_real * height - (scale_real * height * anchor_y_real);

  if (rotation_real == 0)
  {
    sgl_begin_quads();
    sgl_c4f(alpha_real, alpha_real, alpha_real, alpha_real);
    sgl_v2f_t2f(topLeftX + x_real, topLeftY + y_real, 0, uv_y);
    sgl_v2f_t2f(topRightX + x_real, topRightY + y_real, uv_x, uv_y);
    sgl_v2f_t2f(bottomLeftX + x_real, bottomLeftY + y_real, uv_x, 0);
    sgl_v2f_t2f(bottomRightX + x_real, bottomRightY + y_real, 0, 0);
    sgl_end();
  }
  else
  {
    double s = sinf(-rotation_real);
    double c = cosf(-rotation_real);
    topLeftXRot = topLeftX * c - topLeftY * s;
    topLeftYRot = topLeftX * s + topLeftY * c;
    topRightXRot = topRightX * c - topRightY * s;
    topRightYRot = topRightX * s + topRightY * c;

    bottomLeftXRot = bottomLeftX * c - bottomLeftY * s;
    bottomLeftYRot = bottomLeftX * s + bottomLeftY * c;
    bottomRightXRot = bottomRightX * c - bottomRightY * s;
    bottomRightYRot = bottomRightX * s + bottomRightY * c;

    sgl_begin_quads();
    sgl_c4f(alpha_real, alpha_real, alpha_real, alpha_real);
    sgl_v2f_t2f((topLeftXRot) + x_real, (topLeftYRot) + y_real, 0, 1);
    sgl_v2f_t2f((topRightXRot) + x_real, (topRightYRot) + y_real, 1, 1);
    sgl_v2f_t2f((bottomLeftXRot) + x_real, (bottomLeftYRot) + y_real, 1, 0);
    sgl_v2f_t2f((bottomRightXRot) + x_real, (bottomRightYRot) + y_real, 0, 0);
    sgl_end();
  }

  return JS_UNDEFINED;
}

static JSValue js_engine_draw_texture_clip(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv)
{
  double source_x;
  double source_y;
  double source_width;
  double source_height;
  double x;
  double y;
  double anchor_x;
  double anchor_y;
  double rotation;
  double scale;
  double alpha;

  double topLeftX, topLeftY, bottomLeftX, bottomLeftY, topRightX, topRightY, bottomRightX, bottomRightY;
  double topLeftXRot, topLeftYRot, bottomLeftXRot, bottomLeftYRot, topRightXRot, topRightYRot, bottomRightXRot, bottomRightYRot;

  double widthSource = state.texture_sizes[state.current_texture].widthSource;
  double heightSource = state.texture_sizes[state.current_texture].heightSource;

  double widthTexture = state.texture_sizes[state.current_texture].widthTexture;
  double heightTexture = state.texture_sizes[state.current_texture].heightTexture;

  if (widthSource == 0)
  {
    return JS_UNDEFINED;
  }

  JS_ToFloat64(ctx, &source_x, argv[0]);
  JS_ToFloat64(ctx, &source_y, argv[1]);
  JS_ToFloat64(ctx, &source_width, argv[2]);
  JS_ToFloat64(ctx, &source_height, argv[3]);
  JS_ToFloat64(ctx, &x, argv[4]);
  JS_ToFloat64(ctx, &y, argv[5]);
  JS_ToFloat64(ctx, &anchor_x, argv[6]);
  JS_ToFloat64(ctx, &anchor_y, argv[7]);
  JS_ToFloat64(ctx, &rotation, argv[8]);
  JS_ToFloat64(ctx, &scale, argv[9]);
  JS_ToFloat64(ctx, &alpha, argv[10]);

  // SOKOL_LOG("%d %d %d %d", source_x, source_y, (int)source_width, source_height);

  double uv_left = source_x / widthTexture;
  double uv_top = source_y / heightTexture;

  double uv_right = uv_left + source_width / widthTexture;
  double uv_bottom = uv_top + source_height / heightTexture;

  x = x * 0.002;
  y = -y * 0.002;

  double width = source_width * 0.001;
  double height = source_height * 0.001;

  /*sgl_rotate(sgl_rad(angle_deg), 0.0f, 0.0f, 1.0f);
  sgl_scale(scale, scale, 1.0f);
  sgl_translate(x, y, 1.0f);*/

  topLeftX = -scale * width - (scale * width * anchor_x);
  topLeftY = -scale * height - (scale * height * anchor_y);

  topRightX = scale * width - (scale * width * anchor_x);
  topRightY = -scale * height - (scale * height * anchor_y);

  bottomLeftX = scale * width - (scale * width * anchor_x);
  bottomLeftY = scale * height - (scale * height * anchor_y);

  bottomRightX = -scale * width - (scale * width * anchor_x);
  bottomRightY = scale * height - (scale * height * anchor_y);

  if (rotation == 0)
  {

    sgl_begin_quads();
    sgl_c4f(alpha, alpha, alpha, alpha);
    sgl_v2f_t2f(topLeftX + x, topLeftY + y, uv_left, uv_bottom);
    sgl_v2f_t2f(topRightX + x, topRightY + y, uv_right, uv_bottom);
    sgl_v2f_t2f(bottomLeftX + x, bottomLeftY + y, uv_right, uv_top);
    sgl_v2f_t2f(bottomRightX + x, bottomRightY + y, uv_left, uv_top);
    sgl_end();
  }
  else
  {
    double s = sinf(-rotation);
    double c = cosf(-rotation);
    topLeftXRot = topLeftX * c - topLeftY * s;
    topLeftYRot = topLeftX * s + topLeftY * c;
    topRightXRot = topRightX * c - topRightY * s;
    topRightYRot = topRightX * s + topRightY * c;

    bottomLeftXRot = bottomLeftX * c - bottomLeftY * s;
    bottomLeftYRot = bottomLeftX * s + bottomLeftY * c;
    bottomRightXRot = bottomRightX * c - bottomRightY * s;
    bottomRightYRot = bottomRightX * s + bottomRightY * c;

    sgl_begin_quads();
    sgl_c4f(alpha, alpha, alpha, alpha);
    sgl_v2f_t2f((topLeftXRot) + x, (topLeftYRot) + y, uv_left, uv_bottom);
    sgl_v2f_t2f((topRightXRot) + x, (topRightYRot) + y, uv_right, uv_bottom);
    sgl_v2f_t2f((bottomLeftXRot) + x, (bottomLeftYRot) + y, uv_right, uv_top);
    sgl_v2f_t2f((bottomRightXRot) + x, (bottomRightYRot) + y, uv_left, uv_top);
    sgl_end();
  }

  return JS_UNDEFINED;
}

void engine_draw_texture_clip(double source_x, double source_y, double source_width, double source_height, double x, double y, double anchor_x, double anchor_y, double rotation, double scale, double alpha)
{
  double topLeftX, topLeftY, bottomLeftX, bottomLeftY, topRightX, topRightY, bottomRightX, bottomRightY;
  double topLeftXRot, topLeftYRot, bottomLeftXRot, bottomLeftYRot, topRightXRot, topRightYRot, bottomRightXRot, bottomRightYRot;

  double widthSource = state.texture_sizes[state.current_texture].widthSource;
  double heightSource = state.texture_sizes[state.current_texture].heightSource;

  double widthTexture = state.texture_sizes[state.current_texture].widthTexture;
  double heightTexture = state.texture_sizes[state.current_texture].heightTexture;

  if (widthSource == 0)
  {
    return;
  }

  // SOKOL_LOG("%d %d %d %d", source_x, source_y, (int)source_width, source_height);

  double uv_left = source_x / widthTexture;
  double uv_top = source_y / heightTexture;

  double uv_right = uv_left + source_width / widthTexture;
  double uv_bottom = uv_top + source_height / heightTexture;

  x = x * 0.002;
  y = -y * 0.002;

  double width = source_width * 0.001;
  double height = source_height * 0.001;

  /*sgl_rotate(sgl_rad(angle_deg), 0.0f, 0.0f, 1.0f);
  sgl_scale(scale, scale, 1.0f);
  sgl_translate(x, y, 1.0f);*/

  topLeftX = -scale * width - (scale * width * anchor_x);
  topLeftY = -scale * height - (scale * height * anchor_y);

  topRightX = scale * width - (scale * width * anchor_x);
  topRightY = -scale * height - (scale * height * anchor_y);

  bottomLeftX = scale * width - (scale * width * anchor_x);
  bottomLeftY = scale * height - (scale * height * anchor_y);

  bottomRightX = -scale * width - (scale * width * anchor_x);
  bottomRightY = scale * height - (scale * height * anchor_y);

  if (rotation == 0)
  {
    sgl_begin_quads();
    sgl_c4f(alpha, alpha, alpha, alpha);
    sgl_v2f_t2f(topLeftX + x, topLeftY + y, uv_left, uv_bottom);
    sgl_v2f_t2f(topRightX + x, topRightY + y, uv_right, uv_bottom);
    sgl_v2f_t2f(bottomLeftX + x, bottomLeftY + y, uv_right, uv_top);
    sgl_v2f_t2f(bottomRightX + x, bottomRightY + y, uv_left, uv_top);
    sgl_end();
  }
  else
  {
    double s = sinf(-rotation);
    double c = cosf(-rotation);
    topLeftXRot = topLeftX * c - topLeftY * s;
    topLeftYRot = topLeftX * s + topLeftY * c;
    topRightXRot = topRightX * c - topRightY * s;
    topRightYRot = topRightX * s + topRightY * c;

    bottomLeftXRot = bottomLeftX * c - bottomLeftY * s;
    bottomLeftYRot = bottomLeftX * s + bottomLeftY * c;
    bottomRightXRot = bottomRightX * c - bottomRightY * s;
    bottomRightYRot = bottomRightX * s + bottomRightY * c;

    sgl_begin_quads();
    sgl_c4f(alpha, alpha, alpha, alpha);
    sgl_v2f_t2f((topLeftXRot) + x, (topLeftYRot) + y, uv_left, uv_bottom);
    sgl_v2f_t2f((topRightXRot) + x, (topRightYRot) + y, uv_right, uv_bottom);
    sgl_v2f_t2f((bottomLeftXRot) + x, (bottomLeftYRot) + y, uv_right, uv_top);
    sgl_v2f_t2f((bottomRightXRot) + x, (bottomRightYRot) + y, uv_left, uv_top);
    sgl_end();
  }
}

static JSValue js_engine_draw_textured_triangle(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv)
{
  double x1, y1, x2, y2, x3, y3, alpha;

  JS_ToFloat64(ctx, &x1, argv[0]);
  JS_ToFloat64(ctx, &y1, argv[1]);
  JS_ToFloat64(ctx, &x2, argv[2]);
  JS_ToFloat64(ctx, &y2, argv[3]);
  JS_ToFloat64(ctx, &x3, argv[4]);
  JS_ToFloat64(ctx, &y3, argv[5]);
  JS_ToFloat64(ctx, &alpha, argv[6]);

  x1 = x1 * 0.002;
  y1 = -y1 * 0.002;
  x2 = x2 * 0.002;
  y2 = -y2 * 0.002;
  x3 = x3 * 0.002;
  y3 = -y3 * 0.002;

  double widthSource = state.texture_sizes[state.current_texture].widthSource;
  double heightSource = state.texture_sizes[state.current_texture].heightSource;

  double widthTexture = state.texture_sizes[state.current_texture].widthTexture;
  double heightTexture = state.texture_sizes[state.current_texture].heightTexture;

  double uv_x = widthSource / widthTexture;
  double uv_y = heightSource / heightTexture;

  sgl_begin_triangles();
  sgl_c4f(alpha, alpha, alpha, alpha);

  sgl_v2f_t2f(x1, y1, 0, uv_y);
  sgl_v2f_t2f(x2, y2, uv_x, uv_y);
  sgl_v2f_t2f(x3, y3, 0, 0);
  sgl_end();
  return JS_UNDEFINED;
}

static JSValue js_engine_draw_triangle(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
  double x1, y1, x2, y2, x3, y3, r, g, b, a;

  JS_ToFloat64(ctx, &x1, argv[0]);
  JS_ToFloat64(ctx, &y1, argv[1]);
  JS_ToFloat64(ctx, &x2, argv[2]);
  JS_ToFloat64(ctx, &y2, argv[3]);
  JS_ToFloat64(ctx, &x3, argv[4]);
  JS_ToFloat64(ctx, &y3, argv[5]);
  JS_ToFloat64(ctx, &r, argv[6]);
  JS_ToFloat64(ctx, &g, argv[7]);
  JS_ToFloat64(ctx, &b, argv[8]);
  JS_ToFloat64(ctx, &a, argv[9]);

  x1 = x1 * 0.002;
  y1 = -y1 * 0.002;
  x2 = x2 * 0.002;
  y2 = -y2 * 0.002;
  x3 = x3 * 0.002;
  y3 = -y3 * 0.002;

  double widthSource = state.texture_sizes[state.current_texture].widthSource;
  double heightSource = state.texture_sizes[state.current_texture].heightSource;

  double widthTexture = state.texture_sizes[state.current_texture].widthTexture;
  double heightTexture = state.texture_sizes[state.current_texture].heightTexture;

  double uv_x = widthSource / widthTexture;
  double uv_y = heightSource / heightTexture;

  sgl_begin_triangles();
  sgl_c4f(r, g, b, a);

  sgl_v2f(x1, y1);
  sgl_v2f(x2, y2);
  sgl_v2f(x3, y3);
  sgl_end();
  return JS_UNDEFINED;
}

static JSValue js_engine_set_clear_color(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
  double red, green, blue, alpha;

  JS_ToFloat64(ctx, &red, argv[0]);
  JS_ToFloat64(ctx, &green, argv[1]);
  JS_ToFloat64(ctx, &blue, argv[2]);
  JS_ToFloat64(ctx, &alpha, argv[3]);

  state.pass_action = (sg_pass_action){
      .colors[0] = {.action = SG_ACTION_CLEAR, .value = {red, green, blue, alpha}}};

  return JS_UNDEFINED;
}

static JSValue js_engine_graphics_begin_path(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv)
{
  nvgBeginPath(state.vg);
  return JS_UNDEFINED;
}

static JSValue js_engine_graphics_close_path(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv)
{
  nvgClosePath(state.vg);
  return JS_UNDEFINED;
}

static JSValue js_engine_graphics_rounded_rect(JSContext *ctx, JSValueConst this_val,
                                               int argc, JSValueConst *argv)
{
  double x, y, width, height, radius;

  JS_ToFloat64(ctx, &x, argv[0]);
  JS_ToFloat64(ctx, &y, argv[1]);
  JS_ToFloat64(ctx, &width, argv[2]);
  JS_ToFloat64(ctx, &height, argv[3]);
  JS_ToFloat64(ctx, &radius, argv[4]);

  /*x = convert_local_x_to_screen(x);
  y = convert_local_y_to_screen(y);
  width = scale_local_to_screen(width);
  height = scale_local_to_screen(height);
  radius = scale_local_to_screen(radius);*/

  nvgRoundedRect(state.vg, x, y, width, height, radius);
  return JS_UNDEFINED;
}

static JSValue js_engine_graphics_rect(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
  double x, y, width, height, radius;

  JS_ToFloat64(ctx, &x, argv[0]);
  JS_ToFloat64(ctx, &y, argv[1]);
  JS_ToFloat64(ctx, &width, argv[2]);
  JS_ToFloat64(ctx, &height, argv[3]);

  x = convert_local_x_to_screen(x);
  y = convert_local_y_to_screen(y);
  width = scale_local_to_screen(width);
  height = scale_local_to_screen(height);

  nvgRect(state.vg, x, y, width, height);
  return JS_UNDEFINED;
}

static JSValue js_engine_graphics_polygon(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv)
{

  JSValue coords = argv[0];
  JSValue jsLen = JS_GetPropertyStr(ctx, coords, "length");
  int arrayLen;
  JS_ToInt32(ctx, &arrayLen, jsLen);

  JSValue *coordsArray = JS_GetFastArray(ctx, coords);
  JSValue *startCoords = JS_GetFastArray(ctx, coordsArray[0]);

  double x, y, startX, startY;

  JS_ToFloat64(ctx, &startX, startCoords[0]);
  JS_ToFloat64(ctx, &startY, startCoords[1]);
  startX = convert_local_x_to_screen(startX);
  startY = convert_local_y_to_screen(startY);

  nvgMoveTo(state.vg, startX, startY);
  for (int i = 1; i < arrayLen; i++)
  {
    JSValue *point = JS_GetFastArray(ctx, coordsArray[i]);

    JS_ToFloat64(ctx, &x, point[0]);
    JS_ToFloat64(ctx, &y, point[1]);
    x = convert_local_x_to_screen(x);
    y = convert_local_y_to_screen(y);

    // SOKOL_LOG("drawing polygon point %f %f", x, y);

    nvgLineTo(state.vg, x, y);
  }

  nvgLineTo(state.vg, startX, startY);

  return JS_UNDEFINED;
}

static JSValue js_engine_graphics_fill_color(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv)
{
  JSValueConst red, green, blue, alpha;

  red = argv[0];
  green = argv[1];
  blue = argv[2];
  alpha = argv[3];

  double red_real, green_real, blue_real, alpha_real;

  JS_ToFloat64(ctx, &red_real, red);
  JS_ToFloat64(ctx, &green_real, green);
  JS_ToFloat64(ctx, &blue_real, blue);
  JS_ToFloat64(ctx, &alpha_real, alpha);

  NVGcolor color = nvgRGBAf(red_real, green_real, blue_real, alpha_real);
  nvgFillColor(state.vg, color);
  return JS_UNDEFINED;
}

static JSValue js_engine_graphics_fill(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
  nvgFill(state.vg);
  return JS_UNDEFINED;
}

static JSValue js_engine_graphics_hole(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
  nvgPathWinding(state.vg, NVG_HOLE); // Mark circle as a hole.
  return JS_UNDEFINED;
}

static JSValue js_engine_graphics_solid(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv)
{
  nvgPathWinding(state.vg, NVG_SOLID); // Mark circle as a hole.
  return JS_UNDEFINED;
}

static JSValue js_engine_graphics_radial_gradient(JSContext *ctx, JSValueConst this_val,
                                                  int argc, JSValueConst *argv)
{
  JSValueConst cx, cy, inr, outr, first_red, first_green, first_blue, first_alpha, second_red, second_green, second_blue, second_alpha;

  cx = argv[0];
  cy = argv[1];
  inr = argv[2];
  outr = argv[3];
  first_red = argv[4];
  first_green = argv[5];
  first_blue = argv[6];
  first_alpha = argv[7];
  second_red = argv[8];
  second_green = argv[9];
  second_blue = argv[10];
  second_alpha = argv[11];

  double real_cx, real_cy, real_inr, real_outr, real_first_red, real_first_green, real_first_blue, real_first_alpha, real_second_red, real_second_green, real_second_blue, real_second_alpha;

  JS_ToFloat64(ctx, &real_cx, cx);
  JS_ToFloat64(ctx, &real_cy, cy);
  JS_ToFloat64(ctx, &real_inr, inr);
  JS_ToFloat64(ctx, &real_outr, outr);
  JS_ToFloat64(ctx, &real_first_red, first_red);
  JS_ToFloat64(ctx, &real_first_green, first_green);
  JS_ToFloat64(ctx, &real_first_blue, first_blue);
  JS_ToFloat64(ctx, &real_first_alpha, first_alpha);

  JS_ToFloat64(ctx, &real_second_red, second_red);
  JS_ToFloat64(ctx, &real_second_green, second_green);
  JS_ToFloat64(ctx, &real_second_blue, second_blue);
  JS_ToFloat64(ctx, &real_second_alpha, second_alpha);

  real_cx = convert_local_x_to_screen(real_cx);
  real_cy = convert_local_y_to_screen(real_cy);
  real_inr = scale_local_to_screen(real_inr);
  real_outr = scale_local_to_screen(real_outr);

  NVGcolor first_color = nvgRGBAf(real_first_red, real_first_green, real_first_blue, real_first_alpha);
  NVGcolor second_color = nvgRGBAf(real_second_red, real_second_green, real_second_blue, real_second_alpha);

  NVGpaint gradient = nvgRadialGradient(state.vg, real_cx, real_cy, real_inr, real_outr, first_color, second_color);

  nvgFillPaint(state.vg, gradient);

  return JS_UNDEFINED;
}

static JSValue js_engine_graphics_stroke_color(JSContext *ctx, JSValueConst this_val,
                                               int argc, JSValueConst *argv)
{
  JSValueConst red, green, blue, alpha;

  red = argv[0];
  green = argv[1];
  blue = argv[2];
  alpha = argv[3];

  double red_real, green_real, blue_real, alpha_real;

  JS_ToFloat64(ctx, &red_real, red);
  JS_ToFloat64(ctx, &green_real, green);
  JS_ToFloat64(ctx, &blue_real, blue);
  JS_ToFloat64(ctx, &alpha_real, alpha);

  NVGcolor color = nvgRGBAf(red_real, green_real, blue_real, alpha_real);
  nvgStrokeColor(state.vg, color);
  return JS_UNDEFINED;
}

static JSValue js_engine_graphics_stroke_width(JSContext *ctx, JSValueConst this_val,
                                               int argc, JSValueConst *argv)
{
  JSValueConst width;

  width = argv[0];

  double width_real;

  JS_ToFloat64(ctx, &width_real, width);

  nvgStrokeWidth(state.vg, width_real);
  return JS_UNDEFINED;
}

static JSValue js_engine_graphics_stroke(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
  nvgStroke(state.vg);
  return JS_UNDEFINED;
}

static JSValue js_engine_graphics_font_size(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv)
{
  double size_real;
  JS_ToFloat64(ctx, &size_real, argv[0]);
  size_real = scale_local_to_screen(size_real);
  nvgFontSize(state.vg, size_real);
  return JS_UNDEFINED;
}

static JSValue js_engine_graphics_font_face(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv)
{
  const char *font_face;
  font_face = JS_ToCString(ctx, argv[0]);
  nvgFontFace(state.vg, font_face);
  return JS_UNDEFINED;
}

static JSValue js_engine_graphics_text_align(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv)
{
  int align = 0;
  JS_ToInt32(ctx, &align, argv[0]);
  nvgTextAlign(state.vg, align);
  return JS_UNDEFINED;
}

static JSValue js_engine_graphics_text(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{

  double x, y;
  const char *text;

  JS_ToFloat64(ctx, &x, argv[0]);
  JS_ToFloat64(ctx, &y, argv[1]);

  x = convert_local_x_to_screen(x);
  y = convert_local_y_to_screen(y);

  // SOKOL_LOG("screen y is %f %d", y, sapp_height());

  text = JS_ToCString(ctx, argv[2]);

  // SOKOL_LOG("what the %d %d %s", x, y, text);

  nvgText(state.vg, x, y, text, NULL);

  return JS_UNDEFINED;
}

static JSValue js_engine_graphics_text_box(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv)
{
  double x, y, row_width;
  const char *text;

  JS_ToFloat64(ctx, &x, argv[0]);
  JS_ToFloat64(ctx, &y, argv[1]);
  JS_ToFloat64(ctx, &row_width, argv[2]);

  x = convert_local_x_to_screen(x);
  y = convert_local_y_to_screen(y);

  text = JS_ToCString(ctx, argv[3]);

  nvgTextBox(state.vg, x, y, row_width, text, NULL);
  return JS_UNDEFINED;
}

static JSValue js_engine_flush_rendering(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
  /*nvgEndFrame(state.vg);
  sg_reset_state_cache();

  float width = sapp_width();
  float height = sapp_height();
  float ratio = width / height;
  nvgBeginFrame(state.vg, width, height, ratio);*/

  sgl_draw();
  __dbgui_draw();
  sg_end_pass();
  sg_commit();

  if (state.vg != NULL)
  {
    nvgEndFrame(state.vg);
    sg_reset_state_cache();
  }

  float width = sapp_width();
  float height = sapp_height();
  float ratio = width / height;

  if (state.vg != NULL)
  {
    nvgBeginFrame(state.vg, width, height, sapp_dpi_scale());
  }

  sgl_defaults();
  sgl_load_pipeline(state.pip);

  set_viewport();

  sgl_enable_texture();

  sg_begin_default_pass(&state.pass_action_no_clear, sapp_width(), sapp_height());
  return JS_UNDEFINED;
}

static JSValue js_engine_play_sound(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
  JSValueConst sound, volume, pitch;

  sound = argv[0];
  volume = argv[1];
  pitch = argv[2];

  int sound_real;
  double volume_real;
  double pitch_real;

  JS_ToInt32(ctx, &sound_real, sound);
  JS_ToFloat64(ctx, &volume_real, volume);
  JS_ToFloat64(ctx, &pitch_real, pitch);

  SOKOL_LOG("trying to play sound");

  int sound_handle = Soloud_play(state.soloud, state.sounds[sound_real]);

  Soloud_setVolume(state.soloud, sound_handle, volume_real);
  Soloud_setRelativePlaySpeed(state.soloud, sound_handle, pitch_real);

  return JS_UNDEFINED;
}

static void fetch_engine_load_text_callback(const sfetch_response_t *response)
{
  if (response->fetched)
  {
    /* the file data has been fetched, since we provided a big-enough
           buffer we can be sure that all data has been loaded here
        */

    SOKOL_LOG("text loaded");

    load_callback *callback = (load_callback *)response->user_data;

    JSValue text = JS_NewStringLen(state.ctx, response->data.ptr, (int)response->data.size);

    JS_Call(state.ctx, callback->callback, JS_UNDEFINED, 1, (JSValueConst *)&text);
  }
  else if (response->failed)
  {

    SOKOL_LOG("load text error");

    // if loading the file failed, set clear color to red
  }
  free(response->data.ptr);
}

static void fetch_engine_load_texture_callback(const sfetch_response_t *response)
{
  if (response->fetched)
  {
    /* the file data has been fetched, since we provided a big-enough
           buffer we can be sure that all data has been loaded here
        */

    SOKOL_LOG("loadTexture texture loaded");

    int png_width, png_height, num_channels, powerWidth, powerHeight;
    powerWidth = 0;
    powerHeight = 0;
    const int desired_channels = 4;
    stbi_uc *pixels = stbi_load_from_memory(
        response->data.ptr,
        (int)response->data.size,
        &png_width, &png_height,
        &num_channels, desired_channels);

    // premultiply alpha
    for (int i = 0; i < png_width * png_height * 4; i += 4)
    {
      float alpha = ((float)pixels[i + 3] / (float)255);
      float red = (float)pixels[i] * alpha;
      float green = (float)pixels[i + 1] * alpha;
      float blue = (float)pixels[i + 2] * alpha;
      pixels[i] = (unsigned char)red;
      pixels[i + 1] = (unsigned char)green;
      pixels[i + 2] = (unsigned char)blue;
    }

    if (pixels)
    {
      powerWidth = png_width;
      powerHeight = png_height;
      /* ok, time to actually initialize the sokol-gfx texture */

      powerWidth--;
      powerWidth |= powerWidth >> 1;
      powerWidth |= powerWidth >> 2;
      powerWidth |= powerWidth >> 4;
      powerWidth |= powerWidth >> 8;
      powerWidth |= powerWidth >> 16;
      powerWidth++;

      powerHeight--;
      powerHeight |= powerHeight >> 1;
      powerHeight |= powerHeight >> 2;
      powerHeight |= powerHeight >> 4;
      powerHeight |= powerHeight >> 8;
      powerHeight |= powerHeight >> 16;
      powerHeight++;

      /*if (powerWidth > powerHeight)
      {
        powerHeight = powerWidth;
      }
      else
      {
        powerWidth = powerHeight;
      }*/

      // stbi_uc *resizedPixels = malloc(png_width * png_height * 4);
      // memset(resizedPixels, 0, png_width * png_height * 4);
      //  PoT to non-PoT
      // stbir_resize_uint8(pixels, png_width, png_height, 0, resizedPixels, png_width, png_height, 0, 4);

      // SOKOL_LOG("%d %d %d %d", png_width, png_height, powerWidth, powerHeight);

      state.texture_sizes[state.loaded_textures].widthSource = png_width;
      state.texture_sizes[state.loaded_textures].heightSource = png_height;
      state.texture_sizes[state.loaded_textures].widthTexture = png_width;
      state.texture_sizes[state.loaded_textures].heightTexture = png_height;

      sg_init_image(state.textures[state.loaded_textures], &(sg_image_desc){
                                                               .width = png_width,
                                                               .height = png_height,
                                                               .wrap_u = SG_WRAP_CLAMP_TO_BORDER,
                                                               .wrap_w = SG_WRAP_CLAMP_TO_BORDER,
                                                               .wrap_v = SG_WRAP_CLAMP_TO_BORDER,
                                                               .pixel_format = SG_PIXELFORMAT_RGBA8,
                                                               .min_filter = SG_FILTER_LINEAR,
                                                               .mag_filter = SG_FILTER_LINEAR,
                                                               .data.subimage[0][0] = {
                                                                   .ptr = pixels,
                                                                   .size = png_width * png_height * 4,
                                                               }});
      SOKOL_LOG("init done");

      stbi_image_free(pixels);
      // free(resizedPixels);
    }

    load_callback *callback = (load_callback *)response->user_data;

    JSValue texture = JS_NewObject(state.ctx);

    JSValue id = JS_NewInt32(state.ctx, state.loaded_textures);

    JSValue name = JS_NewString(state.ctx, response->path);
    JSValue widthPixels = JS_NewInt32(state.ctx, png_width);
    JSValue heightPixels = JS_NewInt32(state.ctx, png_height);

    JSValue width = JS_NewInt32(state.ctx, powerWidth);
    JSValue height = JS_NewInt32(state.ctx, powerHeight);

    JS_SetPropertyStr(state.ctx, texture, "id", id);
    JS_SetPropertyStr(state.ctx, texture, "name", callback->parameter);
    JS_SetPropertyStr(state.ctx, texture, "width", width);
    JS_SetPropertyStr(state.ctx, texture, "height", height);
    JS_SetPropertyStr(state.ctx, texture, "widthPixels", widthPixels);
    JS_SetPropertyStr(state.ctx, texture, "heightPixels", heightPixels);

    JS_Call(state.ctx, callback->callback, JS_UNDEFINED, 1, (JSValueConst *)&texture);
    // SOKOL_LOG(JS_ToCString(state.ctx, callback->callback));

    state.loaded_textures++;

    // JS_FreeValue(state.ctx, callback->callback);
    // js_free(state.ctx, callback);

    // printf();
  }
  else if (response->failed)
  {

    SOKOL_LOG("loadTexture texture error");

    // if loading the file failed, set clear color to red
  }
  free(response->data.ptr);
}

static void fetch_engine_load_sound_callback(const sfetch_response_t *response)
{
  if (response->fetched)
  {
    /* the file data has been fetched, since we provided a big-enough
           buffer we can be sure that all data has been loaded here
        */

    SOKOL_LOG("loadSound sound loaded size %d", response->data.size);

    state.sounds[state.loaded_sounds] = Wav_create();

    // int result = Wav_loadMem(state.sounds[state.loaded_sounds], response->data.ptr, response->data.size);

    int result = Wav_loadMemEx(state.sounds[state.loaded_sounds], response->data.ptr, (int)response->data.size, 1, 1);

    load_callback *callback = (load_callback *)response->user_data;

    JSValue sound = JS_NewObject(state.ctx);

    JSValue id = JS_NewInt32(state.ctx, state.loaded_sounds);

    JSValue name = JS_NewString(state.ctx, response->path);

    JS_SetPropertyStr(state.ctx, sound, "id", id);
    JS_SetPropertyStr(state.ctx, sound, "name", name);

    JS_Call(state.ctx, callback->callback, JS_UNDEFINED, 1, (JSValueConst *)&sound);

    state.loaded_sounds++;
  }
  else if (response->failed)
  {

    SOKOL_LOG("loadSound load error");

    // if loading the file failed, set clear color to red
  }
  free(response->data.ptr);
}

static void fetch_engine_load_font_callback(const sfetch_response_t *response)
{
  if (response->fetched)
  {
    /* the file data has been fetched, since we provided a big-enough
           buffer we can be sure that all data has been loaded here
        */

    SOKOL_LOG("loadfont font loaded");

    // make copy of font data and create nanovg font
    char *fontData = malloc((int)response->data.size);
    memcpy(fontData, response->data.ptr, (int)response->data.size);

    load_callback *callback = (load_callback *)response->user_data;

    char *name = JS_ToCString(state.ctx, callback->parameter);
    int font = nvgCreateFontMem(state.vg, name, fontData, (int)response->data.size, 0);

    JSValue fontObj = JS_NewObject(state.ctx);

    JSValue id = JS_NewInt32(state.ctx, font);

    JS_SetPropertyStr(state.ctx, fontObj, "id", id);
    JS_SetPropertyStr(state.ctx, fontObj, "name", callback->parameter);

    JS_Call(state.ctx, callback->callback, JS_UNDEFINED, 1, (JSValueConst *)&fontObj);
  }
  else if (response->failed)
  {

    SOKOL_LOG("loadFont load error");

    // if loading the file failed, set clear color to red
  }
  free(response->data.ptr);
}

static const JSCFunctionListEntry js_my_module_funcs[] = {
    JS_CFUNC_DEF("loadTexture", 2, js_engine_load_texture),
    JS_CFUNC_DEF("loadText", 2, js_engine_load_text),
    JS_CFUNC_DEF("setTexture", 1, js_engine_set_texture),
    JS_CFUNC_DEF("drawTexture", 7, js_engine_draw_texture),

    JS_CFUNC_DEF("drawTriangle", 10, js_engine_draw_triangle),

    JS_CFUNC_DEF("drawTexturedTriangle", 7, js_engine_draw_textured_triangle),

    JS_CFUNC_DEF("setClearColor", 4, js_engine_set_clear_color),

    JS_CFUNC_DEF("drawTextureClip", 11, js_engine_draw_texture_clip),

    JS_CFUNC_DEF("graphicsBeginPath", 0, js_engine_graphics_begin_path),
    JS_CFUNC_DEF("graphicsClosePath", 0, js_engine_graphics_close_path),

    JS_CFUNC_DEF("graphicsRoundedRect", 5, js_engine_graphics_rounded_rect),
    JS_CFUNC_DEF("graphicsRect", 4, js_engine_graphics_rect),
    JS_CFUNC_DEF("graphicsPolygon", 1, js_engine_graphics_polygon),
    JS_CFUNC_DEF("graphicsFillColor", 4, js_engine_graphics_fill_color),
    JS_CFUNC_DEF("graphicsFill", 0, js_engine_graphics_fill),

    JS_CFUNC_DEF("graphicsHole", 0, js_engine_graphics_hole),
    JS_CFUNC_DEF("graphicsSolid", 0, js_engine_graphics_solid),

    JS_CFUNC_DEF("graphicsRadialGradient", 12, js_engine_graphics_radial_gradient),

    JS_CFUNC_DEF("graphicsStrokeColor", 4, js_engine_graphics_stroke_color),
    JS_CFUNC_DEF("graphicsStrokeWidth", 1, js_engine_graphics_stroke_width),
    JS_CFUNC_DEF("graphicsStroke", 0, js_engine_graphics_stroke),

    JS_CFUNC_DEF("graphicsFontSize", 1, js_engine_graphics_font_size),
    JS_CFUNC_DEF("graphicsFontFace", 1, js_engine_graphics_font_face),
    JS_CFUNC_DEF("graphicsTextAlign", 1, js_engine_graphics_text_align),
    JS_CFUNC_DEF("graphicsText", 3, js_engine_graphics_text),
    JS_CFUNC_DEF("graphicsTextBox", 4, js_engine_graphics_text_box),

    JS_CFUNC_DEF("flushRendering", 0, js_engine_flush_rendering),

    JS_CFUNC_DEF("setOnFrame", 1, js_engine_set_frame_callback),
    JS_CFUNC_DEF("setOnResize", 1, js_engine_set_resize_callback),

    JS_CFUNC_DEF("setOnKeyDown", 1, js_engine_set_keydown_callback),
    JS_CFUNC_DEF("setOnKeyUp", 1, js_engine_set_keyup_callback),
    JS_CFUNC_DEF("setOnMouseDown", 1, js_engine_set_mousedown_callback),
    JS_CFUNC_DEF("setOnMouseUp", 1, js_engine_set_mouseup_callback),
    JS_CFUNC_DEF("setOnMouseMove", 1, js_engine_set_mousemove_callback),
    JS_CFUNC_DEF("setOnTouchStart", 1, js_engine_set_touchstart_callback),
    JS_CFUNC_DEF("setOnTouchEnd", 1, js_engine_set_touchend_callback),
    JS_CFUNC_DEF("setOnTouchMove", 1, js_engine_set_touchmove_callback),

    JS_CFUNC_DEF("getScreenLeft", 1, js_engine_get_screen_left),
    JS_CFUNC_DEF("getScreenRight", 1, js_engine_get_screen_right),
    JS_CFUNC_DEF("getScreenTop", 1, js_engine_get_screen_top),
    JS_CFUNC_DEF("getScreenBottom", 1, js_engine_get_screen_bottom),

    JS_CFUNC_DEF("loadSound", 2, js_engine_load_sound),
    JS_CFUNC_DEF("playSound", 3, js_engine_play_sound),

    JS_CFUNC_DEF("loadFont", 2, js_engine_load_font),

};

int engine_get_viewport_mode()
{
  return state.viewport_mode;
}

int isBlack(NVGcolor col)
{
  if (col.r == 0.0f && col.g == 0.0f && col.b == 0.0f && col.a == 0.0f)
  {
    return 1;
  }
  return 0;
}

void drawButton(NVGcontext *vg, int preicon, const char *text, float x, float y, float w, float h, NVGcolor col)
{
  NVGpaint bg;
  char icon[8];
  float cornerRadius = 4.0f;
  float tw = 0, iw = 0;

  bg = nvgLinearGradient(vg, x, y, x, y + h, nvgRGBA(255, 255, 255, isBlack(col) ? 16 : 32), nvgRGBA(0, 0, 0, isBlack(col) ? 16 : 32));
  nvgBeginPath(vg);
  nvgRoundedRect(vg, x + 1, y + 1, w - 2, h - 2, cornerRadius - 1);
  if (!isBlack(col))
  {
    nvgFillColor(vg, col);
    nvgFill(vg);
  }
  nvgFillPaint(vg, bg);
  nvgFill(vg);

  nvgBeginPath(vg);
  nvgRoundedRect(vg, x + 0.5f, y + 0.5f, w - 1, h - 1, cornerRadius - 0.5f);
  nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 48));
  nvgStroke(vg);

  nvgFontSize(vg, 17.0f);
  nvgFontFace(vg, "bold");
  tw = nvgTextBounds(vg, 0, 0, text, NULL, NULL);

  /*if (preicon != 0)
  {
    nvgFontSize(vg, h * 1.3f);
    nvgFontFace(vg, "icons");
    iw = nvgTextBounds(vg, 0, 0, cpToUTF8(preicon, icon), NULL, NULL);
    iw += h * 0.15f;
  }*/

  /*if (preicon != 0)
  {
    nvgFontSize(vg, h * 1.3f);
    nvgFontFace(vg, "icons");
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 96));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(vg, x + w * 0.5f - tw * 0.5f - iw * 0.75f, y + h * 0.5f, cpToUTF8(preicon, icon), NULL);
  }*/

  nvgFontSize(vg, 17.0f);
  nvgFontFace(vg, "bold");
  nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
  nvgFillColor(vg, nvgRGBA(0, 0, 0, 160));
  nvgText(vg, x + w * 0.5f - tw * 0.5f + iw * 0.25f, y + h * 0.5f - 1, text, NULL);
  nvgFillColor(vg, nvgRGBA(255, 255, 255, 160));
  nvgText(vg, x + w * 0.5f - tw * 0.5f + iw * 0.25f, y + h * 0.5f, text, NULL);
}

void engine_frame()
{

  float width = sapp_width();
  float height = sapp_height();
  float ratio = width / height;

  if (state.vg != NULL)
  {
    nvgBeginFrame(state.vg, width, height, sapp_dpi_scale());
  }

  sgl_defaults();
  sgl_load_pipeline(state.pip);

  set_viewport();

  sgl_enable_texture();

  sg_begin_default_pass(&state.pass_action, sapp_width(), sapp_height());

  if (JS_IsFunction(state.ctx, engine_get_frame_callback()))
  {
    JS_Call(state.ctx, engine_get_frame_callback(), JS_UNDEFINED, 0, NULL);
  }

  sgl_draw();
  //__dbgui_draw();

  sg_end_pass();
  sg_commit();

  if (state.vg != NULL)
  {
    nvgEndFrame(state.vg);
    sg_reset_state_cache();
  }
}

static int js_engine_init(JSContext *ctx, JSModuleDef *m)
{
  NVGcontext *vg = NULL;

  SOKOL_LOG("init nanovg");

#if defined(SOKOL_METAL)
  vg = nvgCreateMTL(sapp_metal_get_layer(), NVG_ANTIALIAS | NVG_STENCIL_STROKES);
#else
  vg = nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES | NVG_DEBUG);
#endif
  if (vg == NULL)
  {

    SOKOL_LOG("nanovg error");
  }
  else
  {
    SOKOL_LOG("nanovg init done");
  }

  state.vg = vg;

  Soloud *soloud = Soloud_create();
  Soloud_initEx(soloud, SOLOUD_CLIP_ROUNDOFF | SOLOUD_ENABLE_VISUALIZATION,
                SOLOUD_AUTO, SOLOUD_AUTO, SOLOUD_AUTO, 2);

  Soloud_setGlobalVolume(soloud, 4);

  state.soloud = soloud;

  state.pass_action = (sg_pass_action){
      .colors[0] = {.action = SG_ACTION_CLEAR, .value = {0.125f, 0.25f, 0.35f, 1.0f}}};

  state.pass_action_no_clear = (sg_pass_action){
      .colors[0] = {.action = SG_ACTION_DONTCARE, .value = {0.125f, 0.25f, 0.35f, 1.0f}}};

  for (int i = 0; i < 256; i++)
  {
    state.textures[i] = sg_alloc_image();
  }

  state.viewport_mode = ITER_VIEWPORT_FIXED_HEIGHT;

  SOKOL_LOG("textures inited");

  state.pip = sgl_make_pipeline(&(sg_pipeline_desc){
      .sample_count = 4,
      .colors[0].pixel_format = SG_PIXELFORMAT_BGRA8,
      .colors[0].blend = {
          .enabled = true,
          .src_factor_rgb = SG_BLENDFACTOR_ONE, // SG_BLENDFACTOR_SRC_ALPHA, // Let's try and make a fully premultiplied renderer
          .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA}});

  state.ctx = ctx;

  return JS_SetModuleExportList(ctx, m, js_my_module_funcs, countof(js_my_module_funcs));
}

JSModuleDef *js_init_module_engine(JSContext *ctx, const char *module_name)
{
  JSModuleDef *m;
  m = JS_NewCModule(ctx, module_name, js_engine_init);

  if (!m)
    return NULL;

  JS_AddModuleExportList(ctx, m, js_my_module_funcs, countof(js_my_module_funcs));
  return m;
}
