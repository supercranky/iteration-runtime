#pragma once
#include "quickjs.h"

void models_init(JSContext *ctx);
void models_update(double dt);
void models_render(void);
JSValue js_models_set_last_layer(JSContext *, JSValueConst, int, JSValueConst *);
void models_shutdown(void);

JSValue js_models_load(JSContext *, JSValueConst, int, JSValueConst *);
JSValue js_models_unload(JSContext *, JSValueConst, int, JSValueConst *);
JSValue js_models_create(JSContext *, JSValueConst, int, JSValueConst *);
JSValue js_models_destroy(JSContext *, JSValueConst, int, JSValueConst *);
JSValue js_models_draw(JSContext *, JSValueConst, int, JSValueConst *);
JSValue js_models_play(JSContext *, JSValueConst, int, JSValueConst *);
JSValue js_models_stop(JSContext *, JSValueConst, int, JSValueConst *);
JSValue js_models_set_time(JSContext *, JSValueConst, int, JSValueConst *);
JSValue js_models_set_camera(JSContext *, JSValueConst, int, JSValueConst *);
JSValue js_models_set_lighting(JSContext *, JSValueConst, int, JSValueConst *);
JSValue js_models_set_pixel_style(JSContext *, JSValueConst, int, JSValueConst *);
