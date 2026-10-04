#include "rtp.h"
#include <assert.h>
uint32_t ports_get_epoch_time(void){return 0;}
void opennow_media_note(const char* message){(void)message;}
int main(void){
 RtpDecoder first,second;
 rtp_decoder_init(&first,CODEC_H264,0,0);
 assert(first.decode_func && first.nalu_buf && first.au_buf && first.reorder_buf);
 rtp_decoder_init(&second,CODEC_H264,0,0);
 assert(!second.decode_func && !second.nalu_buf);
 rtp_decoder_cleanup(&second);
 assert(first.decode_func && first.nalu_buf);
 rtp_decoder_cleanup(&first);
 rtp_decoder_init(&second,CODEC_H264,0,0);
 assert(second.decode_func && second.reorder_buf);
 rtp_decoder_cleanup(&second);
 return 0;
}
