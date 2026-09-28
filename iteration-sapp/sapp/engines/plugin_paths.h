#pragma once
#include "../plugins/iteration_plugin.h"
#include <math.h>
#include <string.h>

#define ITERATION_RECT_PATH_BYTES (4*sizeof(iteration_xy_command)+sizeof(iteration_render_command))

// Recognize exactly MoveTo(a,b), LineTo(a,d), LineTo(c,d), LineTo(c,b), Close.
// No aliasing/alignment assumptions, no approximate comparisons, no unchecked
// lookahead. Failure leaves the ordinary validator/executor responsible.
static int plugin_rect_path(const uint8_t *bytes,size_t size,float bounds[4]) {
  if(size<ITERATION_RECT_PATH_BYTES)return 0;
  iteration_xy_command p[4];iteration_render_command close;
  memcpy(p,bytes,sizeof(p));memcpy(&close,bytes+sizeof(p),sizeof(close));
  for(int i=0;i<4;i++) if(p[i].header.byte_size!=sizeof(p[i]) ||
      p[i].header.opcode!=(i?ITER_RENDER_LINE_TO:ITER_RENDER_MOVE_TO) ||
      !isfinite(p[i].x) || !isfinite(p[i].y))return 0;
  if(close.opcode!=ITER_RENDER_CLOSE_PATH || close.byte_size!=sizeof(close))return 0;
  if(memcmp(&p[0].x,&p[1].x,sizeof(float)) || memcmp(&p[1].y,&p[2].y,sizeof(float)) ||
     memcmp(&p[2].x,&p[3].x,sizeof(float)) || memcmp(&p[3].y,&p[0].y,sizeof(float)))return 0;
  bounds[0]=p[0].x;bounds[1]=p[0].y;bounds[2]=p[2].x;bounds[3]=p[1].y;
  return 1;
}
