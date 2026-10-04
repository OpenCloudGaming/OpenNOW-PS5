#include "stream/nvst_sdp.hpp"
#include "stream/sdp.hpp"

#include <cassert>
#include <string>

namespace
{

bool HasLine(const std::string& sdp, const std::string& line)
{
    return sdp.find(line + "\n") != std::string::npos;
}

bool HasAttribute(const std::string& sdp, const std::string& attribute)
{
    return sdp.find("a=" + attribute) != std::string::npos;
}

} // namespace

int main()
{
    const std::string mapped_offer =
        "v=0\r\nc=IN IP4 0.0.0.0\r\n"
        "a=candidate:1 1 udp 2122260223 203.0.113.10 47998 typ host\r\n";
    const auto mapped = opennow::sdp::PrepareGfnOfferSdp(
        mapped_offer, "wss://signaling.example/nvst/", "198.51.100.55", 443);
    assert(mapped.find("a=candidate:1 1 udp 2122260223 198.51.100.55 443 typ host") != std::string::npos);
    assert(mapped.find("c=IN IP4 0.0.0.0") != std::string::npos);
    assert(opennow::sdp::PrepareGfnOfferSdp(mapped_offer, "wss://signaling.example/nvst/", "", 0) == mapped_offer);
    assert(opennow::sdp::PrepareGfnOfferSdp(mapped_offer, "wss://signaling.example/nvst/", "198.51.100.55", 0) == mapped_offer);
    const std::string trickled = "candidate:1 1 udp 2122260223 203.0.113.10 47998 typ host generation 0";
    assert(opennow::sdp::RewriteGfnMediaCandidate(trickled, "198.51.100.55", 18784) ==
        "candidate:1 1 udp 2122260223 198.51.100.55 18784 typ host generation 0");
    assert(opennow::sdp::RewriteGfnMediaCandidate(trickled, "198.51.100.55", 65536) == trickled);
    assert(opennow::sdp::RewriteGfnMediaCandidate("candidate:malformed", "198.51.100.55", 443) == "candidate:malformed");
    const std::string zero_offer = "v=0\nc=IN IP4 0.0.0.0\na=candidate:1 1 UDP 1 0.0.0.0 9 typ host";
    assert(opennow::sdp::PrepareGfnOfferSdp(zero_offer, "", "198-51-100-55.example", 443) ==
        "v=0\nc=IN IP4 0.0.0.0\na=candidate:1 1 UDP 1 198.51.100.55 443 typ host");
    for (const auto& signaling : {"wss://198.51.100.55/nvst/", "wss://198-51-100-55.example/nvst/"}) {
        assert(opennow::sdp::PrepareGfnOfferSdp(zero_offer, signaling, "", 0) ==
            "v=0\nc=IN IP4 0.0.0.0\na=candidate:1 1 UDP 1 198.51.100.55 9 typ host");
    }
    assert(opennow::sdp::PrepareGfnOfferSdp(zero_offer, "", "198.51.100.55", 0) ==
        "v=0\nc=IN IP4 0.0.0.0\na=candidate:1 1 UDP 1 198.51.100.55 9 typ host");
    assert(opennow::sdp::PrepareGfnOfferSdp(zero_offer, "wss://unparseable.example/nvst/", "", 0) == zero_offer);
    assert(opennow::sdp::RewriteGfnMediaCandidate("candidate:1 1 UDP 1 0.0.0.0 47998 typ host", "", 0,
        "wss://198-51-100-55.example/nvst/") == "candidate:1 1 UDP 1 198.51.100.55 47998 typ host");
    assert(opennow::sdp::BuildManualMediaCandidate("", "198.51.100.55", 443, 100) ==
        "a=candidate:100 1 UDP 2130706431 198.51.100.55 443 typ host");
    assert(opennow::sdp::BuildManualMediaCandidate("wss://198.51.100.55/", "", 0, 100).empty());

    opennow::StreamSettings settings;
    settings.width = 1280;
    settings.height = 720;
    settings.fps = 60;
    settings.bitrate_kbps = 12000;
    settings.image_quality_mode = "Adaptive";

    const std::string answer =
        "v=0\r\n"
        "a=ice-ufrag:test-ufrag\r\n"
        "a=ice-pwd:test-password\r\n"
        "a=fingerprint:sha-256 AA:BB:CC\r\n";
    const std::string sdp = opennow::webrtc::BuildNvstSdp(
        answer, settings, opennow::webrtc::RiInputCapabilities {});

    for (const auto& line : {
        "a=video.maxFPS:60",
        "a=video.initialBitrateKbps:4000",
        "a=video.initialPeakBitrateKbps:4000",
        "a=vqos.bw.maximumBitrateKbps:12000",
        "a=vqos.bw.minimumBitrateKbps:4000",
        "a=vqos.dynamicStreamingMode:3",
        "a=vqos.drc.enable:1",
        "a=vqos.resControl.cpmRtc.featureMask:3",
    }) {
        assert(HasLine(sdp, line));
    }

    for (const auto& attribute : {
        "vqos.bw.peakBitrateKbps:",
        "vqos.bw.serverPeakBitrateKbps:",
        "vqos.bw.enableBandwidthEstimation:",
        "vqos.bw.disableBitrateLimit:",
        "vqos.grc.maximumBitrateKbps:",
        "vqos.grc.enable:",
        "vqos.dfc.enable:",
        "vqos.dfc.adjustResAndFps:",
        "vqos.resControl.cpmRtc.enable:",
        "vqos.resControl.cpmRtc.minResolutionPercent:",
        "vqos.resControl.cpmRtc.resolutionChangeHoldonMs:",
    }) {
        assert(!HasAttribute(sdp, attribute));
    }

    settings.bitrate_kbps = 20000;
    const std::string quality_sdp = opennow::webrtc::BuildNvstSdp(
        answer, settings, opennow::webrtc::RiInputCapabilities {});
    assert(HasLine(quality_sdp, "a=video.initialBitrateKbps:5000"));
    assert(HasLine(quality_sdp, "a=vqos.bw.maximumBitrateKbps:20000"));
    for(auto profile:{opennow::StreamProfile::quality,opennow::StreamProfile::smooth,
                     opennow::StreamProfile::experimental,opennow::StreamProfile::compatibility}){
        const auto target=opennow::settingsFor(profile);
        auto negotiated=opennow::webrtc::BuildNvstSdp(answer,target,{});
        assert(HasLine(negotiated,"a=video.clientViewportWd:"+std::to_string(target.width)));
        assert(HasLine(negotiated,"a=video.clientViewportHt:"+std::to_string(target.height)));
        assert(HasLine(negotiated,"a=video.maxFPS:"+std::to_string(target.fps)));
        assert(HasLine(negotiated,"a=vqos.bw.maximumBitrateKbps:"+std::to_string(target.bitrate_kbps)));
        assert(HasLine(negotiated,"a=video.dynamicRangeMode:0"));
        assert(HasLine(negotiated,"a=video.bitDepth:8"));
        assert(HasLine(negotiated,"a=video.maxNumReferenceFrames:4"));
        const std::string offer="v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 98\r\na=rtpmap:98 H264/90000\r\n"
            "m=audio 9 UDP/TLS/RTP/SAVPF 111 63\r\na=rtpmap:111 opus/48000/2\r\na=rtpmap:63 red/48000/2\r\na=fmtp:63 111/111\r\n";
        const std::string raw="v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\nc=IN IP4 0.0.0.0\r\na=rtpmap:96 H264/90000\r\n"
            "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=rtpmap:111 opus/48000/2\r\na=fmtp:111 stereo=0\r\n";
        const auto adapted=opennow::sdp::AdaptAnswerSdpToOffer(raw,offer,target);
        assert(HasLine(adapted,"b=AS:"+std::to_string(target.bitrate_kbps)+"\r"));
        assert(adapted.find("stereo=1;sprop-stereo=1;maxaveragebitrate=256000")!=std::string::npos);
        assert(adapted.find("stereo=0")==std::string::npos);
        assert(adapted.find("a=rtpmap:63 red/48000/2")!=std::string::npos);
    }
    const auto hdr=opennow::settingsFor(opennow::StreamProfile::native_hdr120);
    for(auto profile:{opennow::StreamProfile::native_hdr120,opennow::StreamProfile::native_hdr60,
                     opennow::StreamProfile::native_4k120,opennow::StreamProfile::native_1080,
                     opennow::StreamProfile::native_hdr90,opennow::StreamProfile::native_4k90}){
        const auto target=opennow::settingsFor(profile);
        const auto native=opennow::webrtc::BuildNvstSdp(answer,target,{});
        assert(HasLine(native,"a=vqos.bw.minimumBitrateKbps:"+std::to_string(target.bitrate_kbps*3/4)));
        assert(HasLine(native,"a=video.initialBitrateKbps:"+std::to_string(target.bitrate_kbps*3/4)));
        assert(HasLine(native,"a=video.maxFPS:"+std::to_string(target.fps)));
        assert(HasLine(native,"a=video.maxNumReferenceFrames:1"));
        assert(HasLine(native,"a=video.dynamicRangeMode:"+std::to_string(target.hdr?1:0)));
        assert(HasLine(native,"a=video.bitStreamFormat:"+std::to_string(target.codec==opennow::VideoCodec::hevc?1:0)));
        assert(HasLine(native,"a=vqos.dynamicStreamingMode:0"));
        assert(HasLine(native,"a=vqos.drc.enable:0"));
        assert(HasLine(native,"a=vqos.resControl.cpmRtc.minResolutionPercent:100"));
        assert(native.find("a=vqos.drc.enable:1")==std::string::npos);
        assert(native.find("a=vqos.bw.serverPeakBitrateKbps:")<native.find("m=audio"));
    }
    // Every real profile is reachable once through the user-visible L1 cycle.
    bool visited[static_cast<unsigned>(opennow::StreamProfile::count)]{};
    auto selected=opennow::StreamProfile::native_hdr120;
    for(unsigned i=0;i<static_cast<unsigned>(opennow::StreamProfile::count);++i){
        const auto index=static_cast<unsigned>(selected);assert(index<static_cast<unsigned>(opennow::StreamProfile::count)&&!visited[index]);visited[index]=true;
        selected=opennow::nextProfile(selected);
    }
    assert(selected==opennow::StreamProfile::native_hdr120);
    const std::string hevcOffer="v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 98 100\r\na=rtpmap:98 H264/90000\r\na=rtpmap:100 H265/90000\r\na=fmtp:100 profile-id=2;level-id=156;sprop-max-don-diff=0\r\n";
    const std::string rawHevc="v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\na=rtpmap:96 H265/90000\r\na=fmtp:96 profile-id=2;level-id=156;sprop-max-don-diff=0\r\n";
    assert(opennow::sdp::AdaptAnswerSdpToOffer(rawHevc,rawHevc,settings).empty());
    const auto hdrAnswer=opennow::sdp::AdaptAnswerSdpToOffer(rawHevc,hevcOffer,hdr);
    assert(hdrAnswer.find("a=rtpmap:100 H265/90000")!=std::string::npos);
    assert(hdrAnswer.find("profile-id=2;level-id=156")!=std::string::npos);
    const auto hdrSdp=opennow::webrtc::BuildNvstSdp(hdrAnswer,hdr,{});
    for(const auto* a:{"a=video.dynamicRangeMode:1","a=video.bitDepth:10","a=video.chromaFormat:1","a=video.bitStreamFormat:1","a=video.maxFPS:120"})assert(HasLine(hdrSdp,a));
    assert(opennow::sdp::AdaptAnswerSdpToOffer(rawHevc,"v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 98\r\na=rtpmap:98 H264/90000\r\n",hdr).empty());
    auto interleaved=hevcOffer;interleaved.replace(interleaved.find("sprop-max-don-diff=0"),20,"sprop-max-don-diff=1");
    assert(opennow::sdp::AdaptAnswerSdpToOffer(rawHevc,interleaved,hdr).empty());
    return 0;
}
