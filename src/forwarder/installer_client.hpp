// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "forwarder.hpp"

namespace opennow::forwarder::installer {
bool available() noexcept;
Result install(const char* stagedFolder,const Job& job={}) noexcept;
Result list(Visit,void*) noexcept;
Result remove(const char* titleId) noexcept;
}
