#include <stdint.h>
#include <string.h>
#include <assert.h>
// The native FreeBSD sysroot has neither of these legacy macros.
#undef __BYTE_ORDER
#undef __BIG_ENDIAN
#undef __LITTLE_ENDIAN
#include "rtp.h"
#include "rtcp.h"
int main(void) {
 uint8_t wire[12]={0x80,0xef,0,1,0,0,0,2,0,0,0,3};
 RtpHeader header;memcpy(&header,wire,sizeof(header));
 assert(sizeof(header)==12 && header.version==2 && header.type==111 && header.markerbit==1);
 assert(header.extension==0 && header.csrccount==0);
 memset(&header,0,sizeof(header));header.version=2;header.type=96;header.markerbit=1;
 memcpy(wire,&header,sizeof(header));assert(wire[0]==0x80 && wire[1]==0xe0);
 RtcpHeader control={0};control.version=2;control.rc=1;control.type=206;
 memcpy(wire,&control,sizeof(control));assert(wire[0]==0x81 && wire[1]==206);
 return 0;
}
