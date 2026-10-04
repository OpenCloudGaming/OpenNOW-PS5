// SPDX-License-Identifier: GPL-3.0-or-later
// CPU selection policy adapted from ProsperoLight moonlight_pipeline.hpp,
// Copyright (C) 2026 BlackBearReloaded, GPL-3.0-or-later.
#pragma once
#include <bit>
#include <cstdint>

namespace opennow::video {
// Logical CPU numbering and the five-core bound are documented by the
// pinned ProsperoLight moonlight_pipeline.hpp reference. Adjacent CPUs are
// SMT siblings; the historical 0x3f mask covers three physical cores.
constexpr std::uint64_t classicDecoderWorkers=0x3f;
constexpr std::uint64_t titleCpuEnvelope=0x1fff;
constexpr std::uint64_t decoderWorkers(std::uint64_t available) noexcept {
 available&=titleCpuEnvelope;
 if((available&classicDecoderWorkers)!=classicDecoderWorkers)return classicDecoderWorkers;
 std::uint64_t selected=0;unsigned cores=0;
 for(unsigned cpu=0;cpu<12&&cores<5;cpu+=2){
  const auto pair=std::uint64_t(3)<<cpu;
  if((available&pair)!=pair)continue;
  const auto candidate=selected|pair;
  if(std::popcount(available&~candidate)<2)break;
  selected=candidate;++cores;
 }
 return cores>=3?selected:classicDecoderWorkers;
}
}
