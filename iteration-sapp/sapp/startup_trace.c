#include "startup_trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CAPACITY 1024
typedef struct {
    uint64_t frame;
    double at, interval, outside, external, cpu, phases[ST_PHASES];
} trace_frame;
static trace_frame frames[CAPACITY];
static int enabled, count, in_frame;
static double origin, last_start, last_end, checkpoint, external_pending;
static const char *names[ST_PHASES] = {
    "fetchJobs", "storage", "setup", "js", "particles", "flat",
    "vector", "world", "models", "alpha", "overlay", "ui", "commit", "reload"
};
static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0;
}
void startup_trace_init(void) {
    const char *value = getenv("ITERATION_STARTUP_TRACE");
    enabled = value && !strcmp(value, "1");
    count = in_frame = 0;
    origin = last_start = last_end = checkpoint = external_pending = 0;
    if (enabled) memset(frames, 0, sizeof(frames));
}
void startup_trace_begin(uint64_t frame) {
    if (!enabled) return;
    double now = now_ms();
    if (!count) origin = now;
    trace_frame *f = &frames[count];
    f->frame = frame; f->at = now - origin;
    f->interval = count ? now - last_start : 0;
    f->outside = count ? now - last_end : 0;
    f->external = external_pending; external_pending = 0;
    last_start = checkpoint = now; in_frame = 1;
}
void startup_trace_mark(int phase) {
    if (!enabled || !in_frame) return;
    double now = now_ms();
    if (phase >= 0 && phase < ST_PHASES) frames[count].phases[phase] += now - checkpoint;
    checkpoint = now;
}
static void print_frame(int i, const char *role) {
    trace_frame *f = &frames[i];
    printf("[native-frame] {\"role\":\"%s\",\"frame\":%llu,\"atMs\":%.3f,\"intervalMs\":%.3f,\"outsideMs\":%.3f,\"assetCallbacksMs\":%.3f,\"cpuMs\":%.3f,\"phases\":{",
        role, (unsigned long long)f->frame, f->at, f->interval, f->outside, f->external, f->cpu);
    for (int p = 0; p < ST_PHASES; p++) printf("%s\"%s\":%.3f", p ? "," : "", names[p], f->phases[p]);
    puts("}}");
}
void startup_trace_end(void) {
    if (!enabled || !in_frame) return;
    last_end = now_ms();
    frames[count].cpu = last_end - last_start;
    count++; in_frame = 0;
    // Six seconds keeps console I/O out of the normal two-second opening zoom.
    if (last_end - origin >= 6000 || count == CAPACITY) {
        enabled = 0;
        printf("[native-capture] {\"frames\":%d,\"durationMs\":%.3f}\n", count, last_end - origin);
        for (int i = 0; i < count; i++) {
            if (frames[i].interval > 25 || frames[i].cpu > 20) {
                if (i) print_frame(i - 1, "preceding");
                print_frame(i, "slow");
            }
        }
        fflush(stdout);
    }
}
double startup_trace_external_begin(void) { return enabled ? now_ms() : 0; }
void startup_trace_external_end(double started) {
    if (enabled && started > 0) external_pending += now_ms() - started;
}
