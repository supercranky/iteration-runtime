#pragma once
#include "sokol_fetch.h"
/* All runtime asset requests participate in the reset barrier. On iOS these
   are whole-file reads; returned handles indicate acceptance only and are not
   compatible with sfetch_* handle APIs. */
void asset_fetch_init(void (*after_completion)(void));
void asset_fetch_shutdown(void);
sfetch_handle_t asset_fetch(const sfetch_request_t *request);
int asset_fetch_busy(void);
