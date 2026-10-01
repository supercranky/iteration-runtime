#include "../iteration-sapp/sapp/plugins/coverage_validation.h"
#include <assert.h>
#include <stdio.h>
int main(void){
  uint8_t bytes[256];iteration_graphics_writer writer;
  iteration_coverage_vertex v[3]={{0}};float bounds[4]={-10,-10,10,10};
  for(int i=0;i<3;i++){v[i].a[0]=v[i].b[0]=2;v[i].a[1]=v[i].b[1]=1;v[i].opacity=.6f;}
  iteration_graphics_init(&writer,bytes,sizeof(bytes));graphicsCoverageTriangles(&writer,0,bounds,v,3);
  uint8_t *packet=bytes+sizeof(iteration_render_buffer);size_t size=writer.length-sizeof(iteration_render_buffer);
  iteration_coverage_command out,original;memcpy(&original,packet,sizeof original);
  assert(iteration_validate_coverage(packet,size,&out));
  assert(!iteration_validate_coverage(packet,size-1,&out));
  assert(!iteration_validate_coverage(packet,3,&out));
  for(int count=0;count<4;count++){out=original;out.count=(uint32_t[]){0,4,1539,UINT32_MAX}[count];memcpy(packet,&out,sizeof out);assert(!iteration_validate_coverage(packet,size,&out));}
  out=original;out.blend=2;memcpy(packet,&out,sizeof out);assert(!iteration_validate_coverage(packet,size,&out));
  out=original;out.right=-20;memcpy(packet,&out,sizeof out);assert(!iteration_validate_coverage(packet,size,&out));
  out=original;out.top=NAN;memcpy(packet,&out,sizeof out);assert(!iteration_validate_coverage(packet,size,&out));
  memcpy(packet,&original,sizeof original);
  float nan=NAN;memcpy(packet+sizeof(original)+sizeof(float)*2,&nan,sizeof nan);assert(!iteration_validate_coverage(packet,size,&out));
  memcpy(packet+sizeof(original),v,sizeof v);float bad=2;memcpy(packet+sizeof(original)+sizeof(float)*8,&bad,sizeof bad);assert(!iteration_validate_coverage(packet,size,&out));
  memcpy(packet+sizeof(original),v,sizeof v);original.blend=1;memcpy(packet,&original,sizeof original);assert(iteration_validate_coverage(packet,size,&out));
  puts("PASS bounded coverage packets, malformed lengths/counts, ranges and non-finite rejection");
}
