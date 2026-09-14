#pragma once

#include "quickjs.h"
#include "nanovg.h"
typedef float (*visibility_transform_fn)(float value);

void visibility_init(NVGcontext *vg,
                     visibility_transform_fn convert_x,
                     visibility_transform_fn convert_y,
                     visibility_transform_fn scale,
                     visibility_transform_fn translate_x,
                     visibility_transform_fn translate_y);

JSValue js_engine_visibility_set_map(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv);
JSValue js_engine_visibility_draw(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv);
