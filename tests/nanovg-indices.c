#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../iteration-sapp/libs/nanovg/nanovg_indices.h"
int main(void) {
  unsigned short out[32],fan[]={10,11,12,10,12,13,10,13,14};
  unsigned short strip[]={10,11,12,12,11,13,12,13,14};
  assert(nvg__appendPathIndices(out,0,32,10,5,0)==9);
  assert(memcmp(out,fan,sizeof fan)==0);
  assert(nvg__appendPathIndices(out,0,32,10,5,1)==9);
  assert(memcmp(out,strip,sizeof strip)==0);
  assert(nvg__appendPathIndices(out,9,32,100,3,0)==12);
  assert(out[9]==100&&out[10]==101&&out[11]==102);
  assert(nvg__appendPathIndices(out,0,8,10,5,0)==-1);
  assert(nvg__appendPathIndices(out,0,32,65533,3,0)==-1);
  assert(nvg__appendPathIndices(out,0,32,65532,3,0)==3);
  assert(out[2]==65534);
  assert(nvg__appendPathIndices(out,0,32,-1,3,0)==-1);
  assert(nvg__appendPathIndices(out,-1,32,0,3,0)==-1);
  assert(nvg__appendPathIndices(out,0,32,0,-1,0)==-1);
  assert(nvg__appendPathIndices(NULL,0,0,0,2,0)==0);
  unsigned short* large=malloc(NVG_INDEX_CAPACITY_LIMIT*sizeof *large);
  assert(large);
  for(int stripMode=0;stripMode<2;stripMode++) {
    int count=nvg__appendPathIndices(large,0,NVG_INDEX_CAPACITY_LIMIT,0,65535,stripMode);
    assert(count==(65535-2)*3);
    for(int i=0;i<count;i++)assert(large[i]!=65535);
    for(int i=2;i<65535;i++){
      int k=(i-2)*3;
      assert(large[k+2]==i);
      assert(large[k]==(stripMode?i-2+(i&1):0));
      assert(large[k+1]==(stripMode?i-1-(i&1):i-1));
    }
  }
  free(large);
  puts("PASS NanoVG fan/strip order, winding, bounds and restart exclusion");
}
