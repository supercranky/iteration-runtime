#pragma once
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "quickjs.h"
/* One allocation domain per VM. The legacy bindings retain JSValues (including
 * callback return values), so reference-counted teardown alone cannot reclaim a
 * VM. Native subsystems must be shut down first. No native handle may outlive
 * this domain. std/os handlers are explicitly closed before releasing it. */
typedef union vm_block vm_block;
union vm_block {
  struct { vm_block *prev,*next;size_t size; } h;
  long double alignment;
  void *pointer_alignment;
};
static vm_block *vm_blocks;
static void *vm_malloc(JSMallocState *s,size_t size) {
  if(!size||size>SIZE_MAX-sizeof(vm_block)||size>s->malloc_limit||s->malloc_size>s->malloc_limit-size)return NULL;
  vm_block *b=malloc(sizeof(*b)+size);if(!b)return NULL;
  b->h.size=size;b->h.prev=NULL;b->h.next=vm_blocks;
  if(vm_blocks)vm_blocks->h.prev=b;vm_blocks=b;
  s->malloc_count++;s->malloc_size+=size;return b+1;
}
static void vm_free(JSMallocState *s,void *ptr) {
  if(!ptr)return;vm_block *b=(vm_block *)ptr-1;
  if(b->h.prev)b->h.prev->h.next=b->h.next;else vm_blocks=b->h.next;
  if(b->h.next)b->h.next->h.prev=b->h.prev;
  s->malloc_count--;s->malloc_size-=b->h.size;free(b);
}
static void *vm_realloc(JSMallocState *s,void *ptr,size_t size) {
  if(!ptr)return vm_malloc(s,size);
  if(!size){vm_free(s,ptr);return NULL;}
  vm_block *old=(vm_block *)ptr-1;
  void *next=vm_malloc(s,size);if(!next)return NULL;
  memcpy(next,ptr,old->h.size<size?old->h.size:size);vm_free(s,ptr);return next;
}
static size_t vm_size(const void *ptr) {return ptr?((const vm_block *)ptr-1)->h.size:0;}
static const JSMallocFunctions vm_allocator={vm_malloc,vm_free,vm_realloc,vm_size};
static void vm_release(void) {
  while(vm_blocks){vm_block *next=vm_blocks->h.next;free(vm_blocks);vm_blocks=next;}
}
