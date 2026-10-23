#include "startup_trace.h"
#include <stdlib.h>
int main(void) {
    setenv("ITERATION_STARTUP_TRACE", "1", 1);
    startup_trace_init();
    for (unsigned i = 0; i < 2048; i++) {
        double external = startup_trace_external_begin();
        startup_trace_external_end(external);
        startup_trace_begin(i);
        for (int phase = 0; phase < ST_PHASES; phase++) startup_trace_mark(phase);
        startup_trace_end();
    }
    unsetenv("ITERATION_STARTUP_TRACE");
    startup_trace_init();
    for (unsigned i = 0; i < 2048; i++) {
        startup_trace_begin(i); startup_trace_mark(ST_JS); startup_trace_end();
    }
    return 0;
}
