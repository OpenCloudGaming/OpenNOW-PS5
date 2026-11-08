// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#define OPENNOW_SANDBOX_APP_ROOT "/app0"
#define OPENNOW_SANDBOX_STORAGE_ROOT "/download0/opennow"
#define OPENNOW_STORAGE_ROOT opennow_storage_root()
#define OPENNOW_ACCOUNT_PATH opennow_storage_file("account.bin")
#define OPENNOW_SETTINGS_PATH opennow_storage_file("settings.bin")
#define OPENNOW_SESSION_PATH opennow_storage_file("session.bin")
#define OPENNOW_ARTWORK_CACHE_PATH opennow_storage_file("artwork")
#define OPENNOW_CACERT_PATH opennow_app_file("assets/cacert.pem")
#define OPENNOW_LEGACY_ACCOUNT_PATH "/data/opennow/account.bin"

#ifdef __cplusplus
extern "C" {
#endif
const char *opennow_app_root(void);
const char *opennow_storage_root(void);
const char *opennow_app_file(const char *name);
const char *opennow_storage_file(const char *name);
#ifdef __cplusplus
}

#include "account_file.hpp"

namespace opennow::appStorage
{
inline int initialize() noexcept
{
    if (accountFile::makeDirectory(OPENNOW_STORAGE_ROOT, 0755) != 0 && errno != EEXIST)
        return errno;
    const int fd = accountFile::open(OPENNOW_STORAGE_ROOT, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0)
        return errno;
    // Lets FTP read the 0644 diagnostics; account, settings and session files stay 0600.
    // Best effort: a failure only hides logs and must not block login storage.
    (void)accountFile::permissions(fd, 0755);
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
