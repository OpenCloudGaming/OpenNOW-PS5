#pragma once
#include <type_traits>
#include "audio_format.hpp"
namespace opennow {
// Streaming targets. Native profiles are exposed only after console startup
// qualification; sustained frame rate and received codec are measured separately.
enum class StreamProfile { quality, smooth, experimental, compatibility,
 native_hdr120, native_hdr60, native_4k120, native_1080,
 native_hdr90, native_4k90, count };
enum class VideoCodec { h264, hevc };
enum class VideoMode { h264Software, h264Hardware, hevcMain10SdrHardware, hevcMain10HdrHardware };
enum class QualityMode { original, adaptive, clarity };
enum class NetworkPolicy { adaptive, fixed };
struct StreamSettings {
 int width=1920,height=1080,fps=30,bitrate_kbps=25000;
 VideoMode mode=VideoMode::h264Software;
 QualityMode quality=QualityMode::original;
 NetworkPolicy network=NetworkPolicy::adaptive;
 audio::Mode audio_mode=audio::Mode::stereo;
 VideoCodec codec() const noexcept {return tenBit()?VideoCodec::hevc:VideoCodec::h264;}
 bool hardware() const noexcept {return mode!=VideoMode::h264Software;}
 bool hdr() const noexcept {return mode==VideoMode::hevcMain10HdrHardware;}
 bool tenBit() const noexcept {return mode==VideoMode::hevcMain10SdrHardware||hdr();}
 bool operator==(const StreamSettings&) const = default;
};
static_assert(std::is_trivially_copyable_v<StreamSettings> && std::is_standard_layout_v<StreamSettings>);
enum class SettingsError { none, mode, quality, network, dimensions, fps, bitrate, audio };
inline SettingsError validateSettings(const StreamSettings& settings) noexcept {
 if(settings.mode<VideoMode::h264Software||settings.mode>VideoMode::hevcMain10HdrHardware)return SettingsError::mode;
 if(settings.quality<QualityMode::original||settings.quality>QualityMode::clarity)return SettingsError::quality;
 if(settings.network<NetworkPolicy::adaptive||settings.network>NetworkPolicy::fixed)return SettingsError::network;
 if(settings.mode==VideoMode::h264Hardware&&settings.network==NetworkPolicy::adaptive)return SettingsError::network;
 if(settings.width<320||settings.height<180||settings.width>(settings.hardware()?3840:1920)||
    settings.height>(settings.hardware()?2160:1080)||(settings.width&1)||(settings.height&1))return SettingsError::dimensions;
 if(settings.fps<30||settings.fps>(settings.hardware()?120:60))return SettingsError::fps;
 if(settings.bitrate_kbps<4000||settings.bitrate_kbps>100000)return SettingsError::bitrate;
 if(settings.audio_mode<audio::Mode::automatic||settings.audio_mode>audio::Mode::surround71)return SettingsError::audio;
 return SettingsError::none;
}
inline StreamSettings settingsFor(StreamProfile profile) {
 switch(profile) {
 case StreamProfile::native_hdr120: return {3840,2160,120,100000,VideoMode::hevcMain10HdrHardware,QualityMode::original,NetworkPolicy::fixed};
 case StreamProfile::native_hdr90: return {3840,2160,90,100000,VideoMode::hevcMain10HdrHardware,QualityMode::original,NetworkPolicy::fixed};
 case StreamProfile::native_hdr60: return {3840,2160,60,100000,VideoMode::hevcMain10HdrHardware,QualityMode::original,NetworkPolicy::fixed};
 case StreamProfile::native_4k120: return {3840,2160,120,100000,VideoMode::h264Hardware,QualityMode::original,NetworkPolicy::fixed};
 case StreamProfile::native_4k90: return {3840,2160,90,100000,VideoMode::h264Hardware,QualityMode::original,NetworkPolicy::fixed};
 case StreamProfile::native_1080: return {1920,1080,60,75000,VideoMode::h264Hardware,QualityMode::original,NetworkPolicy::fixed};
 case StreamProfile::smooth: return {1280,720,60,20000};
 case StreamProfile::experimental: return {1920,1080,60,75000};
 case StreamProfile::compatibility: return {1280,720,30,10000};
 default: return {};
 }
}
inline StreamProfile nextProfile(StreamProfile profile) {
 switch(profile) {
 case StreamProfile::native_hdr120: return StreamProfile::native_hdr90;
 case StreamProfile::native_hdr90: return StreamProfile::native_hdr60;
 case StreamProfile::native_hdr60: return StreamProfile::native_4k120;
 case StreamProfile::native_4k120: return StreamProfile::native_4k90;
 case StreamProfile::native_4k90: return StreamProfile::native_1080;
 case StreamProfile::native_1080: return StreamProfile::quality;
 case StreamProfile::quality: return StreamProfile::smooth;
 case StreamProfile::smooth: return StreamProfile::experimental;
 case StreamProfile::experimental: return StreamProfile::compatibility;
 default: return StreamProfile::native_hdr120;
 }
}
inline const char* profileLabel(StreamProfile profile) {
 switch(profile) {
 case StreamProfile::native_hdr120: return "4K120 HDR / HEVC / 100 MBPS";
 case StreamProfile::native_hdr90: return "4K90 HDR / HEVC / 100 MBPS";
 case StreamProfile::native_hdr60: return "4K60 HDR / HEVC / 100 MBPS";
 case StreamProfile::native_4k120: return "4K120 SDR / HARDWARE / 100 MBPS";
 case StreamProfile::native_4k90: return "4K90 SDR / HARDWARE / 100 MBPS";
 case StreamProfile::native_1080: return "1080P60 SDR / HARDWARE / 75 MBPS";
 case StreamProfile::smooth: return "SMOOTH 720P60 SDR / 20 MBPS";
 case StreamProfile::experimental: return "EXPERIMENTAL 1080P60 SDR / 75 MBPS";
 case StreamProfile::compatibility: return "COMPATIBILITY 720P30 SDR / 10 MBPS";
 default: return "QUALITY 1080P30 SDR / 25 MBPS";
 }
}
}
