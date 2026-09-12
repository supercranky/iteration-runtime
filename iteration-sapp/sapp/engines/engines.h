#include "quickjs.h"

JSModuleDef *js_init_module_engines(JSContext *ctx, const char *module_name);

JSClassID get_js_point_class_id();

typedef struct
{
  double x;
  double y;
  double z;
} JSPointData;

typedef struct
{
  double x;
  double y;
  int hasLocalPos;
  double localX;
  double localY;
  double width;
  double height;
  double alignX;
  double alignY;
  double scale;
  int hidden;
} JSRectData;
