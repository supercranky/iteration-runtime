#include "asset_fetch.h"
#include <stdlib.h>
#include <string.h>
#ifdef ITERATION_ASSET_FETCH_IOS
#include "asset_fetch_ios.h"
#endif
void asset_fetch_init(void (*after_completion)(void)) {
#ifdef ITERATION_ASSET_FETCH_IOS
  asset_fetch_ios_init(after_completion);
#else
  (void)after_completion;
#endif
}
void asset_fetch_shutdown(void) {
#ifdef ITERATION_ASSET_FETCH_IOS
  asset_fetch_ios_shutdown();
#endif
}
typedef struct {
  void (*callback)(const sfetch_response_t *);
  unsigned char data[128];
} asset_request;
static unsigned pending;
static void complete(const sfetch_response_t *response) {
  asset_request *request=*(asset_request **)response->user_data;
  sfetch_response_t original=*response;original.user_data=request->data;
  if(response->finished)pending--;
  request->callback(&original);
  if(response->finished)free(request);
}
sfetch_handle_t asset_fetch(const sfetch_request_t *request) {
  if(request->user_data.size>128)return (sfetch_handle_t){0};
  asset_request *copy=calloc(1,sizeof(*copy));if(!copy)return (sfetch_handle_t){0};
  copy->callback=request->callback;
  if(request->user_data.size)memcpy(copy->data,request->user_data.ptr,request->user_data.size);
  sfetch_request_t wrapped=*request;wrapped.callback=complete;
  wrapped.user_data=(sfetch_range_t){.ptr=&copy,.size=sizeof(copy)};
#ifdef ITERATION_ASSET_FETCH_IOS
  sfetch_handle_t handle=asset_fetch_ios_send(&wrapped);
#else
  sfetch_handle_t handle=sfetch_send(&wrapped);
#endif
  if(handle.id)pending++;else free(copy);
  return handle;
}
int asset_fetch_busy(void) {return pending!=0;}
