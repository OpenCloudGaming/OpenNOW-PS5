"""Exercise the production socket adapter with a rejecting native API."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/platform/ps5_sockets.c').read_text()
function=source[source.index('int socket(int domain'):source.index('\nint bind(')]
harness=r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 0x20000000
#define SOCK_CLOEXEC 0x10000000
#endif
#define SCE_NET_SO_NBIO 0x1200
static int calls, native_errno, first_error, second_error, protocols[2];
static int option_calls, option_error, closed, seen_type;
int sceNetSetsockopt(int fd,int level,int opt,const void *value,socklen_t size) {
    assert(fd==23&&level==SOL_SOCKET&&opt==SCE_NET_SO_NBIO&&size==sizeof(int));
    assert(*(const int*)value==1);++option_calls;native_errno=option_error;return option_error?-1:0;
}
int sceNetSocketClose(int fd) {assert(fd==23);++closed;native_errno=0;return 0;}
int *sceNetErrnoLoc(void) { return &native_errno; }
int sceNetSocket(const char *name,int domain,int type,int protocol) {
    (void)name;(void)domain;seen_type=type;
    assert(calls<2);protocols[calls]=protocol;
    native_errno=calls++ ? second_error : first_error;
    return native_errno ? -1 : 23;
}
void opennow_network_note(const char *s) {(void)s;}
void opennow_network_result(const char *s,int r,int e) {(void)s;(void)r;(void)e;}
static int network_result(int r) {if(r<0) errno=native_errno;return r;}
'''
tests=r'''
static void reset(int first,int second) {calls=0;first_error=first;second_error=second;option_calls=option_error=closed=0;}
int main(void) {
    reset(0,0);assert(socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)==23);assert(calls==1);
    reset(EPROTONOSUPPORT,0);assert(socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)==23);assert(calls==2&&protocols[0]==IPPROTO_TCP&&protocols[1]==0);
    reset(EPROTONOSUPPORT,0);assert(socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP)==23);assert(calls==2);
    reset(EPROTONOSUPPORT,0);assert(socket(AF_INET,SOCK_STREAM,132)==-1);assert(calls==1);
    reset(EPROTONOSUPPORT,0);assert(socket(AF_INET,SOCK_STREAM,0)==-1);assert(calls==1);
    reset(EMFILE,0);assert(socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)==-1);assert(calls==1&&errno==EMFILE);
    reset(EPROTONOSUPPORT,EACCES);assert(socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)==-1);assert(calls==2&&errno==EACCES);
    reset(0,0);assert(socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK,IPPROTO_TCP)==23);assert(seen_type==SOCK_STREAM&&option_calls==1);
    reset(0,0);assert(socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,IPPROTO_TCP)==23);assert(seen_type==SOCK_STREAM&&option_calls==0);
    reset(0,0);option_error=EACCES;assert(socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK,IPPROTO_TCP)==-1);assert(closed==1&&errno==EACCES);
    puts("Socket flags and fallback regressions passed");
}
'''
with tempfile.TemporaryDirectory() as temp:
    p=Path(temp);(p/'test.c').write_text(harness+function+tests)
    subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
