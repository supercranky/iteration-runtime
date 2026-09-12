/*
 * This is a hack to support setTimeout on emscripten without allowing FILESYSTEM
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <limits.h>
#include <sys/stat.h>
#include <dirent.h>
#if defined(_WIN32)
#include <windows.h>
#include <conio.h>
#include <utime.h>
#else
#include <dlfcn.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#if defined(__APPLE__)
typedef sig_t sighandler_t;
#if !defined(environ)
#include <crt_externs.h>
#define environ (*_NSGetEnviron())
#endif
#endif /* __APPLE__ */

#endif

#if !defined(_WIN32)
/* enable the os.Worker API. IT relies on POSIX threads */
#define USE_WORKER
#endif

#ifdef USE_WORKER
#include <pthread.h>
#include <stdatomic.h>
#endif

#include "quickjs/cutils.h"
#include "quickjs/list.h"
#include "quickjs/quickjs-libc.h"

/* TODO:
   - add socket calls
*/

typedef struct
{
  struct list_head link;
  int fd;
  JSValue rw_func[2];
} JSOSRWHandler;

typedef struct
{
  struct list_head link;
  int sig_num;
  JSValue func;
} JSOSSignalHandler;

typedef struct
{
  struct list_head link;
  BOOL has_object;
  int64_t timeout;
  JSValue func;
} JSOSTimer;

typedef struct
{
  struct list_head link;
  uint8_t *data;
  size_t data_len;
  /* list of SharedArrayBuffers, necessary to free the message */
  uint8_t **sab_tab;
  size_t sab_tab_len;
} JSWorkerMessage;

typedef struct
{
  int ref_count;
#ifdef USE_WORKER
  pthread_mutex_t mutex;
#endif
  struct list_head msg_queue; /* list of JSWorkerMessage.link */
  int read_fd;
  int write_fd;
} JSWorkerMessagePipe;

typedef struct
{
  struct list_head link;
  JSWorkerMessagePipe *recv_pipe;
  JSValue on_message_func;
} JSWorkerMessageHandler;

typedef struct JSThreadState
{
  struct list_head os_rw_handlers;     /* list of JSOSRWHandler.link */
  struct list_head os_signal_handlers; /* list JSOSSignalHandler.link */
  struct list_head os_timers;          /* list of JSOSTimer.link */
  struct list_head port_list;          /* list of JSWorkerMessageHandler.link */
  int eval_script_recurse;             /* only used in the main thread */
  /* not used in the main thread */
  JSWorkerMessagePipe *recv_pipe, *send_pipe;
} JSThreadState;

static void call_handler(JSContext *ctx, JSValueConst func)
{
  JSValue ret, func1;
  /* 'func' might be destroyed when calling itself (if it frees the
       handler), so must take extra care */
  func1 = JS_DupValue(ctx, func);
  ret = JS_Call(ctx, func1, JS_UNDEFINED, 0, NULL);
  JS_FreeValue(ctx, func1);
  if (JS_IsException(ret))
    js_std_dump_error(ctx);
  JS_FreeValue(ctx, ret);
}

#if defined(__linux__) || defined(__APPLE__)
static int64_t get_time_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}
#else
/* more portable, but does not work if the date is updated */
static int64_t get_time_ms(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (int64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
}
#endif

static void unlink_timer(JSRuntime *rt, JSOSTimer *th)
{
  if (th->link.prev)
  {
    list_del(&th->link);
    th->link.prev = th->link.next = NULL;
  }
}

static void free_timer(JSRuntime *rt, JSOSTimer *th)
{
  JS_FreeValueRT(rt, th->func);
  js_free_rt(rt, th);
}

void js_update_timers(JSContext *ctx)
{
  JSRuntime *rt = JS_GetRuntime(ctx);
  JSThreadState *ts = JS_GetRuntimeOpaque(rt);
  int ret, fd_max, min_delay;
  int64_t cur_time, delay;
  fd_set rfds, wfds;
  struct list_head *el;
  struct timeval tv, *tvp;

  if (!list_empty(&ts->os_timers))
  {
    cur_time = get_time_ms();
    min_delay = 10000;
    list_for_each(el, &ts->os_timers)
    {
      JSOSTimer *th = list_entry(el, JSOSTimer, link);
      delay = th->timeout - cur_time;
      if (delay <= 0)
      {
        JSValue func;
        /* the timer expired */
        func = th->func;
        th->func = JS_UNDEFINED;
        unlink_timer(rt, th);
        if (!th->has_object)
          free_timer(rt, th);
        call_handler(ctx, func);
        JS_FreeValue(ctx, func);
        return;
      }
      else if (delay < min_delay)
      {
        min_delay = delay;
      }
    }
    tv.tv_sec = min_delay / 1000;
    tv.tv_usec = (min_delay % 1000) * 1000;
    tvp = &tv;
  }
  else
  {
    tvp = NULL;
  }
  return;
}
