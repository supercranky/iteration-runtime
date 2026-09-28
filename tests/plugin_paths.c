// cc -std=c99 -Wall -Wextra -Werror tests/plugin_paths.c -o /tmp/plugin-paths && /tmp/plugin-paths
#include "../iteration-sapp/sapp/engines/plugin_paths.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
  uint8_t bytes[256],unaligned[257];float bounds[4];
  iteration_graphics_writer writer;iteration_graphics_init(&writer,bytes,sizeof(bytes));
  graphicsMoveTo(&writer,-13.25f,2);graphicsLineTo(&writer,-13.25f,7);
  graphicsLineTo(&writer,31,7);graphicsLineTo(&writer,31,2);graphicsClosePath(&writer);
  uint8_t *path=bytes+sizeof(iteration_render_buffer);
  size_t size=writer.length-sizeof(iteration_render_buffer);
  assert(size==ITERATION_RECT_PATH_BYTES);
  assert(plugin_rect_path(path,size,bounds));
  assert(bounds[0]==-13.25f && bounds[1]==2 && bounds[2]==31 && bounds[3]==7);
  memcpy(unaligned+1,path,size);assert(plugin_rect_path(unaligned+1,size,bounds));
  for(size_t i=0;i<size;i++)assert(!plugin_rect_path(path,i,bounds));
  for(int i=0;i<5;i++) {
    size_t offset=i*sizeof(iteration_xy_command);
    iteration_render_command saved,changed;memcpy(&saved,path+offset,sizeof(saved));
    changed=saved;changed.byte_size++;memcpy(path+offset,&changed,sizeof(changed));
    assert(!plugin_rect_path(path,size,bounds));
    changed=saved;changed.opcode=ITER_RENDER_FILL;memcpy(path+offset,&changed,sizeof(changed));
    assert(!plugin_rect_path(path,size,bounds));memcpy(path+offset,&saved,sizeof(saved));
  }
  float saved;size_t x2=2*sizeof(iteration_xy_command)+sizeof(iteration_render_command);
  memcpy(&saved,path+x2,sizeof(saved));
  const float invalid[]={NAN,INFINITY,-INFINITY,32};
  for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
    memcpy(path+x2,&invalid[i],sizeof(float));assert(!plugin_rect_path(path,size,bounds));
  }
  memcpy(path+x2,&saved,sizeof(saved));assert(plugin_rect_path(path,size,bounds));
  puts("PASS: exact rectangle recognition, unaligned input, all truncated prefixes, malformed sizes/opcodes, non-finite and nonrectangular paths");
}
