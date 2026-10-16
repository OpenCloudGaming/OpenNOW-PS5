// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

struct dirent {
    std::uint32_t d_fileno;
    std::uint16_t d_reclen;
    std::uint8_t d_type;
    std::uint8_t d_namlen;
    char d_name[256];
};
