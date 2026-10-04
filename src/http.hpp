// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "gfn.hpp"
#include <atomic>
#include <curl/curl.h>
namespace opennow {
class Http {
public:
    std::atomic_bool cancelled{false};
    Http() noexcept;
    ~Http();
    bool ready() const noexcept { return buffer_ != nullptr; }
    static Response request(void*, const char*, const char*, const char*, const char*, const char*) noexcept;
private:
    static constexpr std::size_t limit = 2 * 1024 * 1024;
    char* buffer_ = nullptr;
    std::size_t size_ = 0;
    char error_[CURL_ERROR_SIZE]{};
    static std::size_t write(char*,std::size_t,std::size_t,void*) noexcept;
    static int progress(void*,curl_off_t,curl_off_t,curl_off_t,curl_off_t) noexcept;
};
}
