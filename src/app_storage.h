// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#define OPENNOW_STORAGE_ROOT "/download0/opennow"
#define OPENNOW_ACCOUNT_PATH OPENNOW_STORAGE_ROOT "/account.bin"
#define OPENNOW_SETTINGS_PATH OPENNOW_STORAGE_ROOT "/settings.bin"
#define OPENNOW_ARTWORK_CACHE_PATH OPENNOW_STORAGE_ROOT "/artwork"
#define OPENNOW_LEGACY_ACCOUNT_PATH "/data/opennow/account.bin"

#ifdef __cplusplus
#include "account_file.hpp"

namespace opennow::appStorage
{
inline int initialize() noexcept
{
    if (accountFile::makeDirectory(OPENNOW_STORAGE_ROOT, 0700) != 0 && errno != EEXIST)
        return errno;
    const int fd = accountFile::open(OPENNOW_STORAGE_ROOT, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0)
        return errno;
    if (accountFile::close(fd) != 0)
        return errno;
    if (!accountFile::syncParent(OPENNOW_STORAGE_ROOT))
        return errno;
    return 0;
}

inline const char *accountPath() noexcept
{
    const int fd = accountFile::open(OPENNOW_LEGACY_ACCOUNT_PATH, O_RDONLY | O_NOFOLLOW);
    if (fd < 0)
        return OPENNOW_ACCOUNT_PATH;
    accountFile::close(fd);
    return OPENNOW_LEGACY_ACCOUNT_PATH;
}
}
#endif
