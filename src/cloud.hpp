// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "gfn.hpp"
#include "stream/stream_settings.hpp"
namespace opennow {
struct Game { char id[96]{}, title[160]{}, store[48]{}, art[160]{}, hero[160]{}; bool owned=false; };
enum class CloudState { idle, loading, catalog, starting, queued, ready, failed };
enum class CatalogSource { browse, library };
constexpr unsigned catalogPageSize=24;
constexpr unsigned catalogMaxPages=128;
constexpr unsigned emptyPageSkips=12;
enum class SignalingSource { none, explicitUrl, streamConnection, alternateConnection, sessionControl };
struct CloudView {
    CloudState state=CloudState::idle;
    Game games[60]{};
    unsigned count=0, selected=0;
    unsigned revision=0;
    unsigned page=0;
    int queuePosition=-1;
    int setupStep=-1;
    bool hasNext=false;
    bool launchError=false;
    Game current{};
    char message[192]{};
};
struct Session {
    char id[128]{}, signaling[1024]{}, mediaIp[128]{};
    int mediaPort=0;
    SignalingSource signalingSource=SignalingSource::none;
    StreamSettings settings{};
    unsigned audioChannels=2;
};
static_assert(std::is_trivially_copyable_v<Session> && std::is_standard_layout_v<Session>);
class Cloud {
public:
    Cloud(Request r,void* c):request_(r),context_(c){}
    void load(const char* jwt,const char* device,const char* search="",bool next=false) noexcept;
    void loadPage(CatalogSource source,unsigned page,const char* jwt,const char* device,int direction=1) noexcept;
    void select(int delta) noexcept;
    void focus(unsigned index) noexcept;
    void launch(const char* jwt,const char* device,std::uint64_t now,const StreamSettings& settings={},unsigned audioChannels=2) noexcept;
    void launchEntry(CatalogSource source,unsigned index,const char* jwt,const char* device,std::uint64_t now,const StreamSettings& settings,unsigned audioChannels=2) noexcept;
    void dismissLaunchError() noexcept;
    void tick(const char* jwt,const char* device,std::uint64_t now) noexcept;
    bool stop(const char* jwt,const char* device) noexcept;
    void reset() noexcept;
    const CloudView& view() const {return view_;}
    const CloudView& library() const {return library_;}
    const Session& session() const {return session_;}
private:
    void fail(const char*) noexcept;
    void failLaunch(const char*) noexcept;
    bool connect(const char* jwt,const char* device,CloudView& target) noexcept;
    bool fetchPage(CatalogSource source,unsigned page,const char* jwt,const char* device) noexcept;
    void start(const Game& game,const char* jwt,const char* device,std::uint64_t now,const StreamSettings& settings,unsigned audioChannels) noexcept;
    bool parseSession(const Response&) noexcept;
    Request request_;void* context_;
    CloudView view_{},library_{};Session session_{};
    char base_[512]{},vpc_[128]{},search_[128]{};
    char browseCursors_[catalogMaxPages][128]{},libraryCursors_[catalogMaxPages][128]{};
    std::uint64_t nextPoll_=0;
};
bool trustedCloudUrl(const char*) noexcept;
bool parseCatalog(const Response&,CloudView&,char*,std::size_t,bool ownedOnly=false) noexcept;
bool ownedLibraryStatus(const char* status) noexcept;
bool artworkUrl(const char* url) noexcept;
}
