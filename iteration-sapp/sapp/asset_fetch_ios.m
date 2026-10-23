#import <Foundation/Foundation.h>
#import <TargetConditionals.h>
#if TARGET_OS_IOS
#import <OpenGLES/EAGL.h>
#endif
#include "asset_fetch_ios.h"
#include "startup_trace.h"
#include <stdio.h>
#include <string.h>

@interface IterationAssetRead : NSObject {
@public
    sfetch_request_t request;
    sfetch_response_t response;
    unsigned char user_data[128];
    BOOL delivered;
}
@property(nonatomic, copy) NSString *path;
#if TARGET_OS_IOS
@property(nonatomic, strong) EAGLContext *context;
#endif
@end
@implementation IterationAssetRead
@end

static dispatch_queue_t worker;
static NSMutableArray<IterationAssetRead *> *active;
static void (*completion_hook)(void);
static uint32_t next_id;

static void deliver(IterationAssetRead *read, BOOL cancelled) {
    NSCAssert([NSThread isMainThread], @"Asset delivery must stay on main thread");
    if (read->delivered) return;
    read->delivered = YES;
    [active removeObjectIdenticalTo:read];
    if (cancelled) {
        read->response.fetched = false;
        read->response.failed = read->response.cancelled = true;
        read->response.error_code = SFETCH_ERROR_CANCELLED;
    }
#if TARGET_OS_IOS
    EAGLContext *previous = [EAGLContext currentContext];
    // Upload callbacks may run between GLK frames. Explicitly restore the
    // request's render context, then put back the caller's context afterward.
    [EAGLContext setCurrentContext:read.context];
#endif
    double trace_start = startup_trace_external_begin();
    read->request.callback(&read->response);
    if (!cancelled && completion_hook) completion_hook();
    startup_trace_external_end(trace_start);
#if TARGET_OS_IOS
    [EAGLContext setCurrentContext:previous];
#endif
}

void asset_fetch_ios_init(void (*after_completion)(void)) {
    NSCAssert([NSThread isMainThread] && !active, @"Invalid asset loader init");
    if (!worker) worker = dispatch_queue_create("iteration.asset-read", DISPATCH_QUEUE_SERIAL);
    active = [NSMutableArray array];
    completion_hook = after_completion;
}

void asset_fetch_ios_shutdown(void) {
    NSCAssert([NSThread isMainThread], @"Asset shutdown must stay on main thread");
    completion_hook = NULL;
    // No worker may touch caller-owned buffers after VM teardown. Workers never
    // wait on the main queue, so this cannot deadlock with queued deliveries.
    if (worker) dispatch_sync(worker, ^{});
    NSArray *cancelled = [active copy];
    active = nil; // Reject new requests from cancellation callbacks.
    for (IterationAssetRead *read in cancelled) deliver(read, YES);
    // Already-queued main blocks retain only delivered records, and do nothing.
}

sfetch_handle_t asset_fetch_ios_send(const sfetch_request_t *request) {
    NSCAssert([NSThread isMainThread], @"Asset submission must stay on main thread");
    if (!active || active.count >= 256 || !request || !request->path ||
        !request->callback || !request->buffer.ptr || !request->buffer.size ||
        request->chunk_size || request->user_data.size > 128 ||
        (request->user_data.size && !request->user_data.ptr)) return (sfetch_handle_t){0};
    IterationAssetRead *read = [IterationAssetRead new];
    read.path = [NSString stringWithUTF8String:request->path];
    if (!read.path) return (sfetch_handle_t){0};
    read->request = *request;
    read->request.path = read.path.UTF8String;
    if (request->user_data.size) memcpy(read->user_data, request->user_data.ptr, request->user_data.size);
    read->request.user_data.ptr = read->user_data;
#if TARGET_OS_IOS
    read.context = [EAGLContext currentContext];
#endif
    if (!++next_id) ++next_id;
    read->response = (sfetch_response_t){
        .handle = {next_id}, .finished = true, .channel = request->channel,
        .path = read->request.path, .user_data = read->user_data,
        .buffer = request->buffer, .data = {.ptr = request->buffer.ptr},
    };
    [active addObject:read];
    dispatch_async(worker, ^{
        @autoreleasepool {
            FILE *file = fopen(read->request.path, "rb");
            sfetch_error_t error = SFETCH_ERROR_NO_ERROR;
            size_t bytes = 0;
            if (!file) error = SFETCH_ERROR_FILE_NOT_FOUND;
            else {
                bytes = fread((void *)read->request.buffer.ptr, 1, read->request.buffer.size, file);
                if (ferror(file)) error = SFETCH_ERROR_UNEXPECTED_EOF;
                else if (bytes == read->request.buffer.size && fgetc(file) != EOF)
                    error = SFETCH_ERROR_BUFFER_TOO_SMALL;
                if (ferror(file)) error = SFETCH_ERROR_UNEXPECTED_EOF;
                fclose(file);
            }
            read->response.data.size = bytes;
            read->response.error_code = error;
            read->response.failed = error != SFETCH_ERROR_NO_ERROR;
            read->response.fetched = !read->response.failed;
            dispatch_async(dispatch_get_main_queue(), ^{ deliver(read, NO); });
        }
    });
    return read->response.handle;
}
