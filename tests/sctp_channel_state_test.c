#include "sctp.h"
#include <assert.h>
#include <string.h>
#include <arpa/inet.h>
int main(void){
 Sctp s={0};s.connected=1;char open[100]={3,(char)0x82};
 const char* label="control_channel_partially_reliable";
 uint32_t ttl=htonl(300);uint16_t length=htons(strlen(label));
 memcpy(open+4,&ttl,4);memcpy(open+8,&length,2);memcpy(open+12,label,strlen(label));
 const size_t bytes=12+strlen(label);
 assert(sctp_register_local_stream(&s,6,open,bytes)==0);
 assert(s.stream_count==1&&s.stream_table[0].reliability==300&&s.stream_table[0].channel_type==0x82);
 assert(!strcmp(s.stream_table[0].label,label));assert(!sctp_datachannel_is_open(&s,6));
 sctp_datachannel_ack(&s,0);assert(!sctp_datachannel_is_open(&s,6));
 sctp_datachannel_ack(&s,6);assert(sctp_datachannel_is_open(&s,6));
 assert(sctp_register_local_stream(&s,2,open,bytes-1)<0&&s.stream_count==1);
 s.connected=0;assert(!sctp_datachannel_is_open(&s,6));
}
