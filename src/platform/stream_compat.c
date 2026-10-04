// SPDX-License-Identifier: GPL-3.0-or-later
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#include <mbedtls/timing.h>
#include "address.h"
extern unsigned long long sceKernelGetProcessTime(void);
extern struct tm* gmtime_r(const time_t*,struct tm*);
/* This application uses UTC internally, including FFmpeg diagnostics. */
struct tm* localtime_r(const time_t* t,struct tm* out){return gmtime_r(t,out);}
int timingsafe_bcmp(const void* left,const void* right,size_t size){const volatile unsigned char* a=left;const volatile unsigned char* b=right;unsigned char diff=0;while(size--)diff|=*a++^*b++;return diff!=0;}
char* if_indextoname(unsigned index,char* name){(void)index;(void)name;errno=ENXIO;return NULL;}
/* Remote GFN candidates use routable IPs, never LAN mDNS names. */
int mdns_resolve_addr(const char* name,Address* address){(void)name;(void)address;return -1;}
void mbedtls_timing_set_delay(void* data,uint32_t intermediate,uint32_t final){mbedtls_timing_delay_context* c=data;uint64_t now=sceKernelGetProcessTime()/1000;memcpy(c->timer.opaque,&now,sizeof(now));c->int_ms=intermediate;c->fin_ms=final;}
int mbedtls_timing_get_delay(void* data){mbedtls_timing_delay_context* c=data;if(!c->fin_ms)return -1;uint64_t start;memcpy(&start,c->timer.opaque,sizeof(start));uint64_t elapsed=sceKernelGetProcessTime()/1000-start;return elapsed>=c->fin_ms?2:elapsed>=c->int_ms?1:0;}

#include <langinfo.h>
char* nl_langinfo(nl_item item){return item==CODESET ? "UTF-8" : "";}
size_t ___mb_cur_max(void){return 4;}
