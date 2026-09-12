#include "quickjs.h"

#ifdef __cplusplus
extern "C"
{
#endif

#include "sokol_app.h"

  JSModuleDef *js_init_module_engine(JSContext *ctx, const char *module_name);

  JSValue engine_get_frame_callback();

  void engine_frame();

  int engine_get_viewport_mode();
  void engine_handle_event(const sapp_event *e);

  void engine_draw_texture_clip(double source_x, double source_y, double source_width, double source_height, double x, double y, double anchor_x, double anchor_y, double rotation, double scale, double alpha);
  void engine_set_texture(int texture);

#define ITER_VIEWPORT_COVER 0
#define ITER_VIEWPORT_FIXED_WIDTH 1
#define ITER_VIEWPORT_FIXED_HEIGHT 2

#ifdef __cplusplus
} /* extern "C" { */
#endif
