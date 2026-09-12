
#include "quickjs.h"

#include "engines.h"
#include <math.h>
#include "../runtime.h"

#define countof(x) (sizeof(x) / sizeof((x)[0]))

#if defined(__EMSCRIPTEN__)
#include "emscripten.h"
#define SOKOL_LOG(...) emscripten_log(EM_LOG_CONSOLE, __VA_ARGS__);
#endif

static struct
{
  JSValue atlas;
} state;

typedef struct
{
  char *name;
  int texture;
  double frameX;
  double frameY;
  double frameWidth;
  double frameHeight;
  double alpha;
} JSSpriteData;

/* Point Class */

static JSClassID js_point_class_id;

JSClassID get_js_point_class_id()
{
  return js_point_class_id;
}

static void js_point_finalizer(JSRuntime *rt, JSValue val)
{
  JSPointData *s = JS_GetOpaque(val, js_point_class_id);
  /* Note: 's' can be NULL in case JS_SetOpaque() was not called */
  js_free_rt(rt, s);
}

static JSValue js_point_ctor(JSContext *ctx,
                             JSValueConst new_target,
                             int argc, JSValueConst *argv)
{
  JSPointData *s;
  JSValue obj = JS_UNDEFINED;
  JSValue proto;

  s = js_mallocz(ctx, sizeof(*s));
  if (!s)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &s->x, argv[0]))
    goto fail;
  if (JS_ToFloat64(ctx, &s->y, argv[1]))
    goto fail;
  if (JS_ToFloat64(ctx, &s->z, argv[2]))
    goto fail;
  /* using new_target to get the prototype is necessary when the
       class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if (JS_IsException(proto))
    goto fail;
  obj = JS_NewObjectProtoClass(ctx, proto, js_point_class_id);
  JS_FreeValue(ctx, proto);
  if (JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, s);
  return obj;
fail:
  js_free(ctx, s);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue js_point_get_xyz(JSContext *ctx, JSValueConst this_val, int magic)
{
  JSPointData *s = JS_GetOpaque2(ctx, this_val, js_point_class_id);
  if (!s)
    return JS_EXCEPTION;
  if (magic == 0)
    return JS_NewFloat64(ctx, s->x);
  else if (magic == 1)
    return JS_NewFloat64(ctx, s->y);
  else
    return JS_NewFloat64(ctx, s->z);
}

static JSValue js_point_set_xyz(JSContext *ctx, JSValueConst this_val, JSValue val, int magic)
{
  JSPointData *s = JS_GetOpaque2(ctx, this_val, js_point_class_id);
  double v;
  if (!s)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &v, val))
    return JS_EXCEPTION;
  if (magic == 0)
    s->x = v;
  else if (magic == 1)
    s->y = v;
  else
    s->z = v;
  return JS_UNDEFINED;
}

static JSClassDef js_point_class = {
    "Point",
    .finalizer = js_point_finalizer,
};

static const JSCFunctionListEntry js_point_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("x", js_point_get_xyz, js_point_set_xyz, 0),
    JS_CGETSET_MAGIC_DEF("y", js_point_get_xyz, js_point_set_xyz, 1),
    JS_CGETSET_MAGIC_DEF("z", js_point_get_xyz, js_point_set_xyz, 2),

};

/* Rect Class */

static JSClassID js_rect_class_id;

static void js_rect_finalizer(JSRuntime *rt, JSValue val)
{
  JSRectData *s = JS_GetOpaque(val, js_rect_class_id);
  /* Note: 's' can be NULL in case JS_SetOpaque() was not called */
  js_free_rt(rt, s);
}

static JSValue js_rect_ctor(JSContext *ctx,
                            JSValueConst new_target,
                            int argc, JSValueConst *argv)
{
  JSRectData *s;
  JSValue obj = JS_UNDEFINED;
  JSValue proto;

  s = js_mallocz(ctx, sizeof(*s));
  if (!s)
    return JS_EXCEPTION;
  if (JS_ToFloat64(ctx, &s->x, argv[0]))
    goto fail;
  if (JS_ToFloat64(ctx, &s->y, argv[1]))
    goto fail;

  s->hasLocalPos = 0;
  if (!JS_IsUndefined(argv[2]) || !JS_IsUndefined(argv[3]))
  {
    s->hasLocalPos = 1;
  }
  if (JS_ToFloat64(ctx, &s->localX, argv[2]))
    goto fail;
  if (JS_ToFloat64(ctx, &s->localY, argv[3]))
    goto fail;
  if (JS_ToFloat64(ctx, &s->width, argv[4]))
    goto fail;
  if (JS_ToFloat64(ctx, &s->height, argv[5]))
    goto fail;
  if (JS_ToFloat64(ctx, &s->alignX, argv[6]))
    goto fail;
  if (JS_ToFloat64(ctx, &s->alignY, argv[7]))
    goto fail;
  if (JS_ToFloat64(ctx, &s->scale, argv[8]))
    goto fail;
  if (JS_ToInt32(ctx, &s->hidden, argv[9]))
    goto fail;
  /* using new_target to get the prototype is necessary when the
       class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if (JS_IsException(proto))
    goto fail;
  obj = JS_NewObjectProtoClass(ctx, proto, js_rect_class_id);
  JS_FreeValue(ctx, proto);
  if (JS_IsException(obj))
    goto fail;
  JS_SetOpaque(obj, s);
  return obj;
fail:
  js_free(ctx, s);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue js_rect_get_props(JSContext *ctx, JSValueConst this_val, int magic)
{
  JSRectData *s = JS_GetOpaque2(ctx, this_val, js_rect_class_id);
  if (!s)
    return JS_EXCEPTION;

  switch (magic)
  {
  case 0:
    return JS_NewFloat64(ctx, s->x);
  case 1:
    return JS_NewFloat64(ctx, s->y);
  case 2:
    return JS_NewFloat64(ctx, s->localX);
  case 3:
    return JS_NewFloat64(ctx, s->localY);
  case 4:
    return JS_NewFloat64(ctx, s->width);
  case 5:
    return JS_NewFloat64(ctx, s->height);
  case 6:
    return JS_NewFloat64(ctx, s->alignX);
  case 7:
    return JS_NewFloat64(ctx, s->alignY);
  case 8:
    return JS_NewFloat64(ctx, s->scale);
  case 9:
    return JS_NewBool(ctx, s->hidden);
  }
}

static JSValue js_rect_set_props(JSContext *ctx, JSValueConst this_val, JSValue val, int magic)
{
  JSRectData *s = JS_GetOpaque2(ctx, this_val, js_rect_class_id);
  double v;
  int b;
  if (!s)
    return JS_EXCEPTION;

  if (magic == 9)
  {
    if (JS_ToInt32(ctx, &b, val))
      return JS_EXCEPTION;
    s->hidden = b;
    return JS_UNDEFINED;
  }

  if (JS_ToFloat64(ctx, &v, val))
    return JS_EXCEPTION;

  switch (magic)
  {
  case 0:
    s->x = v;
    break;
  case 1:
    s->y = v;
    break;
  case 2:
    s->localX = v;
    break;
  case 3:
    s->localY = v;
    break;
  case 4:
    s->width = v;
    break;
  case 5:
    s->height = v;
    break;
  case 6:
    s->alignX = v;
    break;
  case 7:
    s->alignY = v;
    break;
  case 8:
    s->scale = v;
    break;
  }

  return JS_UNDEFINED;
}

static JSClassDef js_rect_class = {
    "Rect",
    .finalizer = js_rect_finalizer,
};

static const JSCFunctionListEntry js_rect_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("x", js_rect_get_props, js_rect_set_props, 0),
    JS_CGETSET_MAGIC_DEF("y", js_rect_get_props, js_rect_set_props, 1),
    JS_CGETSET_MAGIC_DEF("localX", js_rect_get_props, js_rect_set_props, 2),
    JS_CGETSET_MAGIC_DEF("localY", js_rect_get_props, js_rect_set_props, 3),
    JS_CGETSET_MAGIC_DEF("width", js_rect_get_props, js_rect_set_props, 4),
    JS_CGETSET_MAGIC_DEF("height", js_rect_get_props, js_rect_set_props, 5),
    JS_CGETSET_MAGIC_DEF("alignX", js_rect_get_props, js_rect_set_props, 6),
    JS_CGETSET_MAGIC_DEF("alignY", js_rect_get_props, js_rect_set_props, 7),
    JS_CGETSET_MAGIC_DEF("scale", js_rect_get_props, js_rect_set_props, 8),
    JS_CGETSET_MAGIC_DEF("hidden", js_rect_get_props, js_rect_set_props, 9),
};

/* Sprite Class */

static JSClassID js_sprite_class_id;

JSClassID get_js_sprite_class_id()
{
  return js_sprite_class_id;
}

static void js_sprite_finalizer(JSRuntime *rt, JSValue val)
{
  JSSpriteData *s = JS_GetOpaque(val, js_sprite_class_id);
  /* Note: 's' can be NULL in case JS_SetOpaque() was not called */
  js_free_rt(rt, s);
}

static void set_frame_for_sprite(JSContext *ctx, JSSpriteData *s)
{
  JSValue texture = JS_GetPropertyStr(ctx, state.atlas, "texture");
  JS_ToInt32(ctx, &s->texture, JS_GetPropertyStr(ctx, texture, "id"));

  JSValue frames = JS_GetPropertyStr(ctx, state.atlas, "frames");

  JSValue frameObj = JS_GetPropertyStr(ctx, frames, s->name);
  JSValue frame = JS_GetPropertyStr(ctx, frameObj, "frame");

  JS_ToFloat64(ctx, &s->frameX, JS_GetPropertyStr(ctx, frame, "x"));
  JS_ToFloat64(ctx, &s->frameY, JS_GetPropertyStr(ctx, frame, "y"));
  JS_ToFloat64(ctx, &s->frameWidth, JS_GetPropertyStr(ctx, frame, "w"));
  JS_ToFloat64(ctx, &s->frameHeight, JS_GetPropertyStr(ctx, frame, "h"));
}

static JSValue js_sprite_ctor(JSContext *ctx,
                              JSValueConst new_target,
                              int argc, JSValueConst *argv)
{
  // atlas, name, alpha
  JSSpriteData *s;
  JSValue obj = JS_UNDEFINED;
  JSValue proto;

  s = js_mallocz(ctx, sizeof(*s));
  if (!s)
    return JS_EXCEPTION;

  // s->name = JS_DupValue(ctx, argv[0]);
  s->name = JS_ToCString(ctx, argv[0]);

  set_frame_for_sprite(ctx, s);

  if (JS_IsUndefined(argv[2]))
  {
    s->alpha = 1;
  }
  else
  {
    JS_ToFloat64(ctx, &s->alpha, argv[2]);
  }

  /* using new_target to get the prototype is necessary when the
       class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if (JS_IsException(proto))
    goto fail;
  obj = JS_NewObjectProtoClass(ctx, proto, js_sprite_class_id);
  JS_FreeValue(ctx, proto);
  if (JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, s);
  return obj;
fail:
  js_free(ctx, s);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue js_sprite_get_props(JSContext *ctx, JSValueConst this_val, int magic)
{
  JSSpriteData *s = JS_GetOpaque2(ctx, this_val, js_sprite_class_id);
  if (!s)
    return JS_EXCEPTION;
  if (magic == 0)
    return JS_NewString(ctx, s->name);
  else
    return JS_NewFloat64(ctx, s->alpha);
}

static JSValue js_sprite_set_props(JSContext *ctx, JSValueConst this_val, JSValue val, int magic)
{
  JSSpriteData *s = JS_GetOpaque2(ctx, this_val, js_sprite_class_id);
  double v;
  if (!s)
    return JS_EXCEPTION;

  if (magic == 0)
  {
    JS_FreeCString(ctx, s->name);
    s->name = JS_ToCString(ctx, val);
    set_frame_for_sprite(ctx, s);
  }
  else
  {
    if (JS_ToFloat64(ctx, &v, val))
      return JS_EXCEPTION;
    s->alpha = v;
  }
  return JS_UNDEFINED;
}

static JSClassDef js_sprite_class = {
    "Sprite",
    .finalizer = js_sprite_finalizer,
};

static const JSCFunctionListEntry js_sprite_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("name", js_sprite_get_props, js_sprite_set_props, 0),
    JS_CGETSET_MAGIC_DEF("alpha", js_sprite_get_props, js_sprite_set_props, 1),

};

static JSValue js_engine_star_animation(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv)
{
  JSValue type = argv[0];
  JSValue actors = JS_GetPropertyStr(ctx, type, "actors");

  JSValue jsLen = JS_GetPropertyStr(ctx, actors, "length");

  int arrayLen;
  JS_ToInt32(ctx, &arrayLen, jsLen);

  JSAtom rect_atom, star_atom, sprite_atom, alpha_atom;
  JSValue ret;
  rect_atom = JS_NewAtom(ctx, "Rect");
  star_atom = JS_NewAtom(ctx, "StarAnimation");
  sprite_atom = JS_NewAtom(ctx, "Sprite");
  alpha_atom = JS_NewAtom(ctx, "alpha");

  JSValue rects = JS_GetPropertyStr(ctx, type, "Rect");
  JSValue stars = JS_GetPropertyStr(ctx, type, "StarAnimation");
  JSValue sprites = JS_GetPropertyStr(ctx, type, "Sprite");

  for (int i = 0; i < arrayLen; i++)
  {
    // JSValue actor = actors_arr[i]; //JS_GetPropertyUint32(ctx, actors, i);
    // JSValue actor = JS_GetPropertyUint32(ctx, actors, i);
    /*JSObject *p;
    uint32_t idx, len;
    p = JS_VALUE_GET_OBJ(actors);
    JSValue actor = JS_DupValue(ctx, p->u.array.u.values[idx]);*/

    /*JSValue Rect = JS_GetPropertyStr(ctx, actor, "Rect");
    JSValue StarAnimation = JS_GetPropertyStr(ctx, actor, "StarAnimation");
    JSValue Sprite = JS_GetPropertyStr(ctx, actor, "Sprite");*/

    /*JSValue Rect = JS_GetProperty(ctx, actor, rect_atom);
    JSValue StarAnimation = JS_GetProperty(ctx, actor, star_atom);
    JSValue Sprite = JS_GetProperty(ctx, actor, sprite_atom);*/

    JSValue Rect = JS_GetPropertyUint32(ctx, rects, i);
    JSValue StarAnimation = JS_GetPropertyUint32(ctx, stars, i);
    JSValue Sprite = JS_GetPropertyUint32(ctx, sprites, i);

    JSPointData *star_anim = JS_GetOpaque2(ctx, StarAnimation, js_point_class_id);
    JSRectData *rect_data = JS_GetOpaque2(ctx, Rect, js_rect_class_id);
    JSSpriteData *sprite_data = JS_GetOpaque2(ctx, Sprite, js_sprite_class_id);

    /*double star_x, star_y, star_z;
    JS_ToFloat64(ctx, &star_x, JS_GetPropertyStr(ctx, StarAnimation, "x"));
    JS_ToFloat64(ctx, &star_y, JS_GetPropertyStr(ctx, StarAnimation, "y"));
    JS_ToFloat64(ctx, &star_z, JS_GetPropertyStr(ctx, StarAnimation, "z"));*/

    double rect_x, rect_y, scale, alpha;
    double perspective = 10000;
    double dist = 200;
    rect_data->x = (star_anim->x / star_anim->z) * perspective;
    rect_data->y = (star_anim->y / star_anim->z) * perspective;
    rect_data->scale = ((dist - star_anim->z) / dist) * 0.2;
    sprite_data->alpha = rect_data->scale;

    star_anim->z -= 0.4;

    if (star_anim->z < 0)
    {
      star_anim->z = dist;
    }

    JS_FreeValue(ctx, Rect);
    JS_FreeValue(ctx, StarAnimation);
    JS_FreeValue(ctx, Sprite);

    // JS_SetPropertyStr(ctx, Rect, "x", JS_NewFloat64(ctx, rect_x));
    // JS_SetPropertyStr(ctx, Rect, "y", JS_NewFloat64(ctx, rect_y));
    // JS_SetPropertyStr(ctx, Rect, "scale", JS_NewFloat64(ctx, scale));
    // JS_SetProperty(ctx, Sprite, alpha_atom, JS_NewFloat64(ctx, alpha));
    // JS_SetPropertyStr(ctx, StarAnimation, "z", JS_NewFloat64(ctx, star_z));
  }

  JS_FreeValue(ctx, actors);
  JS_FreeValue(ctx, jsLen);
  JS_FreeValue(ctx, rects);
  JS_FreeValue(ctx, stars);
  JS_FreeValue(ctx, sprites);
  JS_FreeAtom(ctx, rect_atom);
  JS_FreeAtom(ctx, star_atom);
  JS_FreeAtom(ctx, sprite_atom);
  JS_FreeAtom(ctx, alpha_atom);

  return JS_UNDEFINED;
}

/*
    let x = actor.Rect.x;
    let y = actor.Rect.y;
    if (!actor.ParentRect) {
      if (actor.Rect.localX) {
        x = actor.Rect.localX;
      }
      if (actor.Rect.localY) {
        y = actor.Rect.localY;
      }
    }
    let alpha = 1;
    if (actor.Sprite.alpha) {
      alpha = actor.Sprite.alpha;
    }

    Engine.drawAtlasFrame(Engine.atlas, actor.Sprite.name, x, y, 0, 0, 0, actor.Rect.scale, alpha);
*/

/*

  let frame = atlas.frames[frameName].frame;

  Runtime.setTexture(atlas.texture.id);
  Runtime.drawTextureClip(
    frame.x,
    frame.y,
    frame.w,
    frame.h,
    x,
    y,
    anchorX,
    anchorY,
    rotation,
    scale,
    alpha
  );

  */

static JSValue js_engine_sprite_update(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
  JSValue type = argv[0];
  JSValue actors = JS_GetPropertyStr(ctx, type, "actors");
  JSValue jsLen = JS_GetPropertyStr(ctx, actors, "length");

  int arrayLen;
  JS_ToInt32(ctx, &arrayLen, jsLen);

  JSAtom rect_atom, sprite_atom, alpha_atom, parent_atom;
  JSValue ret;
  rect_atom = JS_NewAtom(ctx, "Rect");
  sprite_atom = JS_NewAtom(ctx, "Sprite");
  parent_atom = JS_NewAtom(ctx, "ParentRect");

  alpha_atom = JS_NewAtom(ctx, "alpha");

  JSValue rects = JS_GetPropertyStr(ctx, type, "Rect");
  JSValue sprites = JS_GetPropertyStr(ctx, type, "Sprite");

  JSValue parentRects = JS_GetPropertyStr(ctx, type, "ParentRect");

  for (int i = 0; i < arrayLen; i++)
  {
    JSValue Rect = JS_GetPropertyUint32(ctx, rects, i);
    JSValue Sprite = JS_GetPropertyUint32(ctx, sprites, i);

    JSRectData *rect_data = JS_GetOpaque2(ctx, Rect, js_rect_class_id);
    JSSpriteData *sprite_data = JS_GetOpaque2(ctx, Sprite, js_sprite_class_id);

    /*double star_x, star_y, star_z;
    JS_ToFloat64(ctx, &star_x, JS_GetPropertyStr(ctx, StarAnimation, "x"));
    JS_ToFloat64(ctx, &star_y, JS_GetPropertyStr(ctx, StarAnimation, "y"));
    JS_ToFloat64(ctx, &star_z, JS_GetPropertyStr(ctx, StarAnimation, "z"));*/

    double x, y, alpha;
    x = rect_data->x;
    y = rect_data->y;
    if (JS_IsUndefined(parentRects))
    {
      if (rect_data->hasLocalPos)
      {
        x = rect_data->localX;
        y = rect_data->localY;
      }
    }

    // fix: Sprite struct WITH frame
    // call: engine_draw_texture_clip

    engine_set_texture(sprite_data->texture);
    engine_draw_texture_clip(sprite_data->frameX, sprite_data->frameY, sprite_data->frameWidth, sprite_data->frameHeight, x, y, 0, 0, 0, rect_data->scale, sprite_data->alpha);

    JS_FreeValue(ctx, Rect);
    JS_FreeValue(ctx, Sprite);
  }

  JS_FreeValue(ctx, actors);
  JS_FreeValue(ctx, jsLen);
  JS_FreeValue(ctx, rects);
  JS_FreeValue(ctx, sprites);
  JS_FreeValue(ctx, parentRects);
  JS_FreeAtom(ctx, rect_atom);
  JS_FreeAtom(ctx, sprite_atom);
  JS_FreeAtom(ctx, alpha_atom);
  JS_FreeAtom(ctx, parent_atom);

  return JS_UNDEFINED;
}

static JSValue js_engine_set_atlas(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
  state.atlas = JS_DupValue(ctx, argv[0]);
  return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_my_module_funcs[] = {
    JS_CFUNC_DEF("starAnimationUpdate", 1, js_engine_star_animation),
    JS_CFUNC_DEF("spriteEngineUpdate", 1, js_engine_sprite_update),
    JS_CFUNC_DEF("setAtlas", 1, js_engine_set_atlas),

};

static int js_point_init(JSContext *ctx, JSModuleDef *m)
{
  JSValue point_proto, point_class;

  /* create the Point class */
  JS_NewClassID(&js_point_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_point_class_id, &js_point_class);

  point_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, point_proto, js_point_proto_funcs, countof(js_point_proto_funcs));

  point_class = JS_NewCFunction2(ctx, js_point_ctor, "Point", 2, JS_CFUNC_constructor, 0);
  /* set proto.constructor and ctor.prototype */
  JS_SetConstructor(ctx, point_class, point_proto);
  JS_SetClassProto(ctx, js_point_class_id, point_proto);

  JS_SetModuleExport(ctx, m, "Point", point_class);

  JSValue rect_proto, rect_class;

  /* create the Rect class */
  JS_NewClassID(&js_rect_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_rect_class_id, &js_rect_class);

  rect_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, rect_proto, js_rect_proto_funcs, countof(js_rect_proto_funcs));

  rect_class = JS_NewCFunction2(ctx, js_rect_ctor, "Rect", 2, JS_CFUNC_constructor, 0);
  /* set proto.constructor and ctor.prototype */
  JS_SetConstructor(ctx, rect_class, rect_proto);
  JS_SetClassProto(ctx, js_rect_class_id, rect_proto);

  JS_SetModuleExport(ctx, m, "Rect", rect_class);

  JSValue sprite_proto, sprite_class;

  /* create the Sprite class */
  JS_NewClassID(&js_sprite_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_sprite_class_id, &js_sprite_class);

  sprite_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, sprite_proto, js_sprite_proto_funcs, countof(js_sprite_proto_funcs));

  sprite_class = JS_NewCFunction2(ctx, js_sprite_ctor, "Sprite", 2, JS_CFUNC_constructor, 0);
  /* set proto.constructor and ctor.prototype */
  JS_SetConstructor(ctx, sprite_class, sprite_proto);
  JS_SetClassProto(ctx, js_sprite_class_id, sprite_proto);

  JS_SetModuleExport(ctx, m, "Sprite", sprite_class);

  JS_SetModuleExportList(ctx, m, js_my_module_funcs, countof(js_my_module_funcs));

  return 0;
}

JSModuleDef *js_init_module_engines(JSContext *ctx, const char *module_name)
{
  JSModuleDef *m;
  m = JS_NewCModule(ctx, module_name, js_point_init);
  if (!m)
    return NULL;
  JS_AddModuleExport(ctx, m, "Point");
  JS_AddModuleExport(ctx, m, "Rect");
  JS_AddModuleExport(ctx, m, "Sprite");

  JS_AddModuleExportList(ctx, m, js_my_module_funcs, countof(js_my_module_funcs));

  return m;
}
