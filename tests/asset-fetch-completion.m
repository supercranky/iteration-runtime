#import <Foundation/Foundation.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "asset_fetch.h"

static char path[256], buffers[16][64];
static int callbacks, hooks, chain;
static void submit(int id);
static void complete(const sfetch_response_t *response) {
    assert([NSThread isMainThread]);
    int id = *(int *)response->user_data;
    assert(response->finished);
    if (id == 8) assert(response->failed && response->error_code == SFETCH_ERROR_FILE_NOT_FOUND);
    else if (id == 9) assert(response->failed && response->error_code == SFETCH_ERROR_BUFFER_TOO_SMALL);
    else if (id >= 10) assert(response->cancelled && response->error_code == SFETCH_ERROR_CANCELLED);
    else {
        assert(response->fetched && !response->failed);
        assert(response->data.size == 5 && !memcmp(response->data.ptr, "hello", 5));
    }
    callbacks++;
}
static void jobs(void) {
    assert([NSThread isMainThread]);
    hooks++;
    if (chain > 0) submit(--chain);
}
static void submit(int id) {
    sfetch_request_t request = {
        .path = id == 8 ? "/definitely/not/an/asset" : path, .callback = complete,
        .buffer = {buffers[id], id == 9 ? 2 : sizeof(buffers[id])},
        .user_data = {&id, sizeof(id)},
    };
    assert(asset_fetch(&request).id);
}
static void waitFor(int count) {
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:3];
    while (callbacks < count && deadline.timeIntervalSinceNow > 0)
        [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.001]];
    assert(callbacks == count);
}
int main(void) {
    @autoreleasepool {
        snprintf(path, sizeof(path), "/tmp/iteration-fetch-%d", getpid());
        FILE *file = fopen(path, "wb"); assert(file);
        assert(fwrite("hello", 1, 5, file) == 5); fclose(file);
        asset_fetch_init(jobs);
        chain = 3; submit(3);
        // No frame loop or sfetch_dowork: completion jobs alone chain all reads.
        waitFor(4); assert(hooks == 4 && !asset_fetch_busy());
        submit(8); submit(9); waitFor(6); assert(hooks == 6 && !asset_fetch_busy());
        submit(10); submit(11);
        asset_fetch_shutdown();
        assert(callbacks == 8 && hooks == 6 && !asset_fetch_busy());
        asset_fetch_init(jobs);
        submit(0); waitFor(9);
        // Drain stale queued deliveries; they must not call the new VM's hook.
        [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
        assert(callbacks == 9 && hooks == 7 && !asset_fetch_busy());
        asset_fetch_shutdown(); unlink(path);
        puts("PASS completion-only chaining, copied request data, errors, shutdown cancellation, stale-delivery isolation");
    }
}
