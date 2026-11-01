// SPDX-License-Identifier: GPL-3.0-or-later
#include "gfn.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

namespace {
using namespace std::chrono_literals;
struct Reply {
    const char* endpoint;
    long status;
    std::string body;
    const char* error = nullptr;
};
struct Transport {
    std::vector<Reply> replies;
    std::atomic_size_t calls{0};
    std::mutex mutex;
    std::condition_variable changed;
    std::size_t blockAt = std::string::npos;
    bool entered = false, released = false;
    explicit Transport(std::initializer_list<Reply> data) : replies(data) {}
    static opennow::Response request(void* context, const char* method, const char* url,
        const char* body, const char* bearer, const char*) {
        auto& self = *static_cast<Transport*>(context);
        const auto index = self.calls.fetch_add(1);
        assert(index < self.replies.size());
        auto& reply = self.replies[index];
        assert(std::strstr(url,reply.endpoint));
        assert(!std::strcmp(method,body ? "POST" : "GET"));
        if (std::strstr(url,"/userinfo") || std::strstr(url,"/client_token")) assert(bearer && *bearer);
        {
            std::unique_lock lock(self.mutex);
            if (index == self.blockAt) {
                self.entered = true; self.changed.notify_all();
                self.changed.wait(lock,[&] { return self.released; });
            }
            self.changed.notify_all();
        }
        return {reply.status,reply.body.data(),reply.body.size(),reply.error};
    }
    void waitEntered() {
        std::unique_lock lock(mutex);
        assert(changed.wait_for(lock,10s,[&] { return entered; }));
    }
    void waitRequests() {
        std::unique_lock lock(mutex);
        assert(changed.wait_for(lock,10s,[&] { return calls.load() == replies.size(); }));
    }
    void release() {
        std::lock_guard lock(mutex); released = true; changed.notify_all();
    }
};
const char* identity = "00000000-0000-4000-8000-000000000000";
const char* challenge = R"({"device_code":"synthetic-device","user_code":"TEST-CODE","verification_uri":"https://login.nvidia.com/activate","interval":5})";
Transport initial() {
    return Transport{{"/device/authorize",200,challenge},
        {"/token",200,R"({"access_token":"access1","id_token":"cloud1","refresh_token":"refresh1","expires_in":180})"},
        {"/userinfo",200,R"({"sub":"synthetic-user"})"},
        {"/client_token",200,R"({"client_token":"client1"})"}};
}
struct Account {
    char directory[64] = "/tmp/opennow-async-auth-XXXXXX";
    std::string path;
    Account() { assert(mkdtemp(directory)); path = std::string(directory) + "/account.bin"; }
    ~Account() { assert(access(path.c_str(),F_OK) != 0); assert(rmdir(directory) == 0); }
};
std::string contents(const std::string& path) {
    FILE* file = std::fopen(path.c_str(),"rb"); assert(file);
    std::string data; char bytes[1024]; std::size_t size;
    while ((size = std::fread(bytes,1,sizeof(bytes),file))) data.append(bytes,size);
    std::fclose(file); return data;
}
void signIn(opennow::Login& login) {
    login.begin(identity,0); login.tick(5);
    assert(login.view().state == opennow::State::authenticated && login.view().sessionSaved);
}
template<class Ready> void tickUntil(opennow::Login& login, Transport& transport, std::uint64_t now, Ready ready) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!ready()) {
        assert(std::chrono::steady_clock::now() < deadline);
        login.tickAsync(now,Transport::request,&transport);
        std::this_thread::yield();
    }
}
void startBlocked(opennow::Login& login, Transport& refresh) {
    std::promise<void> returned;
    auto result = returned.get_future();
    std::thread owner([&] { login.tickAsync(125,Transport::request,&refresh); returned.set_value(); });
    refresh.waitEntered();
    assert(result.wait_for(10s) == std::future_status::ready);
    owner.join();
}
}
int main() {
    {
        Account account;auto owner=initial();
        Transport refresh{{"/token",200,R"({"access_token":"access2","id_token":"e30.eyJleHAiOjF9.signature","expires_in":180})"},
            {"/userinfo",200,R"({"sub":"synthetic-user"})"},{"/client_token",404,"{}"}};
        opennow::Login login(Transport::request,&owner,account.path.c_str());signIn(login);
        tickUntil(login,refresh,125,[&]{return login.view().state==opennow::State::requesting;});
        assert(login.view().canSignOut&&!*login.cloudToken());
        login.cancel();
        assert(login.view().state==opennow::State::cancelled&&!login.view().canSignOut);
        assert(access(account.path.c_str(),F_OK)!=0);
    }
    {
        Account account; auto owner = initial();
        Transport refresh{{"/token",200,R"({"access_token":"access2","id_token":"cloud2","refresh_token":"rotated","expires_in":180})"},
            {"/userinfo",200,R"({"sub":"synthetic-user"})"},{"/client_token",404,"{}"}};
        refresh.blockAt = 0;
        opennow::Login login(Transport::request,&owner,account.path.c_str()); signIn(login);
        const auto saved = contents(account.path);
        startBlocked(login,refresh);
        for (unsigned i = 0; i < 20; ++i) login.tickAsync(125,Transport::request,&refresh);
        login.tick(125);
        assert(refresh.calls == 1 && owner.calls == 4);
        assert(!std::strcmp(login.cloudToken(),"cloud1") && contents(account.path) == saved);
        refresh.release(); refresh.waitRequests();
        assert(!std::strcmp(login.cloudToken(),"cloud1") && contents(account.path) == saved);
        tickUntil(login,refresh,125,[&] { return !std::strcmp(login.cloudToken(),"cloud2"); });
        assert(contents(account.path).find("rotated") != std::string::npos);
        assert(contents(account.path).find("cloud2") != std::string::npos);
        assert(login.view().profileVerified && login.view().sessionSaved && owner.calls == 4);
        login.tickAsync(244,Transport::request,&refresh); assert(refresh.calls == 3);
        login.cancel();
    }
    for (const auto& error : {Reply{"/token",503,"temporary"},Reply{"/token",429,"limited"},
        Reply{"/token",0,"","offline"},Reply{"/token",200,"malformed"}}) {
        Account account; auto owner = initial();
        Transport refresh{error,{"/token",401,R"({"error":"invalid_grant"})"},
            {"/token",200,R"({"access_token":"access2","id_token":"cloud2","expires_in":180})"},
            {"/userinfo",200,R"({"sub":"synthetic-user"})"},{"/client_token",404,"{}"}};
        refresh.blockAt = 2;
        opennow::Login login(Transport::request,&owner,account.path.c_str()); signIn(login);
        const auto saved = contents(account.path);
        login.tickAsync(125,Transport::request,&refresh);
        tickUntil(login,refresh,155,[&] { return refresh.calls.load() >= 3; });
        refresh.waitEntered();
        assert(login.view().state == opennow::State::authenticated && login.view().profileVerified);
        assert(!std::strcmp(login.cloudToken(),"cloud1") && contents(account.path) == saved);
        refresh.release();
        tickUntil(login,refresh,155,[&] { return !std::strcmp(login.cloudToken(),"cloud2"); });
        login.cancel();
    }
    {
        Account account; auto owner = initial();
        Transport refresh{{"/token",200,R"({"access_token":"access2","id_token":"cloud2","refresh_token":"rotated","expires_in":10})"},
            {"/userinfo",503,"temporary"}};
        refresh.blockAt = 0;
        opennow::Login login(Transport::request,&owner,account.path.c_str()); signIn(login);
        startBlocked(login,refresh);
        login.tickAsync(200,Transport::request,&refresh); assert(!*login.cloudToken());
        refresh.release();
        tickUntil(login,refresh,200,[&] { return login.view().state == opennow::State::requesting; });
        assert(!*login.cloudToken() && login.view().sessionSaved);
        assert(contents(account.path).find("rotated") != std::string::npos);
        login.cancel();
    }
    for (const auto& profile : {Reply{"/userinfo",503,"temporary"},Reply{"/userinfo",401,"{}"},
        Reply{"/userinfo",0,"","offline"},Reply{"/userinfo",200,"malformed"}}) {
        Account account; auto owner = initial();
        Transport refresh{{"/token",200,R"({"access_token":"access2","id_token":"cloud2","refresh_token":"rotated","expires_in":180})"},profile};
        opennow::Login login(Transport::request,&owner,account.path.c_str()); signIn(login);
        login.tickAsync(125,Transport::request,&refresh);
        tickUntil(login,refresh,125,[&] { return !std::strcmp(login.cloudToken(),"cloud2"); });
        const auto saved = contents(account.path);
        assert(saved.find("rotated") != std::string::npos && saved.find("synthetic-user") != std::string::npos);
        assert(login.view().profileVerified && login.view().sessionSaved);
        login.cancel();
    }
    {
        Account account; auto owner = initial();
        Transport refresh{{"/token",200,R"({"access_token":"foreign-access","id_token":"foreign-cloud","refresh_token":"foreign-refresh","expires_in":180})"},
            {"/userinfo",200,R"({"sub":"different-user"})"}};
        opennow::Login login(Transport::request,&owner,account.path.c_str()); signIn(login);
        login.tickAsync(125,Transport::request,&refresh);
        tickUntil(login,refresh,125,[&] { return login.view().state == opennow::State::failed; });
        assert(!*login.cloudToken() && access(account.path.c_str(),F_OK) != 0);
    }
    {
        Account account; auto owner = initial();
        Transport refresh{{"/token",400,R"({"error":"invalid_grant"})"},
            {"/token",401,R"({"error":"token_revoked"})"}};
        opennow::Login login(Transport::request,&owner,account.path.c_str()); signIn(login);
        login.tickAsync(125,Transport::request,&refresh);
        tickUntil(login,refresh,125,[&] { return login.view().state == opennow::State::failed; });
        assert(refresh.calls == 2 && !*login.cloudToken() && access(account.path.c_str(),F_OK) != 0);
    }
    {
        Account account; auto owner = initial();
        Transport refresh{{"/token",200,R"({"access_token":"access2","id_token":"cloud2","refresh_token":"close-rotation","expires_in":180})"},
            {"/userinfo",0,"","Request cancelled"}};
        refresh.blockAt = 1;
        opennow::Login login(Transport::request,&owner,account.path.c_str()); signIn(login);
        const auto saved = contents(account.path);
        startBlocked(login,refresh);
        std::promise<void> started, returned;
        auto entered = started.get_future(), result = returned.get_future();
        std::thread closing([&] { started.set_value(); login.finishAsync(126); returned.set_value(); });
        entered.wait(); assert(result.wait_for(50ms) == std::future_status::timeout);
        assert(contents(account.path) == saved);
        refresh.release(); assert(result.wait_for(10s) == std::future_status::ready); closing.join();
        assert(!std::strcmp(login.cloudToken(),"cloud2") && login.view().sessionSaved);
        assert(contents(account.path).find("close-rotation") != std::string::npos);
        assert(refresh.calls == 2); login.cancel();
    }
    for (auto blockAt : {0U,1U}) for (bool destroying : {false,true}) {
        Account account; auto owner = initial();
        Transport refresh{{"/token",200,R"({"access_token":"access2","id_token":"cloud2","refresh_token":"rotated","expires_in":180})"},
            {"/userinfo",503,"temporary"}};
        refresh.blockAt = blockAt;
        auto* login = new opennow::Login(Transport::request,&owner,account.path.c_str()); signIn(*login);
        const auto saved = contents(account.path);
        startBlocked(*login,refresh);
        std::promise<void> started, returned;
        auto entered = started.get_future(), result = returned.get_future();
        std::thread closing([&] {
            started.set_value();
            if (destroying) delete login; else login->cancel();
            returned.set_value();
        });
        entered.wait(); assert(result.wait_for(50ms) == std::future_status::timeout);
        assert(contents(account.path) == saved);
        refresh.release(); assert(result.wait_for(10s) == std::future_status::ready); closing.join();
        assert(refresh.calls == blockAt + 1);
        if (destroying) {
            assert(contents(account.path) == saved);
            opennow::Login cleanup(Transport::request,&owner,account.path.c_str()); cleanup.cancel();
        } else {
            assert(login->view().state == opennow::State::cancelled && !*login->cloudToken());
            login->tickAsync(200,Transport::request,&refresh);
            assert(access(account.path.c_str(),F_OK) != 0); delete login;
        }
    }
    std::puts("GFN asynchronous renewal, publication, fallback and teardown regressions passed");
}
