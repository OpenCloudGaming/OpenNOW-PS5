#pragma once
#include "nvst_sdp.hpp"
#include <string>
namespace opennow::sdp {
std::string PrepareGfnOfferSdp(std::string,const std::string&,const std::string&,int);
std::string AdaptAnswerSdpToOffer(const std::string&,const std::string&,const StreamSettings&,const audio::Format* selectedAudio=nullptr);
std::string RewriteGfnMediaCandidate(std::string,const std::string&,int,const std::string& signaling_url = {});
std::string BuildManualMediaCandidate(const std::string&,const std::string&,int,int);
std::string ExtractSdpValue(const std::string&,const std::string&);
webrtc::RiInputCapabilities ParseRiInputCapabilities(const std::string&);
}
