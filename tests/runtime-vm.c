#include "../iteration-sapp/sapp/runtime_vm.h"
#include <assert.h>
#include <stdint.h>
int main(void) {
  for(int run=0;run<100;run++) {
    JSMallocState state={.malloc_limit=1024*1024};
    void *blocks[256];
    for(unsigned i=0;i<256;i++) {
      blocks[i]=vm_malloc(&state,i+1);assert(blocks[i]);
      assert((uintptr_t)blocks[i]%sizeof(void *)==0);
      memset(blocks[i],(int)i,i+1);
    }
    assert(state.malloc_count==256);
    for(unsigned i=0;i<256;i+=2) {
      blocks[i]=vm_realloc(&state,blocks[i],512);assert(blocks[i]);
      for(unsigned j=0;j<i+1;j++)assert(((unsigned char *)blocks[i])[j]==(unsigned char)i);
      vm_free(&state,blocks[i]);
    }
    assert(state.malloc_count==128);
    // A lowered limit must not underflow and allow allocations.
    state.malloc_limit=1;assert(!vm_malloc(&state,1));
    vm_release();assert(!vm_blocks);
  }
  return 0;
}
