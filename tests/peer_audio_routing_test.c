#include <assert.h>
#include <stdio.h>

#include "../vendor/libpeer/src/peer_connection.c"

uint32_t ports_get_epoch_time(void) {
  return 0;
}

void agent_set_remote_description(Agent* agent, char* description) {
  (void)agent;
  (void)description;
}

void agent_update_candidate_pairs(Agent* agent) {
  (void)agent;
}

static void on_audio(const PeerAudioPacket* packet, void* userdata) {
  (void)packet;
  (void)userdata;
}

static void initialize(PeerConnection* pc) {
  memset(pc, 0, sizeof(*pc));
  pc->config.audio_codec = CODEC_OPUS;
  pc->config.video_codec = CODEC_H264;
  pc->config.onaudiopacket = on_audio;
  pc->config.user_data = pc;
  pc->audio_payload = -1;
  pc->audio_red_payload = -1;
  peer_connection_set_remote_description(pc,
      "m=video 9 UDP/TLS/RTP/SAVPF 96 97\r\n"
      "a=rtpmap:96 H264/90000\r\n"
      "a=rtpmap:97 H264/90000\r\n"
      "m=audio 9 UDP/TLS/RTP/SAVPF 109 112\r\n"
      "a=rtpmap:109 opus/48000/2\r\n"
      "a=rtpmap:112 red/48000/2\r\n", SDP_TYPE_OFFER);
}

static void test_dynamic_routing(void) {
  PeerConnection pc;
  initialize(&pc);
  assert(peer_connection_classify_rtp(&pc, 111, 10) == PEER_RTP_DROP);
  assert(peer_connection_classify_rtp(&pc, 63, 10) == PEER_RTP_DROP);
  assert(peer_connection_set_audio_payload_types(&pc, 109, 112) == 0);
  for (unsigned pt = 0; pt <= 127; ++pt) {
    const PeerRtpRoute expected = pt == 109 || pt == 112 ? PEER_RTP_AUDIO :
        pt == 96 || pt == 97 ? PEER_RTP_VIDEO : PEER_RTP_DROP;
    assert(peer_connection_classify_rtp(&pc, pt, 10) == expected);
  }
  assert(pc.remote_assrc == 0 && pc.remote_vssrc == 0);
  assert(peer_connection_classify_rtp(&pc, 128, 10) == PEER_RTP_DROP);
  assert(peer_connection_classify_rtp(&pc, 109, 0) == PEER_RTP_DROP);

  pc.remote_assrc = 10;
  pc.remote_vssrc = 20;
  assert(peer_connection_classify_rtp(&pc, 109, 10) == PEER_RTP_AUDIO);
  assert(peer_connection_classify_rtp(&pc, 112, 10) == PEER_RTP_AUDIO);
  assert(peer_connection_classify_rtp(&pc, 96, 20) == PEER_RTP_VIDEO);
  assert(peer_connection_classify_rtp(&pc, 97, 20) == PEER_RTP_VIDEO);
  assert(peer_connection_classify_rtp(&pc, 109, 30) == PEER_RTP_DROP);
  assert(peer_connection_classify_rtp(&pc, 96, 30) == PEER_RTP_DROP);
  assert(peer_connection_classify_rtp(&pc, 120, 10) == PEER_RTP_DROP);
  assert(peer_connection_classify_rtp(&pc, 120, 20) == PEER_RTP_DROP);
  assert(peer_connection_classify_rtp(&pc, 96, 10) == PEER_RTP_DROP);
  assert(peer_connection_classify_rtp(&pc, 109, 20) == PEER_RTP_DROP);
  assert(peer_connection_classify_rtp(&pc, 112, 20) == PEER_RTP_DROP);

  pc.remote_assrc = 0;
  assert(peer_connection_classify_rtp(&pc, 109, 20) == PEER_RTP_DROP);
  assert(peer_connection_classify_rtp(&pc, 112, 20) == PEER_RTP_DROP);
  assert(pc.remote_assrc == 0 && pc.remote_vssrc == 20);
  pc.remote_assrc = 10;
  pc.remote_vssrc = 0;
  assert(peer_connection_classify_rtp(&pc, 96, 10) == PEER_RTP_DROP);
  assert(peer_connection_classify_rtp(&pc, 120, 30) == PEER_RTP_DROP);
  assert(pc.remote_assrc == 10 && pc.remote_vssrc == 0);
  pc.remote_vssrc = 10;
  assert(peer_connection_classify_rtp(&pc, 109, 10) == PEER_RTP_DROP);
  assert(peer_connection_classify_rtp(&pc, 96, 10) == PEER_RTP_DROP);
  rtp_decoder_cleanup(&pc.artp_decoder);
}

static void test_payload_validation_and_reset(void) {
  PeerConnection pc;
  initialize(&pc);
  assert(peer_connection_set_audio_payload_types(&pc, 109, 112) == 0);
  pc.remote_assrc = 10;
  pc.remote_vssrc = 20;
  pc.artp_decoder.has_last_seq_number = 1;
  pc.artp_decoder.last_seq_number = 400;
  pc.vrtp_decoder.last_seq_number = 800;
  pc.video_has_last_nack = 1;
  pc.video_receiver_stats.ssrc = 20;
  pc.video_routed = 9;
  assert(peer_connection_set_audio_payload_types(&pc, 109, 112) == 0);
  assert(pc.remote_assrc == 10 && pc.artp_decoder.has_last_seq_number == 1);
  assert(pc.artp_decoder.last_seq_number == 400);

  const int invalid[][2] = {
      {-1, -1}, {-1, 112}, {128, 112}, {109, -2}, {109, 128}, {109, 109},
      {96, -1}, {96, 112}, {109, 96}, {97, 112}, {109, 97}
  };
  assert(peer_connection_set_audio_payload_types(NULL, 109, 112) < 0);
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    assert(peer_connection_set_audio_payload_types(&pc, invalid[i][0], invalid[i][1]) < 0);
    assert(pc.audio_payload == 109 && pc.audio_red_payload == 112);
    assert(pc.remote_assrc == 10 && pc.artp_decoder.has_last_seq_number == 1);
    assert(pc.artp_decoder.last_seq_number == 400);
  }

  assert(peer_connection_set_audio_payload_types(&pc, 110, -1) == 0);
  assert(pc.remote_assrc == 0 && pc.artp_decoder.has_last_seq_number == 0);
  assert(pc.artp_decoder.on_audio_packet == on_audio);
  assert(pc.artp_decoder.user_data == &pc && pc.artp_decoder.decode_func != NULL);
  assert(pc.remote_vssrc == 20 && pc.vrtp_decoder.last_seq_number == 800);
  assert(pc.video_has_last_nack == 1 && pc.video_receiver_stats.ssrc == 20);
  assert(pc.video_routed == 9 && pc.video_payloads[96] && pc.video_payloads[97]);
  assert(peer_connection_classify_rtp(&pc, 109, 10) == PEER_RTP_DROP);
  assert(peer_connection_classify_rtp(&pc, 112, 10) == PEER_RTP_DROP);
  assert(peer_connection_classify_rtp(&pc, 110, 30) == PEER_RTP_AUDIO);
  assert(peer_connection_classify_rtp(&pc, 96, 20) == PEER_RTP_VIDEO);

  pc.remote_assrc = 30;
  pc.artp_decoder.has_last_seq_number = 1;
  assert(peer_connection_set_audio_payload_types(&pc, 110, 113) == 0);
  assert(pc.remote_assrc == 0 && pc.artp_decoder.has_last_seq_number == 0);
  assert(peer_connection_classify_rtp(&pc, 113, 40) == PEER_RTP_AUDIO);
  assert(peer_connection_set_audio_payload_types(&pc, 0, 127) == 0);
  assert(peer_connection_classify_rtp(&pc, 0, 40) == PEER_RTP_AUDIO);
  assert(peer_connection_classify_rtp(&pc, 127, 40) == PEER_RTP_AUDIO);
  assert(peer_connection_set_audio_payload_types(&pc, 127, 0) == 0);
  assert(peer_connection_classify_rtp(&pc, 0, 40) == PEER_RTP_AUDIO);
  assert(peer_connection_classify_rtp(&pc, 127, 40) == PEER_RTP_AUDIO);
  rtp_decoder_cleanup(&pc.artp_decoder);
}

static void test_sdp_collisions_and_updates(void) {
  PeerConnection pc;
  initialize(&pc);
  assert(peer_connection_set_audio_payload_types(&pc, 109, 112) == 0);
  peer_connection_set_remote_description(&pc,
      "m=audio 0 UDP/TLS/RTP/SAVPF 111\r\n"
      "a=rtpmap:111 opus/48000/2\r\n"
      "a=ssrc:99 cname:disabled-audio\r\n"
      "m=video 9 UDP/TLS/RTP/SAVPF 96 109 112\r\n"
      "a=rtpmap:96 H264/90000\r\n"
      "a=rtpmap:109 H264/90000\r\n"
      "a=rtpmap:112 red/90000\r\n"
      "a=ssrc:20 cname:video\r\n"
      "a=ssrc:21 cname:repair\r\n"
      "m=audio 9 UDP/TLS/RTP/SAVPF 109 112\r\n"
      "a=rtpmap:109 opus/48000/2\r\n"
      "a=rtpmap:112 red/48000/2\r\n"
      "a=ssrc:10 cname:audio\r\n"
      "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
      "a=ssrc:99 cname:unrelated\r\n", SDP_TYPE_OFFER);
  assert(pc.remote_assrc == 10 && pc.remote_vssrc == 20);
  pc.artp_decoder.has_last_seq_number = 1;
  assert(peer_connection_set_audio_payload_types(&pc, 109, 112) < 0);
  assert(peer_connection_set_audio_payload_types(&pc, 110, 112) < 0);
  assert(peer_connection_set_audio_payload_types(&pc, 109, -1) < 0);
  assert(pc.audio_payload == 109 && pc.audio_red_payload == 112);
  assert(pc.remote_assrc == 10 && pc.artp_decoder.has_last_seq_number == 1);
  assert(peer_connection_classify_rtp(&pc, 109, 10) == PEER_RTP_DROP);
  assert(peer_connection_classify_rtp(&pc, 109, 20) == PEER_RTP_DROP);
  assert(peer_connection_classify_rtp(&pc, 112, 10) == PEER_RTP_DROP);
  assert(peer_connection_classify_rtp(&pc, 112, 20) == PEER_RTP_DROP);
  assert(peer_connection_classify_rtp(&pc, 96, 20) == PEER_RTP_VIDEO);

  pc.remote_assrc = 0;
  assert(peer_connection_classify_rtp(&pc, 109, 20) == PEER_RTP_DROP);
  assert(peer_connection_classify_rtp(&pc, 112, 30) == PEER_RTP_DROP);
  assert(pc.remote_assrc == 0 && pc.remote_vssrc == 20);
  peer_connection_set_remote_description(&pc,
      "m=video 9 UDP/TLS/RTP/SAVPF 98\r\n"
      "a=rtpmap:98 H264/90000\r\n"
      "m=video 0 UDP/TLS/RTP/SAVPF 109 112\r\n"
      "a=rtpmap:109 H264/90000\r\n"
      "a=rtpmap:112 red/90000\r\n"
      "a=ssrc:99 cname:disabled\r\n", SDP_TYPE_OFFER);
  assert(peer_connection_classify_rtp(&pc, 96, 20) == PEER_RTP_DROP);
  assert(peer_connection_classify_rtp(&pc, 98, 20) == PEER_RTP_VIDEO);
  assert(peer_connection_classify_rtp(&pc, 109, 10) == PEER_RTP_AUDIO);
  assert(peer_connection_classify_rtp(&pc, 112, 10) == PEER_RTP_AUDIO);
  assert(pc.remote_vssrc == 20);
  peer_connection_set_remote_description(&pc,
      "m=video 0 UDP/TLS/RTP/SAVPF 98\r\n"
      "a=rtpmap:98 H264/90000\r\n", SDP_TYPE_OFFER);
  assert(peer_connection_classify_rtp(&pc, 98, 20) == PEER_RTP_DROP);
  rtp_decoder_cleanup(&pc.artp_decoder);
}

static void test_sdp_audio_ssrc_reset(void) {
  PeerConnection pc;
  initialize(&pc);
  assert(peer_connection_set_audio_payload_types(&pc, 109, 112) == 0);
  pc.remote_assrc = 10;
  pc.artp_decoder.has_last_seq_number = 1;
  pc.video_has_last_nack = 1;
  peer_connection_set_remote_description(&pc,
      "m=audio 9 UDP/TLS/RTP/SAVPF 109 112\r\n"
      "a=rtpmap:109 opus/48000/2\r\n"
      "a=rtpmap:112 red/48000/2\r\n"
      "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
      "a=ssrc:99 cname:unrelated\r\n", SDP_TYPE_OFFER);
  assert(pc.remote_assrc == 10 && pc.artp_decoder.has_last_seq_number == 1);
  peer_connection_set_remote_description(&pc,
      "m=audio 9 UDP/TLS/RTP/SAVPF 109 112\r\n"
      "a=rtpmap:109 opus/48000/2\r\n"
      "a=rtpmap:112 red/48000/2\r\n"
      "a=ssrc:30 cname:audio\r\n", SDP_TYPE_OFFER);
  assert(pc.remote_assrc == 30 && pc.artp_decoder.has_last_seq_number == 0);
  assert(pc.audio_payload == 109 && pc.audio_red_payload == 112);
  assert(pc.artp_decoder.on_audio_packet == on_audio);
  assert(pc.video_has_last_nack == 1);
  assert(peer_connection_classify_rtp(&pc, 109, 10) == PEER_RTP_DROP);
  assert(peer_connection_classify_rtp(&pc, 109, 30) == PEER_RTP_AUDIO);
  assert(peer_connection_classify_rtp(&pc, 112, 30) == PEER_RTP_AUDIO);
  rtp_decoder_cleanup(&pc.artp_decoder);
}

static void test_other_codecs(void) {
  PeerConnection pc;
  initialize(&pc);
  pc.config.audio_codec = CODEC_PCMU;
  assert(peer_connection_classify_rtp(&pc, 0, 10) == PEER_RTP_AUDIO);
  assert(peer_connection_classify_rtp(&pc, 8, 10) == PEER_RTP_DROP);
  pc.config.audio_codec = CODEC_PCMA;
  assert(peer_connection_classify_rtp(&pc, 8, 10) == PEER_RTP_AUDIO);
  assert(peer_connection_classify_rtp(&pc, 0, 10) == PEER_RTP_DROP);
  pc.config.audio_codec = CODEC_NONE;
  assert(peer_connection_classify_rtp(&pc, 8, 10) == PEER_RTP_DROP);
  pc.config.video_codec = CODEC_NONE;
  assert(peer_connection_classify_rtp(&pc, 96, 20) == PEER_RTP_DROP);
}

static void test_selected_video_codec_routing(void) {
  static const char offer[] =
      "v=0\r\n"
      "o=- 1 1 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "m=video 9 UDP/TLS/RTP/SAVPF 96 97 98 99 100 101 102\r\n"
      "a=rtpmap:96 H264/90000\r\n"
      "a=rtpmap:97 rtx/90000\r\n"
      "a=fmtp:97 apt=96\r\n"
      "a=rtpmap:98 HEVC/90000\r\n"
      "a=rtpmap:99 VP9/90000\r\n"
      "a=rtpmap:100 red/90000\r\n"
      "a=rtpmap:101 ulpfec/90000\r\n"
      "a=rtpmap:102 H265/90000\r\n"
      "m=audio 9 UDP/TLS/RTP/SAVPF 109 112\r\n"
      "a=rtpmap:109 opus/48000/2\r\n"
      "a=rtpmap:112 red/48000/2\r\n";
  PeerConnection pc;
  initialize(&pc);
  peer_connection_set_remote_description(&pc, offer, SDP_TYPE_OFFER);
  assert(peer_connection_set_audio_payload_types(&pc, 109, 112) == 0);
  for (unsigned pt = 97; pt <= 102; ++pt) {
    assert(peer_connection_classify_rtp(&pc, pt, 21) == PEER_RTP_DROP);
    assert(pc.remote_vssrc == 0 && pc.remote_assrc == 0);
  }
  assert(peer_connection_classify_rtp(&pc, 96, 20) == PEER_RTP_VIDEO);
  pc.remote_vssrc = 20;
  for (unsigned pt = 97; pt <= 102; ++pt)
    assert(peer_connection_classify_rtp(&pc, pt, 20) == PEER_RTP_DROP);
  for (int pt = 96; pt <= 102; ++pt) {
    assert(peer_connection_set_audio_payload_types(&pc, pt, -1) < 0);
    assert(peer_connection_set_audio_payload_types(&pc, 109, pt) < 0);
    assert(pc.audio_payload == 109 && pc.audio_red_payload == 112);
  }
  assert(peer_connection_classify_rtp(&pc, 109, 10) == PEER_RTP_AUDIO);

  pc.config.video_codec = CODEC_HEVC;
  pc.remote_vssrc = 0;
  peer_connection_set_remote_description(&pc, offer, SDP_TYPE_OFFER);
  for (unsigned pt = 96; pt <= 102; ++pt) {
    const PeerRtpRoute expected = pt == 98 || pt == 102 ? PEER_RTP_VIDEO : PEER_RTP_DROP;
    assert(peer_connection_classify_rtp(&pc, pt, 20) == expected);
    assert(peer_connection_set_audio_payload_types(&pc, (int)pt, -1) < 0);
  }
  rtp_decoder_cleanup(&pc.artp_decoder);
}

int main(void) {
  test_selected_video_codec_routing();
  test_dynamic_routing();
  test_payload_validation_and_reset();
  test_sdp_collisions_and_updates();
  test_sdp_audio_ssrc_reset();
  test_other_codecs();
  puts("peer audio routing tests passed");
  return 0;
}
