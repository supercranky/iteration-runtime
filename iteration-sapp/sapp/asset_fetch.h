#pragma once
#include "sokol_fetch.h"
/* All runtime asset requests participate in the reset barrier. */
sfetch_handle_t asset_fetch(const sfetch_request_t *request);
int asset_fetch_busy(void);
