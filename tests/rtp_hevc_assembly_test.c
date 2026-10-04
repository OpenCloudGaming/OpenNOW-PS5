#include "rtp.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static unsigned now,frames;static unsigned char output[256];static size_t length;
uint32_t ports_get_epoch_time(void){return now;}
static void frame(const PeerVideoPacket* p,void* unused){(void)unused;assert(p->size<=sizeof(output));memcpy(output,p->data,p->size);length=p->size;++frames;}
static void receive(RtpDecoder* d,unsigned seq,unsigned stamp,int marker,const unsigned char* data,size_t size){
 unsigned char p[256]={0x80,96};p[1]|=marker?0x80:0;p[2]=seq>>8;p[3]=seq;p[4]=stamp>>24;p[5]=stamp>>16;p[6]=stamp>>8;p[7]=stamp;p[11]=42;memcpy(p+12,data,size);rtp_decoder_decode(d,p,size+12);
}
int main(){
 RtpDecoder d;const unsigned char vps[]={64,1,0x80},sps[]={66,1,0x80},pps[]={68,1,0x80},idr[]={38,1,0x80};
 rtp_decoder_init(&d,CODEC_HEVC,NULL,NULL);rtp_decoder_set_video_callback(&d,frame);
 receive(&d,65532,1,1,vps,3);receive(&d,65533,2,1,sps,3);receive(&d,65534,3,1,pps,3);receive(&d,65535,4,1,idr,3);
 assert(frames==1&&length==28&&output[4]==64&&output[11]==66&&output[18]==68&&output[25]==38);
 const unsigned char start[]={98,1,0x93,0x11},end[]={98,1,0x53,0x33},mid[]={98,1,0x13,0x22};
 receive(&d,0,5,0,start,4);receive(&d,2,5,1,end,4);receive(&d,1,5,0,mid,4);
 assert(frames==2&&length==30&&output[25]==38&&output[27]==0x11&&output[28]==0x22&&output[29]==0x33);
 const unsigned char ap[]={96,1,0,3,2,1,0x80,0,3,2,1,0x81};receive(&d,3,6,1,ap,sizeof(ap));assert(frames==3&&length==14);
 const unsigned char bad[]={96,1,0,10,2,1,0x80};receive(&d,4,7,1,bad,sizeof(bad));assert(frames==3);
 receive(&d,5,8,0,start,4);receive(&d,7,8,1,end,4);now+=RTP_REORDER_MAX_HOLD_MS;rtp_decoder_poll(&d);assert(frames==3);
 const unsigned char layered[]={3,9,0x80};receive(&d,8,9,1,layered,sizeof(layered));assert(frames==3);
 receive(&d,9,10,1,idr,3);assert(frames==4);
 rtp_decoder_cleanup(&d);puts("HEVC assembly, parameter recovery, reorder, wrap and malformed/lost packets passed");
}
