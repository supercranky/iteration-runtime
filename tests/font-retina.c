/* Compile natively and with Emscripten; pass the consuming app's font path.
 * WASM's smaller active-edge struct selects a larger stb_truetype heap chunk,
 * so a 64-bit-only regression does not reproduce the historical blank glyph.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FONTSTASH_IMPLEMENTATION
#include "fontstash.h"
static int errors;
static void font_error(void *unused, int code, int value) {
  (void)unused;
  fprintf(stderr,"Fontstash error %d, requested %d bytes\n",code,value);
  errors++;
}
int main(int argc,char **argv) {
  assert(argc==2);
  FONSparams params={0};params.width=1024;params.height=1024;params.flags=FONS_ZERO_TOPLEFT;
  FONScontext *fs=fonsCreateInternal(&params);assert(fs);
  fonsSetErrorCallback(fs,font_error,NULL);
  int font=fonsAddFont(fs,"test",argv[1]);assert(font>=0);
  fonsSetFont(fs,font);
  const float sizes[]={101.28f,202.56f,270,303.84f,405.12f,480};
  for(size_t i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++) {
    fonsResetAtlas(fs,1024,1024);fonsSetSize(fs,sizes[i]);
    /* Public iterator forces rasterization without needing a GL backend. */
    FONStextIter iter;FONSquad quad;
    fonsTextIterInit(fs,&iter,0,sizes[i],"m",NULL);
    assert(fonsTextIterNext(fs,&iter,&quad));assert(iter.prevGlyphIndex>=0);
    int width,height,pixels=0;const unsigned char *texture=fonsGetTextureData(fs,&width,&height);
    for(int y=(int)roundf(quad.t0*height);y<(int)roundf(quad.t1*height);y++)
      for(int x=(int)roundf(quad.s0*width);x<(int)roundf(quad.s1*width);x++)
        pixels+=texture[y*width+x]!=0;
    assert(pixels>sizes[i]);assert(errors==0);
    printf("PASS m at %.2fpx: %d nonempty pixels\n",sizes[i],pixels);
  }
  fonsDeleteInternal(fs);
  return 0;
}
