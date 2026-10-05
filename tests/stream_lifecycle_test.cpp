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
int runtimeResult = 0;
PeerConnectionState nextState = PEER_CONNECTION_NEW;
std::string incoming;
std::vector<std::string> candidates;
std::string remoteSdp;
std::vector<std::string> outbound;
std::vector<std::string> diagnostics;
std::string connectedUrl;
std::vector<opennow::wire::Bytes> sent;
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
    "m=video 9 UDP/TLS/RTP/SAVPF 96\r\na=mid:video\r\na=rtpmap:96 H264/90000\r\n"
    "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=mid:audio\r\na=rtpmap:111 opus/48000/2\r\n"
    "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\na=mid:datachannel\r\n";

opennow::Session session() {
    opennow::Session value;
    std::strcpy(value.id, "session-test");
    std::strcpy(value.signaling, "wss://signaling.example/nvst/");
    return value;
}

void queuePayload(const char* key, const char* value) {
    auto* payload = cJSON_CreateObject();
    if (std::strcmp(key, "sdp") == 0) cJSON_AddStringToObject(payload, "type", "offer");
    cJSON_AddStringToObject(payload, key, value);
    auto* json = cJSON_PrintUnformatted(payload);
    auto* root = cJSON_CreateObject();
    auto* msg = cJSON_AddObjectToObject(root, "peer_msg");
    cJSON_AddNumberToObject(msg, "from", 1);
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
bool Media::start(const StreamSettings&) noexcept { frames = 0; mediaRunning = mediaStarts; return mediaStarts; }
void Media::stop() noexcept { mediaRunning = false; }
void Media::requireKeyframe() noexcept {}
bool Media::video(const std::uint8_t*, std::size_t) noexcept { return true; }
void Media::audio(const std::uint8_t*, std::size_t, std::uint16_t, std::uint8_t, std::uint32_t) noexcept {}
bool randomBytes(void* output, std::size_t size) noexcept { std::memset(output, 0, size); return entropyWorks; }
void secureErase(void* data, std::size_t size) noexcept { std::memset(data, 0, size); }
}

extern "C" {
unsigned long long sceKernelGetProcessTime() { return 1000000; }
void opennow_media_note(const char* note) { diagnostics.emplace_back(note); }
int opennow_peer_random(unsigned char*, std::size_t);
int peer_init() { if (!runtimeResult) ++runtimes; return runtimeResult; }
void peer_deinit() { --runtimes; }
void peer_connection_set_diagnostics_enabled(int) {}
PeerConnection* peer_connection_create(PeerConfiguration* config) {
    if (!peerStarts) return nullptr;
    candidates.clear();
    remoteSdp.clear();
    unsigned char byte;
    opennow_peer_random(&byte, 1);
    ++peers;
    return new PeerConnection{*config};
}
void peer_connection_close(PeerConnection*) {}
void peer_connection_destroy(PeerConnection* pc) { assert(!inPoll && !inLoop); --peers; delete pc; }
int peer_connection_get_video_rtp_stats(PeerConnection*, PeerVideoRtpStats* stats) { *stats = {}; stats->assembler_ready = assemblerReady; return 0; }
void peer_connection_onicecandidate(PeerConnection* pc, void (*callback)(char*, void*)) { localIce = callback; localIceContext = pc->config.user_data; }
void peer_connection_oniceconnectionstatechange(PeerConnection* pc, void (*fn)(PeerConnectionState, void*)) { pc->onState = fn; }
void peer_connection_ondatachannel(PeerConnection* pc, void (*message)(char*, size_t, void*, uint16_t), void (*open)(void*), void (*close)(void*)) { dataCallback = message; dataOpened = open; dataClosed = close; dataContext = pc->config.user_data; }
void peer_connection_set_remote_description(PeerConnection*, const char* sdp, SdpType) { remoteSdp = sdp; }
const char* peer_connection_create_answer(PeerConnection*) {
    if (nullAnswer) return nullptr;
    char candidate[] = "a=candidate:local 1 udp 1234 192.168.1.2 50000 typ host\r\n";
    localIce(candidate, localIceContext);
    return answer;
}
int peer_connection_add_ice_candidate(PeerConnection*, char* candidate) { candidates.emplace_back(candidate); return 0; }
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
int peer_connection_request_video_keyframe(PeerConnection*) { return 0; }
int peer_connection_create_datachannel_sid(PeerConnection*, DecpChannelType, uint16_t, uint32_t, char*, char*, uint16_t) { return 0; }
int peer_connection_datachannel_is_open(PeerConnection*, uint16_t) { return 0; }
int peer_connection_datachannel_send_binary_sid(PeerConnection*, char* data, size_t size, uint16_t sid) { if (!sid) sent.emplace_back(data, data + size); return 0; }
}

int main() {
    opennow::Media media;
    opennow::Stream stream(media);
    auto launch = session();
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

    for (auto state : {PEER_CONNECTION_FAILED, PEER_CONNECTION_CLOSED}) {
        assert(stream.start(launch, "test"));
        media.frames = 1;
        nextState = state;
        stream.tick(1000000);
        assert(stream.failed());
        clean();
    }

    assert(stream.start(launch, "test"));
    nextState = PEER_CONNECTION_DISCONNECTED;
    stream.tick(1000000);
    assert(!stream.failed() && stream.active());
    media.frames = 1;
    stream.tick(46000001);
    assert(!stream.failed() && stream.active());
    stream.stop();
    clean();

    assert(stream.start(launch, "test"));
    stream.tick(1000000);
    stream.tick(46000001);
    assert(std::strstr(stream.status(), "Video timeout"));
    assert(stream.failed());
    clean();

    launch.profile = opennow::StreamProfile::native_hdr60;
    assert(stream.start(launch, "test"));
    queuePayload("sdp", answer);
    stream.tick(1000000);
    assert(std::strstr(stream.status(), "selected video codec"));
    assert(stream.failed());
    clean();
    launch.profile = opennow::StreamProfile::quality;

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
        std::strcpy(launch.signaling, "wss://signaling.example/nvst/sign_in?discard=1#fragment");
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
        0x0c, 0, 0, 0, 0x1a, 0, 0, 0, 0x01, 0, 0x14, 0, 0x20, 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x55, 0, 0, 0,
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
    assert(!stream.inputReady());
    stream.events(queue, 3000001);
    assert(sent.empty());
    dataOpened(dataContext);
    stream.tick(4000000);
    assert(stream.inputReady());
    sent.clear();
    queue.move(5, 0);
    stream.events(queue, 4000001);
    assert(sent.size() == 4);
    assert(sent[0] == wire::key(false, 0x41, 0x1e, 1, 2, 4000001));
    assert(sent[1] == wire::key(false, 0xa0, 0x2a, 0, 2, 4000001));
    assert(sent[2] == wire::mouseButton(false, 1, 2, 4000001));
    assert(sent[3] == wire::mouseMove(5, 0, 2, 4000001));
    sent.clear();
    stream.stop();
    assert(sent.size() == 1 && sent[0].size() == 38);
    clean();
    media.frames = 0;
}
