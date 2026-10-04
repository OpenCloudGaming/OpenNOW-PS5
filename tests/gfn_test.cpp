// SPDX-License-Identifier: GPL-3.0-or-later
#include "gfn.hpp"
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <cstdio>
struct Reply { long code; std::string body; const char* error=nullptr; };
struct Fake {
    std::vector<Reply> replies;
    unsigned calls=0;
    static opennow::Response request(void* p,const char* method,const char* url,const char* body,const char* bearer,const char*) {
        auto& self=*static_cast<Fake*>(p); assert(self.calls<self.replies.size());
        if (self.calls==0) { assert(std::strcmp(method,"POST")==0); assert(std::strstr(url,"/device/authorize")); assert(std::strstr(body,"scope=openid%20consent")); }
        if (std::strstr(url,"/userinfo")) { assert(std::strcmp(method,"GET")==0); assert(bearer && std::strcmp(bearer,"test-token")==0); }
        auto& r=self.replies[self.calls++]; return {r.code,r.body.data(),r.body.size(),r.error};
    }
};
const char* challenge=R"({"device_code":"device&code","user_code":"TEST-CODE","verification_uri":"https://static-login.nvidia.com/service/gfn/pin","verification_uri_complete":"https://static-login.nvidia.com/service/gfn/pin?user_code=TEST-CODE","expires_in":60,"interval":5})";
int main() {
    using opennow::State;
    char out[64]; assert(opennow::encodeForm("a& b+",out,sizeof(out))); assert(std::strcmp(out,"a%26%20b%2B")==0);
    assert(!opennow::encodeForm("long",out,3));
    assert(opennow::trustedVerificationUrl("https://static-login.nvidia.com/service/gfn/pin"));
    for (auto s:{"http://login.nvidia.com/activate","https://login.nvidia.com.evil.test/","https://login.nvidia.com@evil.test/","https://evil.test/","https://login.nvidia.com/\n"}) assert(!opennow::trustedVerificationUrl(s));
    {
        Fake f{{{200,challenge},{400,R"({"error":"authorization_pending"})"},{400,R"({"error":"slow_down"})"},{200,R"({"access_token":"test-token"})"},{200,R"({"sub":"test-user"})"}}};
        opennow::Login login(Fake::request,&f); login.begin("test-device",100);
        assert(login.view().state==State::waiting); login.tick(104); assert(f.calls==1);
        login.tick(105); assert(f.calls==2); login.tick(110); assert(f.calls==3);
        login.tick(115); assert(f.calls==3); login.tick(120); assert(f.calls==5);
        assert(login.view().state==State::authenticated && login.view().profileVerified);
        assert(login.view().code[0]==0); login.cancel(); assert(login.view().state==State::cancelled);
    }
    {
        Fake f{{{200,challenge}}}; opennow::Login login(Fake::request,&f);
        login.begin("id",0); login.tick(60); assert(login.view().state==State::expired && f.calls==1);
        login.cancel(); login.tick(100); assert(f.calls==1);
    }
    for (auto error:{"access_denied","expired_token","unknown"}) {
        Fake f{{{200,challenge},{400,std::string("{\"error\":\"")+error+"\"}"}}};
        opennow::Login login(Fake::request,&f);login.begin("id",0);login.tick(5);
        assert(login.view().state!=State::waiting && login.view().state!=State::authenticated);
        assert(login.view().code[0]==0);
    }
    for (auto bad:{"{}","not JSON",R"({"device_code":"a","user_code":"a","verification_uri":"https://evil.test/"})"}) {
        Fake f{{{200,bad}}}; opennow::Login login(Fake::request,&f);login.begin("id",0);assert(login.view().state==State::failed);
    }
    {
        Fake f{{{200,challenge},{503,"temporary"},{200,R"({"access_token":"test-token"})"},{401,"{}"}}};
        opennow::Login login(Fake::request,&f);login.begin("id",0);login.tick(5);login.tick(10);assert(f.calls==2);
        login.tick(15);assert(login.view().state==State::failed && !login.view().profileVerified);
    }
    {
        Fake f{{{200,std::string(challenge)+" trailing"}}};opennow::Login login(Fake::request,&f);login.begin("id",0);assert(login.view().state==State::failed);
    }
    std::puts("GFN authorization regression tests passed");
}
