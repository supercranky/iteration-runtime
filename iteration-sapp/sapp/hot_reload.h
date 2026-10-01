#pragma once
#include <stddef.h>
#include "quickjs.h"
/* Dev-only transport. All JS access and generation commits happen on the frame thread. */
void hot_reload_start(void);
int hot_reload_pending(void);
int hot_reload_commit(const char *state);
const char *hot_reload_status(void);
char *hot_reload_take_state(void);
const char *hot_reload_asset_path(const char *name, char *buffer, size_t size);
void hot_reload_install(JSContext *ctx);
int hot_reload_tick(JSContext *ctx);
void hot_reload_forget(JSContext *ctx);
