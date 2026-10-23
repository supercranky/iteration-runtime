#pragma once
#include <stdint.h>
enum {
    ST_FETCH_JOBS, ST_STORAGE, ST_SETUP, ST_JS, ST_PARTICLES, ST_FLAT,
    ST_VECTOR, ST_WORLD, ST_MODELS, ST_ALPHA, ST_OVERLAY, ST_UI, ST_COMMIT,
    ST_RELOAD, ST_PHASES
};
/* Opt-in with ITERATION_STARTUP_TRACE=1; CPU wall times, never GPU timings. */
void startup_trace_init(void);
void startup_trace_begin(uint64_t frame);
void startup_trace_mark(int phase);
void startup_trace_end(void);
double startup_trace_external_begin(void);
void startup_trace_external_end(double started);
