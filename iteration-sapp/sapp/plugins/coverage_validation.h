#pragma once
#include "iteration_plugin.h"
#include <math.h>
static inline int iteration_validate_coverage(const uint8_t *bytes,size_t size,iteration_coverage_command *out){
  if(!bytes||!out||size<sizeof(*out))return 0;
  memcpy(out,bytes,sizeof(*out));
  if(out->header.opcode!=ITER_RENDER_COVERAGE_TRIANGLES||out->header.byte_size!=size||
     !out->count||out->count>ITER_COVERAGE_MAX_VERTICES||out->count%3||out->blend>1||
     size!=sizeof(*out)+out->count*sizeof(iteration_coverage_vertex))return 0;
  float bounds[4]={out->left,out->top,out->right,out->bottom};
  for(int j=0;j<4;j++)if(!isfinite(bounds[j])||fabsf(bounds[j])>1e12f)return 0;
  if(out->left>out->right||out->top>out->bottom)return 0;
  for(uint32_t i=0;i<out->count;i++){
    float v[9];memcpy(v,bytes+sizeof(*out)+i*sizeof(iteration_coverage_vertex),sizeof(v));
    for(int j=0;j<9;j++)if(!isfinite(v[j])||fabsf(v[j])>1e12f)return 0;
    if(v[8]<0||v[8]>1)return 0;
  }
  return 1;
}
