// Copyright © 2026 Arm Limited.
// SPDX-License-Identifier: MIT
#pragma once

#include "common.h"
#include "vk_context.h"

// The render-resolution G-buffer Arm ASR consumes, plus the display-resolution
// target it writes into.
struct RenderTarget {
    VkImage        image  = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView    view   = VK_NULL_HANDLE;
    VkFormat       format = VK_FORMAT_UNDEFINED;
    VkExtent2D     extent = {0, 0};
};

struct ScenePushConstants {
    float renderSize[2];
    float jitter[2];
    float camZ;
    float camZPrev;
    float tanHalfFov;
    float aspect;
    float nearZ;
    float farZ;
    float planeY;
    float time;
};

class Scene {
public:
    static constexpr VkFormat kColorFormat  = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat kMotionFormat = VK_FORMAT_R16G16_SFLOAT;
    static constexpr VkFormat kDepthFormat  = VK_FORMAT_D32_SFLOAT;
    static constexpr VkFormat kOutputFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    bool init(VkContext& ctx, VkExtent2D renderExtent, VkExtent2D displayExtent);
    void destroy(VkContext& ctx);

    // Records the low-resolution pass. Leaves color/motion in
    // COLOR_ATTACHMENT_OPTIMAL and depth in DEPTH_STENCIL_ATTACHMENT_OPTIMAL.
    void record(VkCommandBuffer cmd, const ScenePushConstants& pc);

    // Transitions the G-buffer into the layouts Arm ASR expects for its inputs
    // and the output into GENERAL for the upscaler's UAV write.
    void barrierForUpscale(VkCommandBuffer cmd);

    // Presents the upscaled result into a swapchain image.
    void recordBlit(VkCommandBuffer cmd, VkImageView swapchainView, VkExtent2D extent);

    const RenderTarget& color()  const { return color_; }
    const RenderTarget& motion() const { return motion_; }
    const RenderTarget& depth()  const { return depth_; }
    const RenderTarget& output() const { return output_; }

private:
    bool createTarget(VkContext& ctx, RenderTarget& rt, VkFormat format, VkExtent2D extent,
                      VkImageUsageFlags usage, VkImageAspectFlags aspect);
    bool createScenePass(VkContext& ctx);
    bool createBlitPass(VkContext& ctx);
    bool createPipelines(VkContext& ctx);

    VkDevice device_ = VK_NULL_HANDLE;

    RenderTarget color_, motion_, depth_, output_;
    VkExtent2D   renderExtent_  = {0, 0};
    VkExtent2D   displayExtent_ = {0, 0};

    VkRenderPass     scenePass_       = VK_NULL_HANDLE;
    VkFramebuffer    sceneFramebuffer_= VK_NULL_HANDLE;
    VkPipelineLayout sceneLayout_     = VK_NULL_HANDLE;
    VkPipeline       scenePipeline_   = VK_NULL_HANDLE;

    VkRenderPass          blitPass_       = VK_NULL_HANDLE;
    VkPipelineLayout      blitLayout_     = VK_NULL_HANDLE;
    VkPipeline            blitPipeline_   = VK_NULL_HANDLE;
    VkDescriptorSetLayout blitSetLayout_  = VK_NULL_HANDLE;
    VkDescriptorPool      blitPool_       = VK_NULL_HANDLE;
    VkDescriptorSet       blitSet_        = VK_NULL_HANDLE;
    VkSampler             sampler_        = VK_NULL_HANDLE;

    // Framebuffers for swapchain images are cached lazily, keyed by view.
    struct BlitFramebuffer { VkImageView view; VkFramebuffer fb; };
    BlitFramebuffer blitFramebuffers_[8] = {};
    uint32_t        blitFramebufferCount_ = 0;
};
