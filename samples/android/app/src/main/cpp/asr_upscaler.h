// Copyright © 2026 Arm Limited.
// SPDX-License-Identifier: MIT
#pragma once

#include "common.h"
#include "scene.h"
#include "vk_context.h"

#include <host/backends/vk/ffxm_vk.h>
#include <host/ffxm_fsr2.h>

#include <cstdint>
#include <vector>

// Wraps the Arm ASR quick-integration path: scratch buffer -> FfxmDevice ->
// FfxmInterface -> FfxmFsr2Context -> per-frame dispatch.
class AsrUpscaler {
public:
    bool init(VkContext& ctx, VkExtent2D renderExtent, VkExtent2D displayExtent,
              arm::FfxmFsr2ShaderQualityMode quality);
    void destroy();

    // Records the upscale for this frame. `frameIndex` drives the jitter
    // sequence; `reset` should be true on the first frame after a camera cut.
    bool dispatch(VkCommandBuffer cmd, const Scene& scene, VkExtent2D renderExtent,
                  float frameTimeMs, int32_t frameIndex, bool reset);

    // Sub-pixel jitter in pixels for `frameIndex`, to be folded into the camera.
    void jitterForFrame(int32_t frameIndex, float* outX, float* outY) const;

    static uint32_t initializationFlags();

private:
    VkDevice                 device_        = VK_NULL_HANDLE;
    arm::FfxmInterface       interface_     = {};
    arm::FfxmFsr2Context     context_       = {};
    std::vector<uint8_t>     scratchBuffer_;
    bool                     contextCreated_= false;
    int32_t                  jitterPhaseCount_ = 1;
    VkExtent2D               renderExtent_  = {0, 0};
    VkExtent2D               displayExtent_ = {0, 0};
};
