// SPDX-License-Identifier: GPL-3.0-or-later
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <fcntl.h>
#include "app_storage.h"

namespace
{
std::vector<std::string> opened;
bool directoryExists = false;
bool legacyReadable = false;
int mkdirError = 0;
int directoryError = 0;
int syncError = 0;
int closeError = 0;
int syncCalls = 0;
int nativeError(int error)
{
    return error ? static_cast<int>(0x80020000u | static_cast<unsigned>(error)) : 0;
}
void reset()
{
    opened.clear();
    directoryExists = false;
    legacyReadable = false;
    mkdirError = directoryError = syncError = closeError = syncCalls = 0;
}
}

extern "C" int sceKernelOpen(const char *path, int flags, unsigned mode)
{
    opened.emplace_back(path);
    const std::string name = path;
    if (name == "/download0/opennow" || name == "/download0")
    {
        assert(flags == (O_RDONLY | O_DIRECTORY | (name == "/download0/opennow" ? O_NOFOLLOW : 0)));
        assert(mode == 0);
        if (directoryError)
            return nativeError(directoryError);
        return 7;
    }
    if (name == "/data/opennow/account.bin")
    {
        assert(flags == (O_RDONLY | O_NOFOLLOW));
        assert(mode == 0);
        return legacyReadable ? 7 : nativeError(EACCES);
    }
    assert(flags == (O_WRONLY | O_CREAT | O_APPEND));
    assert(mode == 0644);
    assert(directoryExists);
    return 7;
}
extern "C" int sceKernelMkdir(const char *path, unsigned mode)
{
    assert(std::string(path) == "/download0/opennow");
    assert(mode == 0700);
    if (mkdirError)
        return nativeError(mkdirError);
    if (directoryExists)
        return nativeError(EEXIST);
    directoryExists = true;
    return 0;
}
extern "C" int sceKernelFsync(int fd)
{
    assert(fd == 7);
    ++syncCalls;
    return nativeError(syncError);
}
extern "C" std::int64_t sceKernelWrite(int fd, const void *, std::size_t size)
{
    assert(fd == 7);
    return static_cast<std::int64_t>(size);
}
extern "C" int sceKernelClose(int fd)
{
    assert(fd == 7);
    return nativeError(closeError);
}
extern "C" void opennow_network_note(const char *);
extern "C" void opennow_media_note(const char *);

int main()
{
    using namespace opennow::appStorage;
    assert(initialize() == 0);
    assert(directoryExists && syncCalls == 1);
    assert((opened == std::vector<std::string>{"/download0/opennow", "/download0"}));
    assert(initialize() == 0);
    assert(syncCalls == 2);
    opened.clear();
    opennow_network_note("socket result=1 errno=0");
    opennow_media_note("VIDEO dimensions changed: 1920x1080");
    assert((opened == std::vector<std::string>{"/download0/opennow/network.log",
                                               "/download0/opennow/media.log"}));

    assert(std::string(accountPath()) == "/download0/opennow/account.bin");
    legacyReadable = true;
    assert(std::string(accountPath()) == "/data/opennow/account.bin");
    assert(std::string(accountPath()) == "/data/opennow/account.bin");

    reset();
    mkdirError = EACCES;
    assert(initialize() == EACCES);
    assert(opened.empty() && !directoryExists && syncCalls == 0);
    reset();
    directoryExists = true;
    directoryError = ENOTDIR;
    assert(initialize() == ENOTDIR);
    assert(syncCalls == 0);
    reset();
    syncError = EIO;
    assert(initialize() == EIO);
    syncError = 0;
    assert(initialize() == 0);
    reset();
    closeError = EIO;
    assert(initialize() == EIO);
    assert(syncCalls == 0);
}
