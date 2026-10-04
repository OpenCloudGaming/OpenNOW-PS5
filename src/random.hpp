// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
namespace opennow {
// Kernel-backed entropy. Returns false with cleared output if the provider fails.
bool randomBytes(void* output, std::size_t length) noexcept;
}
