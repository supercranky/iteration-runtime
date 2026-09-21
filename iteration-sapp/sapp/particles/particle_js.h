#ifndef ITERATION_PARTICLE_JS_H
#define ITERATION_PARTICLE_JS_H
#include "quickjs.h"
JSValue particle_js_create_api(JSContext *ctx);
void particle_js_shutdown(void);
#endif
