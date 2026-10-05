#include <cstddef>
#include "stream/native/gpu_presenter.hpp"
extern "C" {
#include "platform/ps5-pad.h"
int sceUserServiceGetInitialUser(int*) {return -1;}
int scePadOpen(int,int,int,void*) {return -1;}
int scePadReadState(int,PS5_PadData*) {return -1;}
}
namespace opennow::gpu {
bool settingsAvailable(const StreamSettings& settings) noexcept {
    return validateSettings(settings)==SettingsError::none&&!(settings.hdr()&&settings.fps>90);
}
StreamSettings bestSettings() noexcept {return settingsFor(StreamProfile::native_hdr90);}
}
