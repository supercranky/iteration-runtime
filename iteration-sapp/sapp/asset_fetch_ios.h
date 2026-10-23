#pragma once
#include "sokol_fetch.h"
/* Main-thread-only, whole-file buffered reads. Handles indicate acceptance;
   they are not Sokol handles and must not be passed to sfetch_* APIs. */
void asset_fetch_ios_init(void (*after_completion)(void));
void asset_fetch_ios_shutdown(void);
sfetch_handle_t asset_fetch_ios_send(const sfetch_request_t *request);
