// SPDX-License-Identifier: GPL-3.0-or-later
#include "stream/stream.hpp"
#include "vendor/cJSON.h"

#include <cassert>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {
int peers = 0, sockets = 0, runtimes = 0;
bool inPoll = false, inLoop = false, dropSocket = false;
bool mediaStarts = true, peerStarts = true, socketStarts = true, entropyWorks = true;
bool assemblerReady = true, nullAnswer = false;
bool mediaRunning = false;
bool audioConfigureSucceeds=true,audioPayloadsSucceed=true,allowEightChannelOutput=true;
int selectedAudioPayload=-1,selectedRedPayload=-1,audioHandles=0;
opennow::audio::Format configuredAudio;
opennow::StreamSettings mediaSettings;
int runtimeResult = 0;
PeerConnectionState nextState = PEER_CONNECTION_NEW;
std::string incoming;
std::vector<std::string> candidates;
std::string remoteSdp;
std::vector<std::string> outbound;
std::vector<std::string> diagnostics;
std::string connectedUrl;
std::vector<opennow::wire::Bytes> sent;
std::vector<opennow::wire::Bytes> controlSent;
int inputSendBudget=-1;
bool controlSendFails=false;
bool controlOpen=false;
bool inputOpen=true,partialInputOpen=true,channelCreateFails=false,candidateFails=false;
bool mediaVideoAccepts=true,secureAnswer=true;
unsigned keyframeRequests=0,referenceResets=0,mediaVideoCalls=0;
std::uint64_t processTime=0;
PeerVideoRtpStats rtpStats{};
void (*videoCallback)(const PeerVideoPacket*,void*)=nullptr;
void* videoContext=nullptr;
struct ChannelRequest {std::uint16_t sid;DecpChannelType type;std::uint32_t reliability;std::string label;};
std::vector<ChannelRequest> channelRequests;
std::vector<std::uint16_t> sentSids;
void (*dataCallback)(char*, size_t, void*, uint16_t) = nullptr;
void (*dataOpened)(void*) = nullptr;
void (*dataClosed)(void*) = nullptr;
void* dataContext = nullptr;
void (*localIce)(char*, void*) = nullptr;
void* localIceContext = nullptr;
WebSocketClient* websocket = nullptr;
constexpr const char* answer =
    "v=0\r\na=group:BUNDLE video audio datachannel\r\n"
    "a=ice-ufrag:test\r\na=ice-pwd:test-password\r\n"
    "a=fingerprint:sha-256 AA:BB:CC\r\n"
    "m=video 9 UDP/TLS/RTP/SAVPF 96\r\na=mid:video\r\na=rtpmap:96 H264/90000\r\n"
    "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=mid:audio\r\na=rtpmap:111 opus/48000/2\r\n"
    "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\na=mid:datachannel\r\n";

opennow::Session session() {
    opennow::Session value;
    std::strcpy(value.id, "session-test");
    std::strcpy(value.signaling, "wss://signaling.example/nvst/");
    return value;
}

void queuePayload(const char* key, const char* value, const char* from="1") {
    auto* payload = cJSON_CreateObject();
    if (std::strcmp(key, "sdp") == 0) cJSON_AddStringToObject(payload, "type", "offer");
    cJSON_AddStringToObject(payload, key, value);
    auto* json = cJSON_PrintUnformatted(payload);
    auto* root = cJSON_CreateObject();
    auto* msg = cJSON_AddObjectToObject(root, "peer_msg");
    if(from)cJSON_AddItemToObject(msg,"from",cJSON_Parse(from));
    cJSON_AddStringToObject(msg, "msg", json);
    auto* wire = cJSON_PrintUnformatted(root);
    incoming = wire;
    cJSON_free(wire);
    cJSON_free(json);
    cJSON_Delete(root);
    cJSON_Delete(payload);
}

void clean() {
    assert(peers == 0);
    assert(sockets == 0);
    assert(runtimes == 0);
    assert(!mediaRunning);
    assert(audioHandles==0);
}
opennow::wire::Bytes command(std::uint16_t code,const opennow::wire::Bytes& payload){
    opennow::wire::Bytes bytes;
    opennow::wire::le(bytes,code,2);opennow::wire::le(bytes,payload.size(),2);
    bytes.insert(bytes.end(),payload.begin(),payload.end());return bytes;
}
void receive(const opennow::wire::Bytes& bytes,std::uint16_t sid=6){
    dataCallback(reinterpret_cast<char*>(const_cast<std::uint8_t*>(bytes.data())),bytes.size(),dataContext,sid);
}
}

struct PeerConnection {
    PeerConfiguration config;
    PeerConnectionState state = PEER_CONNECTION_NEW;
    void (*onState)(PeerConnectionState, void*) = nullptr;
};

WebSocketClient::WebSocketClient(const std::string& url): url_(url) { ++sockets; websocket = this; }
WebSocketClient::~WebSocketClient() { assert(!inPoll); --sockets; websocket = nullptr; }
bool WebSocketClient::connect() {
    connectedUrl = url_;
    assert(std::find(custom_headers_.begin(), custom_headers_.end(),
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/131.0.0.0 Safari/537.36") != custom_headers_.end());
    connected_ = socketStarts; last_error_ = "mock connect failure"; return connected_;
}
void WebSocketClient::disconnect() { connected_ = false; }
void WebSocketClient::send_message(const std::string& message) { outbound.push_back(message); }
void WebSocketClient::poll() {
    inPoll = true;
    if (dropSocket) { connected_ = false; last_error_ = "mock socket loss"; dropSocket = false; }
    if (!incoming.empty()) { auto message = std::move(incoming); incoming.clear(); on_message_(message); }
    inPoll = false;
}

namespace opennow {
bool Media::start(const StreamSettings& settings,unsigned channels) noexcept { mediaSettings=settings;frames = 0;presented=0; mediaRunning = mediaStarts&&audioOutput_.open(channels); return mediaRunning; }
bool Media::configureAudio(const audio::Format& format) noexcept {configuredAudio=format;return audioConfigureSucceeds&&audio::validFormat(format);}
void Media::stop() noexcept { audioOutput_.close();mediaRunning = false; }
void Media::requireKeyframe() noexcept {++referenceResets;}
bool Media::video(const std::uint8_t*, std::size_t) noexcept {++mediaVideoCalls;return mediaVideoAccepts;}
void Media::audio(const std::uint8_t*, std::size_t, std::uint16_t, std::uint8_t, std::uint32_t) noexcept {}
bool randomBytes(void* output, std::size_t size) noexcept { std::memset(output, 0, size); return entropyWorks; }
void secureErase(void* data, std::size_t size) noexcept { std::memset(data, 0, size); }
}

extern "C" {
unsigned long long sceKernelGetProcessTime() { return processTime; }
int sceAudioOutInit(){return 0;}
int sceAudioOutOpen(int,int,int,unsigned,unsigned,unsigned format){if(format==2&&!allowEightChannelOutput)return -1;++audioHandles;return 10;}
int sceAudioOutClose(int){--audioHandles;return 0;}
int sceAudioOutOutput(int,const void*){return 0;}
void opennow_media_note(const char* note) { diagnostics.emplace_back(note); }
int opennow_peer_random(unsigned char*, std::size_t);
int peer_init() { if (!runtimeResult) ++runtimes; return runtimeResult; }
void peer_deinit() { --runtimes; }
void peer_connection_set_diagnostics_enabled(int) {}
int peer_connection_set_audio_payload_types(PeerConnection*,int primary,int red){assert(!remoteSdp.empty());if(!audioPayloadsSucceed)return -1;selectedAudioPayload=primary;selectedRedPayload=red;return 0;}
PeerConnection* peer_connection_create(PeerConfiguration* config) {
    if (!peerStarts) return nullptr;
    candidates.clear();
    remoteSdp.clear();
    channelRequests.clear();videoCallback=config->onvideopacket;videoContext=config->user_data;
    unsigned char byte;
    opennow_peer_random(&byte, 1);
    ++peers;
    return new PeerConnection{*config};
}
void peer_connection_close(PeerConnection*) {}
void peer_connection_destroy(PeerConnection* pc) { assert(!inPoll && !inLoop); --peers; delete pc; }
int peer_connection_get_video_rtp_stats(PeerConnection*, PeerVideoRtpStats* stats) { *stats = rtpStats; stats->assembler_ready = assemblerReady; return 0; }
void peer_connection_onicecandidate(PeerConnection* pc, void (*callback)(char*, void*)) { localIce = callback; localIceContext = pc->config.user_data; }
void peer_connection_oniceconnectionstatechange(PeerConnection* pc, void (*fn)(PeerConnectionState, void*)) { pc->onState = fn; }
void peer_connection_ondatachannel(PeerConnection* pc, void (*message)(char*, size_t, void*, uint16_t), void (*open)(void*), void (*close)(void*)) { dataCallback = message; dataOpened = open; dataClosed = close; dataContext = pc->config.user_data; }
void peer_connection_set_remote_description(PeerConnection*, const char* sdp, SdpType) { remoteSdp = sdp; }
const char* peer_connection_create_answer(PeerConnection*) {
    if (nullAnswer) return nullptr;
    char candidate[] = "a=candidate:local 1 udp 1234 192.168.1.2 50000 typ host\r\n";
    localIce(candidate, localIceContext);
    if(!secureAnswer)return "v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\na=mid:video\r\na=rtpmap:96 H264/90000\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=mid:audio\r\na=rtpmap:111 opus/48000/2\r\n";
    return answer;
}
int peer_connection_add_ice_candidate(PeerConnection*, char* candidate) {assert(!remoteSdp.empty());if(candidateFails)return -1;candidates.emplace_back(candidate); return 0; }
int peer_connection_get_ice_candidate_pair_stats(PeerConnection*, int* total, int*, int*, int*, int*) {
    *total = static_cast<int>(candidates.size()) + (remoteSdp.find("a=candidate:") != std::string::npos);
    return 0;
}
const char* peer_connection_state_to_string(PeerConnectionState state) { return state == PEER_CONNECTION_FAILED ? "failed" : "closed"; }
PeerConnectionState peer_connection_get_state(PeerConnection* pc) { return pc->state; }
int peer_connection_loop(PeerConnection* pc) {
    inLoop = true;
    if (nextState != PEER_CONNECTION_NEW) {
        pc->state = nextState;
        nextState = PEER_CONNECTION_NEW;
        pc->onState(pc->state, pc->config.user_data);
    }
    inLoop = false;
    return 0;
}
int peer_connection_request_video_keyframe(PeerConnection*) { ++keyframeRequests;return 0; }
int peer_connection_create_datachannel_sid(PeerConnection*, DecpChannelType type, uint16_t, uint32_t reliability, char* label, char*, uint16_t sid) {if(channelCreateFails)return -1;channelRequests.push_back({sid,type,reliability,label});return 0; }
int peer_connection_datachannel_is_open(PeerConnection*, uint16_t sid) { return (sid==6&&controlOpen)||(sid==0&&inputOpen)||(sid==4&&partialInputOpen); }
int peer_connection_datachannel_send_binary_sid(PeerConnection*, char* data, size_t size, uint16_t sid) {
    if(sid==0||sid==4){
        if((sid==0&&!inputOpen)||(sid==4&&!partialInputOpen))return -1;
        if(inputSendBudget==0)return -1;
        if(inputSendBudget>0)--inputSendBudget;
        sent.emplace_back(data,data+size);
        sentSids.push_back(sid);
    }else if(sid==6){
        if(controlSendFails)return -1;
        controlSent.emplace_back(data,data+size);
    }
    return 0;
}
}

int main() {
    opennow::Media media;
    opennow::Stream stream(media);
    auto launch = session();
    {
        const std::string offered="m=video 9 UDP/TLS/RTP/SAVPF 96\r\na=rtpmap:96 H264/90000\r\n"
            "m=audio 9 UDP/TLS/RTP/SAVPF 107 113 114\r\na=rtpmap:107 opus/48000/2\r\n"
            "a=rtpmap:113 multiopus/48000/8\r\na=fmtp:113 num_streams=5;coupled_streams=3;channel_mapping=0,6,1,2,3,4,5,7\r\n"
            "a=rtpmap:114 red/48000/8\r\na=fmtp:114 113/113\r\n";
        launch.settings.audio_mode=opennow::audio::Mode::automatic;launch.audioChannels=8;
        assert(stream.start(launch,"test"));outbound.clear();queuePayload("sdp",offered.c_str());stream.tick(1000000);
        assert(!stream.failed()&&configuredAudio.channels==8&&selectedAudioPayload==113&&selectedRedPayload==114);
        assert(std::any_of(outbound.begin(),outbound.end(),[](const auto& text){return text.find("multiopus/48000/8")!=std::string::npos;}));
        stream.stop();clean();
        allowEightChannelOutput=false;
        assert(stream.start(launch,"test"));queuePayload("sdp",offered.c_str());stream.tick(1000000);
        assert(!stream.failed()&&configuredAudio.channels==2&&selectedAudioPayload==107&&selectedRedPayload==-1);
        stream.stop();clean();allowEightChannelOutput=true;
        for(bool* fail:{&audioPayloadsSucceed,&audioConfigureSucceeds}){
            *fail=false;assert(stream.start(launch,"test"));outbound.clear();queuePayload("sdp",offered.c_str());stream.tick(1000000);
            assert(stream.failed()&&std::strstr(stream.status(),"configure offered audio"));clean();*fail=true;
        }
        {
            const auto invalid=std::string("m=video 9 RTP/AVP 107\r\na=rtpmap:107 H264/90000\r\nm=audio 9 RTP/AVP 107\r\na=rtpmap:107 opus/48000/2\r\n");
            assert(stream.start(launch,"test"));queuePayload("sdp",invalid.c_str());stream.tick(1000000);
            assert(stream.failed()&&std::strstr(stream.status(),"supported audio"));clean();
        }
        {
            const auto microphone=offered+"a=mid:0\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=mid:3\r\na=rtpmap:111 opus/48000/2\r\na=recvonly\r\n";
            launch=session();
            assert(stream.start(launch,"test"));outbound.clear();queuePayload("sdp",microphone.c_str());stream.tick(1000000);
            assert(!stream.failed()&&configuredAudio.channels==2&&selectedAudioPayload==107&&selectedRedPayload==-1);
            assert(std::any_of(outbound.begin(),outbound.end(),[](const auto& text){return text.find("opus/48000/2")!=std::string::npos;}));
            stream.stop();clean();
        }
        launch=session();launch.audioChannels=8;assert(!stream.start(launch,"test"));clean();launch=session();
    }
    struct RouteCase {opennow::SignalingSource source;const char* label;};
    for(const auto& route:{
        RouteCase{opennow::SignalingSource::none,"none"},
        RouteCase{opennow::SignalingSource::explicitUrl,"explicit"},
        RouteCase{opennow::SignalingSource::streamConnection,"stream"},
        RouteCase{opennow::SignalingSource::alternateConnection,"alternate"},
        RouteCase{opennow::SignalingSource::sessionControl,"control"}
    }) {
        launch.signalingSource=route.source;socketStarts=false;diagnostics.clear();
        assert(!stream.start(launch,"test"));
        const auto expected=std::string("Signaling: mock connect failure [route=")+route.label+"]";
        assert(stream.status()==expected);
        assert(diagnostics.back()==expected);
        assert(std::find(diagnostics.begin(),diagnostics.end(),std::string("SIGNALING route=")+route.label)!=diagnostics.end());
        for(const auto& note:diagnostics){assert(note.find(launch.id)==std::string::npos);assert(note.find("signaling.example")==std::string::npos);}
        clean();
    }
    socketStarts=true;
    for(const char* id:{"","null","1.5","2147483648","\"invalid\""}) {
        assert(stream.start(launch,"test"));
        outbound.clear();
        incoming=R"({"peer_info":{"name":"opennow-test","id":7},"ackid":1})";
        stream.tick(1000000);
        assert(std::find(outbound.begin(),outbound.end(),R"({"ack":1})")==outbound.end());
        outbound.clear();
        incoming=std::string(R"({"peer_info":{"name":"opennow-test")")+(*id?std::string(",\"id\":")+id:"")+R"(},"ackid":8})";
        stream.tick(1000002);
        assert(std::find(outbound.begin(),outbound.end(),R"({"ack":8})")!=outbound.end());
        outbound.clear();queuePayload("sdp",answer);stream.tick(1000004);
        bool checked=false;
        for(const auto& value:outbound) {
            auto* root=cJSON_Parse(value.c_str());
            auto* peer=cJSON_GetObjectItemCaseSensitive(root,"peer_msg");
            if(peer){assert(cJSON_GetObjectItemCaseSensitive(peer,"from")->valueint==7);checked=true;}
            cJSON_Delete(root);
        }
        assert(checked);stream.stop();clean();
    }
    for(bool established:{false,true})for(const char* from:{static_cast<const char*>(nullptr),"null","1.5","2147483648","\"invalid\""}) {
        assert(stream.start(launch,"test"));
        if(established){incoming=R"({"peer_msg":{"from":9,"msg":"{}"}})";stream.tick(1000000);}
        outbound.clear();queuePayload("sdp",answer,from);stream.tick(1000002);
        bool checked=false;
        for(const auto& value:outbound) {
            auto* root=cJSON_Parse(value.c_str());auto* peer=cJSON_GetObjectItemCaseSensitive(root,"peer_msg");
            if(peer){assert(cJSON_GetObjectItemCaseSensitive(peer,"to")->valueint==(established?9:1));checked=true;}
            cJSON_Delete(root);
        }
        assert(checked);stream.stop();clean();
    }
    assert(stream.start(launch,"test"));stream.tick(1000000);outbound.clear();
    incoming=R"({"hb":null,"ackid":3})";stream.tick(1000002);
    assert(std::find(outbound.begin(),outbound.end(),R"({"ack":3})")!=outbound.end());
    assert(std::find(outbound.begin(),outbound.end(),R"({"hb":1})")==outbound.end());
    stream.stop();clean();
    launch.settings={1440,1080,47,32000,opennow::VideoMode::h264Hardware,opennow::QualityMode::clarity,opennow::NetworkPolicy::fixed};
    const auto expectedSettings=launch.settings;
    assert(stream.start(launch,"test")&&mediaSettings==expectedSettings);
    launch.settings={};outbound.clear();queuePayload("sdp",answer);stream.tick(1000000);
    assert(std::any_of(outbound.begin(),outbound.end(),[](const auto& text){return text.find("a=video.maxFPS:47")!=std::string::npos&&text.find("a=video.clientViewportWd:1440")!=std::string::npos;}));
    stream.stop();clean();
    launch.settings.fps=240;assert(!stream.start(launch,"test")&&stream.failed());clean();launch.settings={};
    assert(stream.start(launch, "test"));
    assert(!stream.failed());
    dropSocket = true;
    stream.tick(1000000);
    assert(!stream.active());
    assert(stream.failed());
    clean();
    assert(std::strstr(stream.status(), "mock socket loss"));
    stream.tick(2000000);
    assert(stream.failed());

    assert(stream.start(launch, "test"));
    assert(!stream.failed());
    websocket->disconnect();
    assert(stream.active());
    stream.tick(1000000);
    assert(stream.failed());
    clean();

    // Losing signaling after the answer keeps the negotiated media and stops using the socket.
    assert(stream.start(launch, "test"));
    queuePayload("sdp", answer);
    stream.tick(1000000);
    assert(!stream.failed()&&std::any_of(outbound.begin(),outbound.end(),[](const auto& text){return text.find("answer")!=std::string::npos;}));
    diagnostics.clear();
    dropSocket = true;
    stream.tick(1500000);
    assert(stream.active()&&!stream.failed()&&sockets==0&&peers==1);
    assert(std::any_of(diagnostics.begin(),diagnostics.end(),[](const auto& note){return note.find("SIGNALING lost after answer")!=std::string::npos&&note.find("mock socket loss")!=std::string::npos;}));
    stream.tick(4000000);
    assert(stream.active()&&!stream.failed());
    stream.stop();
    clean();

    for (auto state : {PEER_CONNECTION_FAILED, PEER_CONNECTION_CLOSED,PEER_CONNECTION_DISCONNECTED}) {
        assert(stream.start(launch, "test"));
        media.frames = 1;
        nextState = state;
        stream.tick(1000000);
        assert(stream.failed());
        clean();
    }

    assert(stream.start(launch, "test"));
    stream.tick(1000000);
    stream.tick(46000001);
    assert(std::strstr(stream.status(), "Video timeout"));
    assert(stream.failed());
    clean();

    launch.settings = opennow::settingsFor(opennow::StreamProfile::native_hdr60);
    assert(stream.start(launch, "test"));
    queuePayload("sdp", answer);
    stream.tick(1000000);
    assert(std::strstr(stream.status(), "selected video codec"));
    assert(stream.failed());
    clean();
    launch.settings = opennow::settingsFor(opennow::StreamProfile::quality);

    for (bool* option : {&mediaStarts, &peerStarts, &socketStarts, &entropyWorks, &assemblerReady}) {
        *option = false;
        assert(!stream.start(launch, "test"));
        assert(stream.failed());
        clean();
        *option = true;
    }
    runtimeResult = -1;
    assert(!stream.start(launch, "test"));
    assert(stream.failed());
    clean();
    runtimeResult = 0;

    launch.signaling[0] = '\0';
    assert(!stream.start(launch, "test"));
    assert(stream.failed());
    clean();
    launch = session();

    assert(stream.start(launch, "test"));
    entropyWorks = false;
    unsigned char byte;
    opennow_peer_random(&byte, 1);
    stream.tick(1000000);
    assert(stream.failed());
    clean();
    entropyWorks = true;

    for (const char* message : {"{\"error\":\"peerRemoved\"}", "{\"peer_msg\":{\"from\":1,\"msg\":\"BYE\"}}"}) {
        assert(stream.start(launch, "test"));
        incoming = message;
        stream.tick(1000000);
        assert(stream.failed());
        clean();
    }

    assert(stream.start(launch, "test"));
    nullAnswer = true;
    queuePayload("sdp", answer);
    stream.tick(1000000);
    assert(stream.failed());
    clean();
    nullAnswer = false;

    std::strcpy(launch.mediaIp, "198.51.100.55");
    launch.mediaPort = 443;
    assert(stream.start(launch, "test"));
    queuePayload("sdp", (std::string(answer) + "a=candidate:1 1 udp 2122260223 203.0.113.10 47998 typ host\r\n").c_str());
    stream.tick(1000000);
    assert(remoteSdp.find("203.0.113.10 47998 typ host") != std::string::npos);
    assert(candidates.empty());
    queuePayload("candidate", "candidate:1 1 udp 2122260223 203.0.113.10 47998 typ host");
    stream.tick(1000001);
    assert(candidates.back() == "a=candidate:1 1 udp 2122260223 203.0.113.10 47998 typ host");
    queuePayload("candidate", "candidate:1 1 udp 2122260223 10.0.0.5 47998 typ host");
    stream.tick(1000002);
    assert(candidates.back() == "a=candidate:1 1 udp 2122260223 198.51.100.55 443 typ host");
    stream.stop();
    assert(!stream.failed());
    clean();

    for (bool audioBundle : {false, true}) {
        std::strcpy(launch.signaling, "wss://signaling.example/nvst/sign_in?discard=1");
        assert(stream.start(launch, "test"));
        assert(connectedUrl == "wss://signaling.example/nvst/sign_in?peer_id=opennow-test&version=2&peer_role=1&pairing_id=session-test");
        outbound.clear();
        auto offer = std::string(answer);
        if (audioBundle) offer.replace(offer.find("BUNDLE video audio"), 18, "BUNDLE audio video");
        queuePayload("sdp", offer.c_str());
        stream.tick(1000000);
        char lateCandidate[] = "a=candidate:late 1 udp 1234 192.168.1.2 50001 typ host\r\n";
        localIce(lateCandidate, localIceContext);
        stream.tick(1000001);
        int sentCandidates = 0;
        for (const auto& wire : outbound) {
            auto* root = cJSON_Parse(wire.c_str());
            auto* peer = cJSON_GetObjectItemCaseSensitive(root, "peer_msg");
            auto* message = cJSON_GetObjectItemCaseSensitive(peer, "msg");
            auto* data = cJSON_IsString(message) ? cJSON_Parse(message->valuestring) : nullptr;
            if (cJSON_GetObjectItemCaseSensitive(data, "candidate")) {
                auto* mid = cJSON_GetObjectItemCaseSensitive(data, "sdpMid");
                auto* index = cJSON_GetObjectItemCaseSensitive(data, "sdpMLineIndex");
                assert(cJSON_IsString(mid) && !std::strcmp(mid->valuestring, audioBundle ? "audio" : "video"));
                assert(cJSON_IsNumber(index) && index->valueint == (audioBundle ? 1 : 0));
                ++sentCandidates;
            }
            cJSON_Delete(data);
            cJSON_Delete(root);
        }
        assert(sentCandidates == 2);
        stream.stop();
        clean();
    }

    assert(stream.start(launch, "test"));
    queuePayload("sdp", answer);
    stream.tick(1000000);
    assert(candidates.size() == 1);
    assert(candidates.front() == "a=candidate:100 1 UDP 2130706431 198.51.100.55 443 typ host");
    stream.stop();
    clean();

    using opennow::wire::Bytes;
    namespace wire = opennow::wire;
    for(unsigned protocol:{2U,3U}) {
        assert(stream.start(launch,"test"));
        char handshake[]={0x0e,0x02,static_cast<char>(protocol),0};
        dataCallback(handshake,sizeof(handshake),dataContext,0);
        sent.clear();
        PS5_PadData state{};state.connected=true;state.leftStick={128,128};state.rightStick={128,128};
        const unsigned offset=protocol==3?12:0;
        stream.input(state,100000);assert(sent.size()==1);
        state.leftStick.x=200;
        stream.input(state,102000);assert(sent.size()==1);
        state.leftStick.x=128;
        stream.input(state,104000);assert(sent.size()==1);
        assert(sent.back()[offset+16]==0&&sent.back()[offset+17]==0);
        state.leftStick.x=200;state.rightStick.x=200;
        stream.input(state,120000);assert(sent.size()==2);
        state.leftStick.x=128;
        stream.input(state,120002);assert(sent.size()==3);
        assert(sent.back()[offset+16]==0&&sent.back()[offset+17]==0&&sent.back()[offset+21]!=0);
        state.rightStick.x=201;
        stream.input(state,120004);assert(sent.size()==3);
        state.rightStick.x=128;
        stream.input(state,120006);assert(sent.size()==4);
        state.buttons=PS5_PAD_BUTTON_UP;
        stream.input(state,120008);assert(sent.size()==5&&sent.back()[offset+12]==1);
        state.buttons=0;
        stream.input(state,120010);assert(sent.size()==6&&sent.back()[offset+12]==0);
        state.analogButtons.l2=128;
        stream.input(state,120012);assert(sent.size()==7&&sent.back()[offset+14]==128);
        state.analogButtons.l2=0;
        stream.input(state,120014);assert(sent.size()==8&&sent.back()[offset+14]==0);
        state.connected=false;
        stream.input(state,120016);assert(sent.size()==9);
        state.connected=true;
        stream.input(state,120018);assert(sent.size()==10);
        stream.input(state,120020);assert(sent.size()==10);
        stream.input(state,136018);assert(sent.size()==11);
        state.leftStick.x=135;
        stream.input(state,152018);assert(sent.size()==12);
        state.leftStick.x=134;
        stream.input(state,152020);assert(sent.size()==13&&sent.back()[offset+16]==0&&sent.back()[offset+17]==0);
        state.leftStick.x=128;
        stream.input(state,152022);assert(sent.size()==13);
        stream.stop();clean();sent.clear();
    }
    opennow::InputQueue queue;
    assert(stream.start(launch, "test"));
    queue.key({0x41, 0x1e, 0});
    queue.move(500, 0);
    stream.events(queue, 4000000);
    assert(sent.empty() && queue.size() == 0);
    char v3[] = {0x0e, 0x02, 0x03, 0x00};
    dataCallback(v3, sizeof(v3), dataContext, 0);
    sent.clear();
    PS5_PadData pad{};
    pad.connected = true;
    pad.buttons = PS5_PAD_BUTTON_CROSS | PS5_PAD_BUTTON_TOUCH_PAD;
    pad.leftStick = {128, 128};
    pad.rightStick = {128, 128};
    stream.input(pad, 5000000);
    assert(sent.size() == 1);
    assert((sent[0] == Bytes{0x23, 0, 0, 0, 0, 0, 0x4c, 0x4b, 0x40, 0x21, 0x00, 0x26,
        0x0c, 0, 0, 0, 0x1a, 0, 0, 0, 0x01, 1, 0x14, 0, 0x20, 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x55, 0, 0, 0,
        0x40, 0x4b, 0x4c, 0, 0, 0, 0, 0}));
    sent.clear();
    pad.buttons = PS5_PAD_BUTTON_TOUCH_PAD;
    stream.input(pad, 5016000);
    assert(sent.size() == 1 && sent[0].size() == 50 && sent[0][24] == 0x20 && sent[0][25] == 0);
    sent.clear();
    pad.buttons = 0;
    stream.input(pad, 5032000);
    assert(sent.size() == 1 && sent[0][24] == 0 && sent[0][25] == 0);

    sent.clear();
    queue.key({0x41, 0x1e, opennow::modifierShift});
    queue.move(70000, 0);
    stream.events(queue, 6000000);
    assert(sent.size() == 5);
    assert(sent[0] == wire::key(true, 0xa0, 0x2a, 1, 3, 6000000));
    assert(sent[1] == wire::key(true, 0x41, 0x1e, 1, 3, 6000000));
    assert(sent[2] == wire::mouseMove(32767, 0, 3, 6000000) && sent[3] == sent[2]);
    assert(sent[4] == wire::mouseMove(1, 0, 3, 6000000));
    sent.clear();
    queue.key({0x42, 0x30, opennow::modifierCtrl | opennow::modifierAlt});
    stream.events(queue, 6031999);
    assert(sent.empty() && queue.size() == 1);
    stream.events(queue, 6032000);
    assert(sent.size() == 2);
    assert(sent[0] == wire::key(false, 0x41, 0x1e, 1, 3, 6032000));
    assert(sent[1] == wire::key(false, 0xa0, 0x2a, 0, 3, 6032000));
    sent.clear();
    stream.events(queue, 6063999);
    assert(sent.empty());
    stream.events(queue, 6064000);
    assert(sent.size() == 3);
    assert(sent[0] == wire::key(true, 0x11, 0x1d, 2, 3, 6064000));
    assert(sent[1] == wire::key(true, 0x12, 0x38, 6, 3, 6064000));
    assert(sent[2] == wire::key(true, 0x42, 0x30, 6, 3, 6064000));
    sent.clear();
    assert(queue.button(3, true) && queue.button(3, true));
    stream.events(queue, 6070000);
    assert(sent.size() == 1 && sent[0] == wire::mouseButton(true, 3, 3, 6070000));
    sent.clear();
    stream.stop();
    assert(sent.size() == 5);
    assert(sent[0] == wire::key(false, 0x42, 0x30, 6, 3, 5052000));
    assert(sent[1] == wire::key(false, 0x12, 0x38, 2, 3, 5052000));
    assert(sent[2] == wire::key(false, 0x11, 0x1d, 0, 3, 5052000));
    assert(sent[3] == wire::mouseButton(false, 3, 3, 5052000));
    assert(sent[4].size() == 50 && sent[4][9] == 0x21 && sent[4][24] == 0 && sent[4][25] == 0);
    clean();

    assert(stream.start(launch, "test"));
    char v2[] = {0x0e, 0x02, 0x02, 0x00};
    dataCallback(v2, sizeof(v2), dataContext, 0);
    sent.clear();
    queue.clear();
    assert(queue.button(1, true));
    queue.key({0x0d, 0x1c, 0});
    stream.events(queue, 7000000);
    assert(sent.size() == 2);
    assert(sent[0] == wire::mouseButton(true, 1, 2, 7000000));
    assert(sent[1] == wire::key(true, 0x0d, 0x1c, 0, 2, 7000000));
    sent.clear();
    stream.stop();
    assert(sent.size() == 3);
    assert(sent[0] == wire::key(false, 0x0d, 0x1c, 0, 2, 20000));
    assert(sent[1] == wire::mouseButton(false, 1, 2, 20000));
    assert(sent[2].size() == 38 && sent[2][0] == 0x0c && sent[2][12] == 0 && sent[2][13] == 0);
    clean();

    assert(stream.start(launch, "test"));
    queue.clear();
    queue.key({0x51, 0x10, 0});
    queue.cancel();
    stream.events(queue, 8000000);
    assert(!stream.inputReady() && queue.size() == 0);
    dataCallback(v3, sizeof(v3), dataContext, 0);
    assert(stream.inputReady());
    sent.clear();
    stream.events(queue, 8000001);
    stream.events(queue, 9000000);
    assert(sent.empty());
    assert(queue.button(200, true) && queue.button(0, true) && queue.button(1, true));
    stream.events(queue, 9000000);
    assert(sent.size() == 1 && sent[0] == wire::mouseButton(true, 1, 3, 9000000));
    sent.clear();
    queue.key({0x41, 0x1e, opennow::modifierShift});
    stream.events(queue, 9100000);
    assert(sent.size() == 2);
    sent.clear();
    queue.key({0x42, 0x30, 0});
    queue.move(10, 0);
    queue.cancel();
    queue.key({0x43, 0x2e, 0});
    stream.events(queue, 9110000);
    assert(sent.size() == 3);
    assert(sent[0] == wire::key(false, 0x41, 0x1e, 1, 3, 9110000));
    assert(sent[1] == wire::key(false, 0xa0, 0x2a, 0, 3, 9110000));
    assert(sent[2] == wire::mouseButton(false, 1, 3, 9110000));
    sent.clear();
    stream.events(queue, 9141999);
    assert(sent.empty() && queue.size() == 1);
    stream.events(queue, 9142000);
    assert(sent.size() == 1 && sent[0] == wire::key(true, 0x43, 0x2e, 0, 3, 9142000));
    sent.clear();
    queue.key({0x44, 0x20, 0});
    queue.cancel();
    stream.events(queue, 9150000);
    assert(sent.size() == 1 && sent[0] == wire::key(false, 0x43, 0x2e, 0, 3, 9150000));
    sent.clear();
    for (std::uint64_t at = 9150000; at < 9400000; at += 16000) stream.events(queue, at);
    assert(sent.empty() && queue.size() == 0);
    queue.cancel();
    stream.events(queue, 9400000);
    assert(sent.empty());
    stream.stop();
    clean();

    assert(stream.start(launch, "test"));
    media.frames = 1;
    queue.clear();
    dataOpened(dataContext);
    dataCallback(v3, sizeof(v3), dataContext, 0);
    assert(stream.inputReady());
    assert(queue.button(1, true));
    queue.key({0x41, 0x1e, opennow::modifierShift});
    stream.events(queue, 2000000);
    sent.clear();
    stream.events(queue, 2010000);
    assert(sent.size() == 0);
    dataClosed(dataContext);
    assert(!stream.inputReady());
    assert(queue.button(1, false));
    queue.key({0x42, 0x30, 0});
    stream.events(queue, 2020000);
    assert(sent.empty() && queue.size() == 0);
    stream.tick(3000000);
    assert(!stream.inputReady()&&stream.failed()&&!stream.active());
    stream.events(queue, 3000001);
    assert(sent.empty());
    dataOpened(dataContext);
    stream.tick(4000000);
    assert(!stream.inputReady());
    clean();
    assert(stream.start(launch,"test"));
    dataCallback(v3,sizeof(v3),dataContext,0);
    assert(stream.inputReady());
    sent.clear();
    queue.move(5, 0);
    stream.events(queue, 4000001);
    assert(sent.size() == 1);
    assert(sent[0] == wire::mouseMove(5, 0, 3, 4000001));
    sent.clear();
    stream.stop();
    assert(sent.size() == 1 && sent[0].size() == 50);
    clean();
    media.frames = 0;
    for(int protocol:{2,3}){
        assert(stream.start(session(),"test"));
        char handshake[]={14,2,static_cast<char>(protocol),0};
        dataCallback(handshake,sizeof(handshake),dataContext,0);
        assert(stream.inputReady());
        opennow::InputQueue retryQueue;
        sent.clear();
        inputSendBudget=1;
        assert(retryQueue.key({0x41,0x1e,opennow::modifierShift|opennow::modifierCtrl}));
        stream.events(retryQueue,5000000);
        assert(sent.size()==1&&sent[0]==wire::key(true,0xa0,0x2a,1,protocol,5000000));
        sent.clear();inputSendBudget=-1;
        stream.events(retryQueue,5000100);
        assert(sent.size()==2);
        assert(sent[0]==wire::key(true,0x11,0x1d,3,protocol,5000100));
        assert(sent[1]==wire::key(true,0x41,0x1e,3,protocol,5000100));
        sent.clear();inputSendBudget=0;
        stream.events(retryQueue,5032100);
        assert(sent.empty());
        inputSendBudget=1;
        stream.events(retryQueue,5032200);
        assert(sent.size()==1&&sent[0]==wire::key(false,0x41,0x1e,3,protocol,5032200));
        sent.clear();inputSendBudget=-1;
        stream.events(retryQueue,5032300);
        assert(sent.size()==2);
        assert(sent[0]==wire::key(false,0x11,0x1d,1,protocol,5032300));
        assert(sent[1]==wire::key(false,0xa0,0x2a,0,protocol,5032300));
        sent.clear();inputSendBudget=1;
        retryQueue.move(65535,-65535);
        stream.events(retryQueue,5100000);
        assert(sent.size()==1&&sent[0]==wire::mouseMove(32767,-32768,protocol,5100000));
        sent.clear();inputSendBudget=-1;
        stream.events(retryQueue,5100001);
        assert(sent.size()==2);
        assert(sent[0]==wire::mouseMove(32767,-32767,protocol,5100001));
        assert(sent[1]==wire::mouseMove(1,0,protocol,5100001));
        sent.clear();inputSendBudget=0;
        assert(retryQueue.wheel(-120,120));
        stream.events(retryQueue,5200000);
        assert(sent.empty());
        inputSendBudget=-1;stream.events(retryQueue,5200001);
        assert(sent.size()==1&&sent[0]==wire::mouseWheel(-120,120,protocol,5200001));
        sent.clear();inputSendBudget=0;
        assert(retryQueue.button(1,true));stream.events(retryQueue,5300000);
        assert(sent.empty());
        inputSendBudget=-1;stream.events(retryQueue,5300001);
        assert(sent.size()==1&&sent[0]==wire::mouseButton(true,1,protocol,5300001));
        sent.clear();inputSendBudget=1;
        assert(retryQueue.key({0x42,0x30,opennow::modifierShift}));
        stream.events(retryQueue,5400000);
        assert(sent.size()==1&&sent[0]==wire::key(true,0xa0,0x2a,1,protocol,5400000));
        retryQueue.cancel();sent.clear();inputSendBudget=1;
        stream.events(retryQueue,5400001);
        assert(sent.size()==1&&sent[0]==wire::key(false,0xa0,0x2a,0,protocol,5400001));
        sent.clear();inputSendBudget=-1;
        stream.events(retryQueue,5400002);
        assert(sent.size()==1&&sent[0]==wire::mouseButton(false,1,protocol,5400002));
        sent.clear();stream.events(retryQueue,5500000);
        assert(sent.empty());
        const wire::Bytes cursor{8,3,1,0,1};
        controlSent.clear();controlOpen=false;
        stream.tick(5999999);assert(controlSent.empty());
        controlOpen=true;controlSendFails=true;
        stream.tick(6000000);assert(controlSent.empty());
        controlSendFails=false;stream.tick(6000001);
        assert(std::count(controlSent.begin(),controlSent.end(),cursor)==1);
        stream.tick(6000002);
        assert(std::count(controlSent.begin(),controlSent.end(),cursor)==1);
        stream.tick(7000001);
        assert(std::count(controlSent.begin(),controlSent.end(),cursor)==2);
        stream.stop();clean();controlOpen=false;
    }

    for(const auto& invalid:std::vector<std::string>{"{","null","{} trailing",std::string("{}\0{}",5),R"({"error":"rejected secret server text"})",R"({"peer_msg":{"msg":"{"}})",std::string(65537,'x')}){
        assert(stream.start(session(),"test"));incoming=invalid;stream.tick(1000000);
        assert(stream.failed()&&!stream.active());
        assert(!std::strstr(stream.status(),"secret server text"));clean();
    }
    for(const char* bad:{"wss://","https://signaling.example/nvst/","wss://user:password@signaling.example/nvst/","wss://signaling.example/nvst/#fragment","wss://signaling.example/\r\n"}){
        auto invalid=session();std::strcpy(invalid.signaling,bad);
        assert(!stream.start(invalid,"test")&&stream.failed());clean();
    }
    {
        auto encoded=session();std::strcpy(encoded.id,"seat&other=1 /%");
        assert(stream.start(encoded,"device&name=1"));
        assert(connectedUrl=="wss://signaling.example/nvst/sign_in?peer_id=opennow-device%26name%3D1&version=2&peer_role=1&pairing_id=seat%26other%3D1%20%2F%25");
        stream.stop();clean();
    }
    {
        outbound.clear();assert(stream.start(session(),"test"));
        auto* root=cJSON_Parse(outbound.front().c_str());auto* info=cJSON_GetObjectItemCaseSensitive(root,"peer_info");
        assert(cJSON_GetArraySize(info)==8);
        assert(!std::strcmp(cJSON_GetObjectItemCaseSensitive(info,"browser")->valuestring,"Chrome"));
        assert(!std::strcmp(cJSON_GetObjectItemCaseSensitive(info,"browserVersion")->valuestring,"131"));
        assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(info,"connected")));
        assert(cJSON_GetObjectItemCaseSensitive(info,"peerRole")->valueint==0);
        assert(cJSON_GetObjectItemCaseSensitive(info,"version")->valueint==2);
        assert(!std::strcmp(cJSON_GetObjectItemCaseSensitive(info,"resolution")->valuestring,"1920x1080"));
        cJSON_Delete(root);
        stream.tick(1000000);outbound.clear();incoming=R"({"hb":0})";stream.tick(1000001);
        assert(outbound.empty());incoming=R"({"hb":1})";stream.tick(1000002);
        assert(outbound==std::vector<std::string>{R"({"hb":1})"});
        stream.stop();clean();
    }
    {
        assert(stream.start(session(),"test"));secureAnswer=false;
        queuePayload("sdp",answer);stream.tick(1000000);
        assert(stream.failed()&&std::strstr(stream.status(),"credentials"));clean();secureAnswer=true;
        assert(stream.start(session(),"test"));queuePayload("sdp",(std::string(answer)+"a=ri.partialReliableThresholdMs:0\r\n").c_str());stream.tick(1000000);
        assert(stream.failed()&&std::strstr(stream.status(),"capabilities"));clean();
        assert(stream.start(session(),"test"));queuePayload("sdp",answer);stream.tick(1000000);
        assert(!stream.failed());queuePayload("sdp",answer);stream.tick(1000001);
        assert(stream.failed()&&std::strstr(stream.status(),"renegotiation"));clean();
    }
    {
        constexpr const char* candidate="candidate:early 1 udp 1234 198.51.100.44 50000 typ host";
        assert(stream.start(session(),"test"));queuePayload("candidate",candidate);stream.tick(1000000);
        assert(candidates.empty()&&!stream.failed());queuePayload("sdp",answer);stream.tick(1000001);
        assert(candidates==std::vector<std::string>{std::string("a=")+candidate});stream.stop();clean();
        assert(stream.start(session(),"test"));
        for(unsigned i=0;i<65;++i){queuePayload("candidate",candidate);stream.tick(1000000+i);assert(stream.failed()==(i==64));}
        assert(std::strstr(stream.status(),"Too many queued"));clean();
        assert(stream.start(session(),"test"));queuePayload("candidate",std::string(4097,'x').c_str());stream.tick(1000000);
        assert(stream.failed()&&std::strstr(stream.status(),"ICE candidate"));clean();
        assert(stream.start(session(),"test"));queuePayload("candidate",candidate);stream.tick(1000000);
        candidateFails=true;queuePayload("sdp",answer);stream.tick(1000001);
        assert(stream.failed()&&std::strstr(stream.status(),"Could not add remote"));clean();candidateFails=false;
    }
    {
        assert(stream.start(session(),"test"));inputOpen=partialInputOpen=false;dataOpened(dataContext);
        assert(channelRequests.size()==4);
        assert(channelRequests[0].sid==0&&channelRequests[0].type==DATA_CHANNEL_RELIABLE&&channelRequests[0].label=="input_channel_v1");
        assert(channelRequests[1].sid==2&&channelRequests[1].type==DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT_UNORDERED&&channelRequests[1].reliability==0&&channelRequests[1].label=="stats_channel");
        assert(channelRequests[2].sid==4&&channelRequests[2].type==DATA_CHANNEL_PARTIAL_RELIABLE_TIMED_UNORDERED&&channelRequests[2].reliability==300&&channelRequests[2].label=="input_channel_partially_reliable");
        assert(channelRequests[3].sid==6&&channelRequests[3].label=="control_channel_partially_reliable"&&channelRequests[3].reliability==300);
        dataOpened(dataContext);assert(channelRequests.size()==4);
        char zeroVersion[]={14,2,0,0};dataCallback(zeroVersion,sizeof(zeroVersion),dataContext,0);
        inputOpen=partialInputOpen=true;stream.tick(3000000);assert(!stream.inputReady());
        partialInputOpen=false;processTime=4000000;
        dataCallback(v3,sizeof(v3),dataContext,0);assert(!stream.inputReady());
        partialInputOpen=true;stream.tick(4000000);assert(stream.inputReady());
        dataCallback(v2,sizeof(v2),dataContext,0);
        sent.clear();sentSids.clear();PS5_PadData state{};state.connected=true;state.leftStick={128,128};state.rightStick={128,128};
        stream.input(state,4001234);
        assert(sent==std::vector<Bytes>{wire::gamepad(opennow::gamepadState(state,5,5),3,1234)});
        assert(sentSids==std::vector<std::uint16_t>{4});
        stream.stop();clean();processTime=0;
        assert(stream.start(session(),"test"));channelCreateFails=true;dataOpened(dataContext);assert(channelRequests.empty());
        channelCreateFails=false;dataOpened(dataContext);assert(channelRequests.size()==4);
        dataCallback(v2,sizeof(v2),dataContext,0);assert(stream.inputReady());
        partialInputOpen=false;stream.tick(1000000);assert(stream.failed());clean();partialInputOpen=true;
    }
    {
        auto tuned=session();tuned.settings.controller_left_deadzone=50;tuned.settings.controller_right_deadzone=0;tuned.settings.mouse_sensitivity=200;
        assert(stream.start(tuned,"test"));dataCallback(v3,sizeof(v3),dataContext,0);
        PS5_PadData state{};state.connected=true;state.leftStick={150,128};state.rightStick={150,128};
        sent.clear();inputSendBudget=0;stream.input(state,100000);assert(sent.empty());
        inputSendBudget=1;stream.input(state,100001);assert(sent.size()==1);
        assert(sent.back()==wire::gamepad(opennow::gamepadState(state,50,0),3,100001));
        assert(sent.back()[28]==0&&sent.back()[29]==0&&sent.back()[33]!=0);
        state.buttons=PS5_PAD_BUTTON_CROSS;inputSendBudget=0;stream.input(state,100002);assert(sent.size()==1);
        inputSendBudget=1;stream.input(state,100003);assert(sent.size()==2&&sent.back()[25]==0x10);
        state.buttons=0;inputSendBudget=0;stream.input(state,100004);assert(sent.size()==2);
        inputSendBudget=1;stream.input(state,100005);assert(sent.size()==3&&sent.back()[25]==0);
        inputSendBudget=-1;opennow::InputQueue tunedQueue;tunedQueue.move(10,-11);sent.clear();sentSids.clear();stream.events(tunedQueue,200000);
        assert(sent==std::vector<Bytes>{wire::mouseMove(10,-11,3,200000,200)}&&sentSids==std::vector<std::uint16_t>{4});
        tunedQueue.button(1,true);stream.events(tunedQueue,200001);assert(sentSids.back()==0);
        stream.stop();clean();
    }
    {
        assert(stream.start(session(),"test"));keyframeRequests=referenceResets=0;
        stream.tick(1000000);media.frames=1;stream.tick(2000000);stream.tick(9999999);
        assert(!stream.failed()&&keyframeRequests==0);stream.tick(10000000);
        assert(!stream.failed()&&keyframeRequests==1&&referenceResets==1);stream.tick(13999999);
        assert(!stream.failed()&&keyframeRequests==1);stream.tick(14000000);
        assert(stream.failed()&&std::strstr(stream.status(),"stopped decoding"));clean();
        assert(stream.start(session(),"test"));keyframeRequests=0;media.frames=1;stream.tick(1000000);stream.tick(9000000);
        assert(keyframeRequests==1);media.frames=2;stream.tick(10000000);stream.tick(17999999);
        assert(!stream.failed()&&keyframeRequests==1);stream.tick(18000000);assert(keyframeRequests==2);
        media.presented=1;stream.tick(19000000);stream.tick(26999999);assert(!stream.failed());stream.stop();clean();
        assert(stream.start(session(),"test"));keyframeRequests=referenceResets=mediaVideoCalls=0;mediaVideoAccepts=false;
        std::uint8_t bytes[]={0,0,0,1,0x41};PeerVideoPacket packet{};packet.data=bytes;packet.size=sizeof(bytes);
        rtpStats.access_units_dropped=1;processTime=1000000;videoCallback(&packet,videoContext);
        assert(referenceResets==1&&keyframeRequests==1&&mediaVideoCalls==1);
        processTime=1100000;videoCallback(&packet,videoContext);assert(referenceResets==1&&keyframeRequests==1);
        processTime=1250000;videoCallback(&packet,videoContext);assert(keyframeRequests==2);
        stream.stop();clean();videoCallback(&packet,videoContext);assert(mediaVideoCalls==3);
        mediaVideoAccepts=true;rtpStats={};processTime=0;
    }
    {
        const auto warning=command(0x104,{60,0,0,0}),clear=command(0x105,{0,0,0,0});
        assert(stream.start(session(),"test"));processTime=123456;
        receive(warning,4);assert(!stream.sessionTimer().active);
        receive(warning);assert(stream.sessionTimer().active&&stream.sessionTimer().timerType==4&&stream.sessionTimer().secondsLeft==60&&stream.sessionTimer().receivedAt==123456);
        receive({4,1,4,0,7});assert(stream.sessionTimer().secondsLeft==60);
        const auto decoy=command(0x199,{9,1,4,0,0x80,4,0,0x10});receive(decoy);assert(!stream.failed());
        auto large=command(0x199,Bytes(80));large.insert(large.end(),clear.begin(),clear.end());receive(large);
        assert(!stream.sessionTimer().active&&stream.sessionTimer().timerType==5);
        processTime=0;dataCallback(v3,sizeof(v3),dataContext,0);receive(warning);
        stream.tick(1000000);assert(stream.sessionTimer().active);
        opennow::InputQueue warningQueue;warningQueue.button(1,true);inputSendBudget=0;stream.events(warningQueue,1000001);
        assert(stream.sessionTimer().active);inputSendBudget=-1;stream.events(warningQueue,1000002);
        assert(!stream.sessionTimer().active);stream.stop();clean();
        assert(stream.start(session(),"test"));dataCallback(v3,sizeof(v3),dataContext,0);
        PS5_PadData neutral{};neutral.connected=true;neutral.leftStick={128,128};neutral.rightStick={128,128};
        stream.input(neutral,100000);receive(warning);stream.input(neutral,116000);
        assert(stream.sessionTimer().active);
        neutral.leftStick.x=200;inputSendBudget=0;stream.input(neutral,132000);assert(stream.sessionTimer().active);
        inputSendBudget=-1;stream.input(neutral,132001);assert(!stream.sessionTimer().active);
        receive(warning);sentSids.clear();stream.stop();assert(!sentSids.empty()&&sentSids.back()==0);clean();
        opennow::control::Command parsed;
        for(const auto& malformed:std::vector<Bytes>{{4,1},{4,1,4,0,1},Bytes(65540)}){
            opennow::control::Reader reader(malformed.data(),malformed.size());assert(!reader.next(parsed)&&!reader.valid());
        }
        Bytes tooMany(257*4);opennow::control::Reader reader(tooMany.data(),tooMany.size());unsigned count=0;
        while(reader.next(parsed))++count;assert(count==256&&!reader.valid());
        for(std::uint32_t code:{0x80030023U,0x80040010U,0x80049999U}){
            assert(stream.start(session(),"test"));receive(warning);
            Bytes payload;wire::be(payload,code,4);payload.insert(payload.end(),{0x37,0,0,0,0x10,0,4,0x80});receive(command(0x109,payload));
            assert(stream.failed()&&!stream.inputReady()&&!stream.sessionTimer().active);
            const auto termination=stream.serverTermination();assert(termination.present&&termination.wireCode==code);
            if(code==0x80030023U){assert(termination.reason==0x38&&termination.extendedCode==0x80030007&&termination.resumable==false);}
            else if(code==0x80040010U){assert(termination.reason==0x37&&termination.extendedCode==0x80040010&&termination.resumable==true);}
            else assert(!termination.reason&&!termination.extendedCode&&!termination.resumable);
            stream.tick(1000000);clean();assert(stream.serverTermination().wireCode==code);
        }
        assert(stream.start(session(),"test"));assert(!stream.serverTermination().present);stream.stop();clean();
    }
    {
        assert(stream.start(session(),"test"));controlOpen=true;controlSent.clear();
        stream.tick(1000000);
        const auto first=opennow::webrtc::qosPacket({1,0});
        assert(controlSent==std::vector<Bytes>{Bytes(first.begin(),first.end())});
        stream.tick(1049999);assert(controlSent.size()==1);
        controlSendFails=true;stream.tick(1050000);assert(controlSent.size()==1);
        controlSendFails=false;stream.tick(1100000);
        const auto second=opennow::webrtc::qosPacket({2,9000});
        assert(controlSent.size()==2&&controlSent.back()==Bytes(second.begin(),second.end()));
        stream.stop();clean();assert(stream.start(session(),"test"));controlSent.clear();stream.tick(2000000);
        assert(controlSent==std::vector<Bytes>{Bytes(first.begin(),first.end())});stream.stop();clean();controlOpen=false;
    }
    {
        const auto rumble=command(0x10b,{1,0,6,0,0,0,0x34,0x12,0x78,0x56});
        const auto stopRumble=command(0x10b,{1,0,6,0,0,0,0,0,0,0});opennow::Rumble result;
        assert(stream.start(session(),"test"));receive(rumble);assert(!stream.takeRumble(result));
        dataCallback(v3,sizeof(v3),dataContext,0);receive(rumble,4);assert(!stream.takeRumble(result));
        receive(rumble);assert(stream.takeRumble(result)&&result.controllerId==0&&result.lowFrequency==0x1234&&result.highFrequency==0x5678&&result.durationMs==1000);
        assert(!stream.takeRumble(result));receive(rumble,0);receive(stopRumble);
        assert(stream.takeRumble(result)&&!result.lowFrequency&&!result.highFrequency);
        Bytes tooMany(256*4);tooMany.insert(tooMany.end(),rumble.begin(),rumble.end());receive(tooMany);assert(!stream.takeRumble(result));
        auto oversized=rumble;oversized.resize(65540);receive(oversized);assert(!stream.takeRumble(result));
        receive(rumble);receive(command(0x10b,{1,0,6,0,1,0,1,0,1,0}));assert(stream.takeRumble(result)&&result.controllerId==0&&result.lowFrequency==0x1234);
        receive(rumble);dataClosed(dataContext);assert(!stream.takeRumble(result));stream.tick(1000000);clean();
        assert(stream.start(session(),"test"));dataCallback(v2,sizeof(v2),dataContext,0);assert(!stream.takeRumble(result));
        receive(rumble);stream.stop();receive(rumble);assert(!stream.takeRumble(result));clean();
    }
}
