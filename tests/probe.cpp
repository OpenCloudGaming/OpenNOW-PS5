// SPDX-License-Identifier: GPL-3.0-or-later
#include "gfn.hpp"
#include "http.hpp"
#include "vendor/cJSON.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
int main() {
    unsigned char random[16];
    arc4random_buf(random,sizeof(random));
    char id[33]; for (unsigned i=0;i<16;++i) std::snprintf(id+2*i,3,"%02x",random[i]);
    opennow::Http http; opennow::Login login(opennow::Http::request,&http);
    login.begin(id,0);
    const bool ok=login.view().state==opennow::State::waiting;
    // Intentionally omit device code, user code, QR URL and response body.
    std::printf("NVIDIA device authorization: %s\n",ok ? "valid challenge received" : login.view().message);
    login.cancel();
    return ok ? 0 : 1;
}
