// SPDX-License-Identifier: GPL-3.0-or-later
// Public interface declarations adapted from Kodi PS5 VideoDec2.cpp,
// Copyright (C) 2026 Team Kodi, GPL-2.0-or-later, used under GPL-3.0-or-later.
// Interface and call sequence are corroborated by pinned ProsperoLight.
#pragma once
#include <cstddef>
#include <cstdint>
extern "C"
{
struct sceVideodec2DecoderConfig
{
  uint64_t size;
  uint32_t resourceType, codecType, profile, maxLevel;
  int32_t maxWidth, maxHeight, maxDpbFrames;
  uint32_t pipelineDepth;
  uint64_t computeQueue, cpuAffinity;
  int32_t cpuPriority;
  uint32_t optimizeProgressive, checkMemoryType, reserved;
};

struct sceVideodec2DecoderMemory
{
  uint64_t size, cpuSize;
  void* cpu;
  uint64_t gpuSize;
  void* gpu;
  uint64_t cpuGpuSize;
  void* cpuGpu;
  uint64_t maxFrameSize;
  uint32_t frameAlignment, reserved;
};

struct sceVideodec2ComputeConfig
{
  uint64_t size;
  uint16_t pipeId, queueId;
  uint8_t checkMemoryType, reserved0;
  uint16_t reserved1;
};

struct sceVideodec2ComputeMemory
{
  uint64_t size, cpuGpuSize;
  void* cpuGpu;
};

struct sceVideodec2Input
{
  uint64_t size;
  void* au;
  uint64_t auSize, pts, dts, attached;
};

struct sceVideodec2Frame
{
  uint64_t size;
  void* buffer;
  uint64_t bufferSize;
  uint32_t accepted, reserved;
};

struct sceVideodec2Output
{
  uint64_t size;
  uint8_t valid, error, pictureCount, padding;
  uint32_t codec, width, pitch, height, reserved;
  void* buffer;
  uint64_t bufferSize;
  uint32_t frameFormat, pitchBytes;
};

int32_t sceVideodec2QueryComputeMemoryInfo(sceVideodec2ComputeMemory* memory);
int32_t sceVideodec2AllocateComputeQueue(const sceVideodec2ComputeConfig* config,
                                         const sceVideodec2ComputeMemory* memory, void** queue);
int32_t sceVideodec2ReleaseComputeQueue(void* queue);
int32_t sceVideodec2QueryDecoderMemoryInfo(const sceVideodec2DecoderConfig* config,
                                           sceVideodec2DecoderMemory* memory);
int32_t sceVideodec2CreateDecoder(const sceVideodec2DecoderConfig* config,
                                  const sceVideodec2DecoderMemory* memory, void** decoder);
int32_t sceVideodec2DeleteDecoder(void* decoder);
int32_t sceVideodec2Decode(void* decoder, sceVideodec2Input* input, sceVideodec2Frame* frame,
                           sceVideodec2Output* output);
int32_t sceVideodec2Flush(void* decoder, sceVideodec2Frame* frame, sceVideodec2Output* output);
int32_t sceVideodec2Reset(void* decoder);

int32_t sceSysmoduleLoadModule(uint32_t id);
}

static_assert(sizeof(sceVideodec2DecoderConfig)==72);
static_assert(sizeof(sceVideodec2DecoderMemory)==72);
static_assert(sizeof(sceVideodec2ComputeConfig)==16);
static_assert(sizeof(sceVideodec2ComputeMemory)==24);
static_assert(sizeof(sceVideodec2Input)==48);
static_assert(sizeof(sceVideodec2Frame)==32);
static_assert(sizeof(sceVideodec2Output)==56);
static_assert(offsetof(sceVideodec2Output,buffer)==32);
