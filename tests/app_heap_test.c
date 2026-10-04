#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <malloc/malloc.h>
#include "../src/platform/app_heap.c"
static char* arena;static size_t cursor;static unsigned freed;
struct Block{void* p;size_t n;};static struct Block blocks[64];static unsigned count;
void* __real_malloc(size_t n){return malloc(n);}
void* __real_calloc(size_t n,size_t s){return calloc(n,s);}
void* __real_realloc(void* p,size_t n){return realloc(p,n);}
void __real_free(void* p){free(p);}
int __real_posix_memalign(void** p,size_t a,size_t n){return posix_memalign(p,a,n);}
size_t __real_malloc_usable_size(const void* p){return malloc_size(p);}
void* sceLibcMspaceCreate(const char* n,void* p,size_t size,unsigned flags){(void)n;(void)size;(void)flags;arena=p;return p;}
void* sceLibcMspaceMalloc(void* m,size_t n){(void)m;if(n>1024*1024)return NULL;cursor=(cursor+63)&~(size_t)63;void* p=arena+cursor;cursor+=n;blocks[count++]=(struct Block){p,n};return p;}
void* sceLibcMspaceCalloc(void* m,size_t n,size_t s){if(s&&n>SIZE_MAX/s)return NULL;void* p=sceLibcMspaceMalloc(m,n*s);if(p)memset(p,0,n*s);return p;}
size_t sceLibcMspaceMallocUsableSize(const void* p){for(unsigned i=0;i<count;i++)if(blocks[i].p==p)return blocks[i].n;abort();}
void sceLibcMspaceFree(void* m,void* p){(void)m;(void)p;freed++;}
void* sceLibcMspaceRealloc(void* m,void* p,size_t n){void* q=sceLibcMspaceMalloc(m,n);if(q){size_t old=sceLibcMspaceMallocUsableSize(p);memcpy(q,p,old<n?old:n);}return q;}
int sceLibcMspacePosixMemalign(void* m,void** p,size_t a,size_t n){if(a!=64)return EINVAL;*p=sceLibcMspaceMalloc(m,n);return *p?0:ENOMEM;}
int64_t sceKernelGetDirectMemorySize(void){return 0;}
int32_t sceKernelAllocateDirectMemory(int64_t a,int64_t b,size_t c,size_t d,int e,int64_t* f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return -1;}
int32_t sceKernelMapDirectMemory(void** a,size_t b,int c,int d,int64_t e,size_t f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return -1;}
int main(void){
 char* p=__wrap_malloc(256);assert(p&&ps5_heap_owns(p));memset(p,42,256);
 p=__wrap_realloc(p,512);assert(p&&p[255]==42);
 p=__wrap_realloc(p,2*1024*1024);assert(p&&!ps5_heap_owns(p)&&p[255]==42);__wrap_free(p);
 void* aligned=NULL;assert(__wrap_posix_memalign(&aligned,64,128)==0);assert(((uintptr_t)aligned%64)==0);__wrap_free(aligned);
 unsigned char* zero=__wrap_calloc(8,8);for(int i=0;i<64;i++)assert(zero[i]==0);__wrap_free(zero);
 assert(freed==3);assert(ps5_opengl_heap_live_bytes()==0);return 0;
}
int sceKernelDebugOutText(int channel,const char* text){(void)channel;(void)text;return 0;}
