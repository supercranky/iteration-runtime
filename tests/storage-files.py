#!/usr/bin/env python3
"""Exercise the production POSIX storage function against temporary directories."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'iteration-sapp/sapp/engines/storage.h').read_text()
struct = source[source.index('static struct storage_request'):source.index('static unsigned storage_token;')]
function = source[source.index('static void storage_file('):source.index('static void *storage_worker(')]
prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#define STORAGE_LIMIT 1024
#define STORAGE_SLOTS 4
typedef void JSContext;
typedef int JSValue;
'''
tests = r'''
static void reset(struct storage_request *r,int op,const char *key) {
  free(r->output);memset(r,0,sizeof(*r));r->op=op;snprintf(r->key,sizeof(r->key),"%s",key);
}
int main(int argc,char **argv) {
  assert(argc==2);setenv("HOME",argv[1],1);setenv("TMPDIR",argv[1],1);
  struct storage_request r={0};
  reset(&r,0,"save");storage_file(&r);assert(r.missing&&!r.error[0]);
  reset(&r,1,"save");r.input="old";r.length=3;storage_file(&r);assert(!r.error[0]);
  reset(&r,1,"save");r.input="new";r.length=3;storage_file(&r);assert(!r.error[0]);
  reset(&r,0,"save.backup");storage_file(&r);assert(r.output_length==3&&!strcmp(r.output,"old"));
  reset(&r,0,"save");storage_file(&r);assert(!strcmp(r.output,"new"));
  char temp[PATH_MAX];snprintf(temp,sizeof(temp),"%s/.iteration-data/save.tmp",argv[1]);
  assert(!mkdir(temp,0700));
  reset(&r,1,"save");r.input="broken";r.length=6;storage_file(&r);assert(r.error[0]);
  reset(&r,0,"save");storage_file(&r);assert(!strcmp(r.output,"new"));assert(!rmdir(temp));
  reset(&r,1,"save");r.input="session";r.length=7;r.temporary=1;storage_file(&r);assert(!r.error[0]);
  reset(&r,0,"save");r.temporary=1;storage_file(&r);assert(!strcmp(r.output,"session"));
  reset(&r,2,"save");storage_file(&r);assert(!r.error[0]);
  reset(&r,0,"save");storage_file(&r);assert(r.missing);
  reset(&r,0,"save.backup");storage_file(&r);assert(!strcmp(r.output,"old"));
  reset(&r,0,"save");r.temporary=1;storage_file(&r);assert(!strcmp(r.output,"session"));
  free(r.output);
  puts("PASS: native atomic storage, previous-save backup, failed-write preservation, temporary isolation, missing keys and deletion");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path/'test.c').write_text(prefix+struct+function+tests)
    subprocess.run(['cc','-O2',str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test'),directory],check=True)
