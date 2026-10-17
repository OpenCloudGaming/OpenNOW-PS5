// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "../app_storage.h"

enum { file_size = 320 };
static const char app_root[] = OPENNOW_SANDBOX_APP_ROOT;
static const char storage_root[] = OPENNOW_SANDBOX_STORAGE_ROOT;

#define APP_FILES(X) X("assets/cacert.pem") X("assets/forwarder-template") X("opennow-tile-installer.elf")
#define STORAGE_FILES(X) X("account.bin") X("settings.bin") X("session.bin") X("artwork") \
    X("network.log") X("media.log") X("ui-perf.log") X("video-sps.bin") X("live-video.status") X("tile-stage")
#define NAME(n) n,
#define APP_PATH(n) OPENNOW_SANDBOX_APP_ROOT "/" n,
#define STORAGE_PATH(n) OPENNOW_SANDBOX_STORAGE_ROOT "/" n,
static const char *const app_names[] = {APP_FILES(NAME)};
static const char *const storage_names[] = {STORAGE_FILES(NAME)};
static const char app_files[][file_size] = {APP_FILES(APP_PATH)};
static const char storage_files[][file_size] = {STORAGE_FILES(STORAGE_PATH)};
enum {
    app_count = sizeof(app_names) / sizeof(app_names[0]),
    storage_count = sizeof(storage_names) / sizeof(storage_names[0])
};

const char *opennow_app_root(void) { return app_root; }
const char *opennow_storage_root(void) { return storage_root; }

const char *opennow_app_file(const char *name)
{
    for (unsigned i = 0; i < app_count; ++i)
        if (!strcmp(app_names[i], name))
            return app_files[i];
    return "";
}

const char *opennow_storage_file(const char *name)
{
    for (unsigned i = 0; i < storage_count; ++i)
        if (!strcmp(storage_names[i], name))
            return storage_files[i];
    return "";
}
