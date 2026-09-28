#!/usr/bin/env python3
"""Compile the actual runtime coordinate helpers and check 1x/2x/3x alignment."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'iteration-sapp/sapp/runtime.c').read_text()

def function(name):
    start = source.index('static float ' + name + '(')
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

names = ['convert_screen_x_to_local', 'convert_screen_y_to_local',
         'convert_local_x_to_screen_dimensions', 'convert_local_x_to_screen',
         'convert_local_y_to_screen_dimensions', 'convert_local_y_to_screen',
         'scale_local_to_screen_dimensions', 'scale_local_to_screen']
code = '''#include <assert.h>
#include <math.h>
#include <stdio.h>
enum { ITER_VIEWPORT_COVER, ITER_VIEWPORT_FIXED_WIDTH, ITER_VIEWPORT_FIXED_HEIGHT };
static struct { int viewport_mode; } state = { ITER_VIEWPORT_FIXED_HEIGHT };
static int width, height;
static int sapp_width(void) { return width; }
static int sapp_height(void) { return height; }
'''+ '\n'.join(function(name) for name in names) + '''
int main(void) {
  for (int landscape=0; landscape<2; landscape++) for (int dpi=1; dpi<=3; dpi++) {
    width=(landscape?844:390)*dpi; height=(landscape?390:844)*dpi;
    for (int v=-400; v<=400; v+=40) {
      float x=convert_local_x_to_screen(v), y=convert_local_y_to_screen(v);
      assert(fabsf(x-(width/2.f+v*height/1000.f))<0.001f);
      assert(fabsf(y-(height/2.f+v*height/1000.f))<0.001f);
      assert(fabsf(convert_screen_x_to_local(x)-v)<0.002f);
      assert(fabsf(convert_screen_y_to_local(y)-v)<0.002f);
      assert(fabsf(scale_local_to_screen(v)-v*height/1000.f)<0.001f);
    }
  }
  puts("PASS: framebuffer coordinates and touch round trips at 1x/2x/3x, portrait/landscape");
}
'''
assert 'sapp_dpi_scale()' not in source, 'NanoVG must not apply DPI twice'
layers = (root / 'iteration-sapp/sapp/engines/min_layers.h').read_text()
assert 'nvgBeginFrame(min_layers.vg,sapp_width(),sapp_height(),1.0f)' in layers
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(code)
    subprocess.run(['cc', str(path/'test.c'), '-lm', '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True)
