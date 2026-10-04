// SPDX-License-Identifier: GPL-3.0-or-later
// Fixed socket operation names and libcurl errors only. Never headers or bodies.
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
extern int sceKernelOpen(const char*,int,unsigned);
extern int64_t sceKernelWrite(int,const void*,size_t);
extern int sceKernelClose(int);
static unsigned entries;
void opennow_network_note(const char* message) {
    if (__atomic_fetch_add(&entries,1,__ATOMIC_RELAXED)>=64) return;
    int fd=sceKernelOpen("/data/opennow/network.log",O_WRONLY|O_CREAT|O_APPEND,0644);
    if (fd<0) return;
    size_t size=strnlen(message,384);
    sceKernelWrite(fd,message,size);sceKernelWrite(fd,"\n",1);sceKernelClose(fd);
}
void opennow_network_result(const char* operation,int result,int error) {
    char line[128];snprintf(line,sizeof(line),"%s result=%d errno=%d",operation,result,error);
    opennow_network_note(line);
}

// Whitelisted media counters/SDP codec lines only, never ICE credentials or payloads.
void opennow_media_note(const char* message) {
    static unsigned count;
    if (__atomic_fetch_add(&count,1,__ATOMIC_RELAXED)>=160) return;
    int fd=sceKernelOpen("/data/opennow/media.log",O_WRONLY|O_CREAT|O_APPEND,0644);
    if(fd<0)return;
    sceKernelWrite(fd,message,strnlen(message,384));sceKernelWrite(fd,"\n",1);sceKernelClose(fd);
}

// SPS contains codec configuration, dimensions and color metadata only.
// Store the first bounded HEVC SPS, never VCL/game pictures or audio.
void opennow_hevc_sps_note(const uint8_t* nal,size_t size) {
 if(!nal||size<5||size>2048||((nal[0]>>1)&63)!=33)return;
 int fd=sceKernelOpen("/data/opennow/video-sps.bin",O_WRONLY|O_CREAT|O_TRUNC,0644);
 if(fd<0)return;sceKernelWrite(fd,nal,size);sceKernelClose(fd);
}
