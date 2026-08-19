// Copyright © 2026 Arm Limited.
// SPDX-License-Identifier: MIT
#pragma once

#include "common.h"

#include <cstdint>
#include <vector>

struct ANativeWindow;

// Everything Arm ASR needs to know about the device it landed on. Checked once
// at startup and logged, because the prebuilt SPIR-V blobs shipped with the
// library are SPIR-V 1.4 with explicit 16-bit types and there is no fp32
// fallback permutation set.
struct DeviceCapabilities {
    uint32_t apiVersion        = 0;
    bool     spirv14           = false;  // Vulkan 1.2, or 1.1 + VK_KHR_spirv_1_4
    bool     shaderFloat16     = false;  // VK_KHR_shader_float16_int8
    bool     storage16bit      = false;  // VK_KHR_16bit_storage
    bool     subgroupSizeCtrl  = false;  // VK_EXT_subgroup_size_control (optional)

    bool sufficientForArmAsr() const { return spirv14 && shaderFloat16 && storage16bit; }
};

class VkContext {
public:
    static constexpr uint32_t kFramesInFlight = 2;

    bool init(ANativeWindow* window);
    void destroy();

    // Acquires the next swapchain image and begins this frame's command buffer.
    // Returns false when the swapchain needs recreating or acquisition failed.
    bool beginFrame(uint32_t* outImageIndex);
    bool endFrame(uint32_t imageIndex);

    bool recreateSwapchain(ANativeWindow* window);

    VkInstance       instance()       const { return instance_; }
    VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    VkDevice         device()         const { return device_; }
    VkQueue          queue()          const { return queue_; }
    uint32_t         queueFamily()    const { return queueFamily_; }
    VkCommandBuffer  commandBuffer()  const { return commandBuffers_[frame_]; }
    VkExtent2D       swapchainExtent()const { return swapchainExtent_; }
    VkFormat         swapchainFormat()const { return swapchainFormat_; }
    VkImage          swapchainImage(uint32_t i)     const { return swapchainImages_[i]; }
    VkImageView      swapchainImageView(uint32_t i) const { return swapchainImageViews_[i]; }
    uint32_t         swapchainImageCount()          const { return (uint32_t)swapchainImages_.size(); }
    const DeviceCapabilities& caps() const { return caps_; }

    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
    bool     waitIdle() const;

private:
    bool createInstance();
    bool createSurface(ANativeWindow* window);
    bool pickPhysicalDevice();
    bool createDevice();
    bool createSwapchain();
    void destroySwapchain();
    bool createFrameResources();

    VkInstance       instance_       = VK_NULL_HANDLE;
    VkSurfaceKHR     surface_        = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         device_         = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;

    VkSwapchainKHR           swapchain_       = VK_NULL_HANDLE;
    VkFormat                 swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D               swapchainExtent_ = {0, 0};
    std::vector<VkImage>     swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;

    VkCommandPool   commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffers_[kFramesInFlight] = {};
    VkSemaphore     acquireSemaphores_[kFramesInFlight] = {};
    VkSemaphore     releaseSemaphores_[kFramesInFlight] = {};
    VkFence         inFlightFences_[kFramesInFlight] = {};
    uint32_t        frame_ = 0;

    DeviceCapabilities caps_;
};
