// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
#include <errno.h>
#include <stddef.h>
#include <unistd.h>
// No subprocesses, terminal or syslog facility in this title.
void openlog(const char* id, int options, int facility) { (void)id; (void)options; (void)facility; }
FILE* popen(const char* command,const char* mode) { (void)command; (void)mode; errno=ENOSYS; return NULL; }
int pclose(FILE* stream) { (void)stream; errno=ENOSYS; return -1; }
int isatty(int fd) { (void)fd; return 0; }
int mkstemps(char* path,int suffix) { (void)path; (void)suffix; errno=ENOSYS; return -1; }
int mkstemp(char* path) { (void)path; errno=ENOSYS; return -1; }
__attribute__((noreturn)) void __assert(const char* function,const char* file,int line,const char* expression) {
    (void)function; (void)file; (void)line; (void)expression;
    __builtin_trap();
}

#include <dirent.h>
#include <pthread.h>
#include <stdint.h>
// This prototype uses a bundled PEM file, never OpenSSL certificate directories.
// Avoid SDK imports into libScePosixForWebKit, unavailable to native titles.
DIR* opendir(const char* path) { (void)path; errno=ENOENT; return NULL; }
struct dirent* readdir(DIR* dir) { (void)dir; errno=EBADF; return NULL; }
int closedir(DIR* dir) { (void)dir; errno=EBADF; return -1; }
extern int sceKernelUsleep(uint32_t);
// Same FreeBSD once-control protocol as ProsperoLight's native runtime.
int pthread_once(pthread_once_t* control,void (*init)(void)) {
    int state=__atomic_load_n(&control->state,__ATOMIC_ACQUIRE);
    if (state==PTHREAD_DONE_INIT) return 0;
    int expected=PTHREAD_NEEDS_INIT;
    if (__atomic_compare_exchange_n(&control->state,&expected,2,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
        init(); __atomic_store_n(&control->state,PTHREAD_DONE_INIT,__ATOMIC_RELEASE); return 0;
    }
    while (__atomic_load_n(&control->state,__ATOMIC_ACQUIRE)!=PTHREAD_DONE_INIT) sceKernelUsleep(100);
    return 0;
}
long ftell(FILE* file) { return (long)ftello(file); }
int fseek(FILE* file,long offset,int origin) { return fseeko(file,offset,origin); }
