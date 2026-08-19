// Copyright © 2026 Arm Limited.
// SPDX-License-Identifier: MIT

#include "vk_context.h"

#include <android/native_window.h>
#include <vulkan/vulkan_android.h>

#include <cstring>
#include <string>

namespace {

bool hasExtension(const std::vector<VkExtensionProperties>& list, const char* name)
{
    for (const auto& e : list) {
        if (strcmp(e.extensionName, name) == 0)
            return true;
    }
    return false;
}

} // namespace

bool VkContext::createInstance()
{
    uint32_t loaderVersion = VK_API_VERSION_1_0;
    auto enumerateInstanceVersion = (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(
        VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
    if (enumerateInstanceVersion) {
        VK_CHECK(enumerateInstanceVersion(&loaderVersion));
    }
    LOGI("Vulkan loader reports instance version %u.%u.%u",
         VK_API_VERSION_MAJOR(loaderVersion), VK_API_VERSION_MINOR(loaderVersion),
         VK_API_VERSION_PATCH(loaderVersion));

    VkApplicationInfo appInfo = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "Arm ASR Sample";
    appInfo.apiVersion       = loaderVersion >= VK_API_VERSION_1_2 ? VK_API_VERSION_1_2
                                                                  : VK_API_VERSION_1_1;

    const char* extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };

    VkInstanceCreateInfo ci = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo        = &appInfo;
    ci.enabledExtensionCount   = (uint32_t)(sizeof(extensions) / sizeof(extensions[0]));
    ci.ppEnabledExtensionNames = extensions;

    VK_CHECK(vkCreateInstance(&ci, nullptr, &instance_));
    return true;
}

bool VkContext::createSurface(ANativeWindow* window)
{
    VkAndroidSurfaceCreateInfoKHR ci = {VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    ci.window = window;
    VK_CHECK(vkCreateAndroidSurfaceKHR(instance_, &ci, nullptr, &surface_));
    return true;
}

bool VkContext::pickPhysicalDevice()
{
    uint32_t count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance_, &count, nullptr));
    if (count == 0) {
        LOGE("No Vulkan physical devices");
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    VK_CHECK(vkEnumeratePhysicalDevices(instance_, &count, devices.data()));

    for (VkPhysicalDevice candidate : devices) {
        uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
        std::vector<VkQueueFamilyProperties> props(families);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, props.data());

        for (uint32_t i = 0; i < families; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface_, &present);
            const bool graphics = (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
            const bool compute  = (props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
            if (graphics && compute && present) {
                physicalDevice_ = candidate;
                queueFamily_    = i;
                break;
            }
        }
        if (physicalDevice_ != VK_NULL_HANDLE)
            break;
    }

    if (physicalDevice_ == VK_NULL_HANDLE) {
        LOGE("No physical device with a graphics+compute+present queue");
        return false;
    }

    VkPhysicalDeviceProperties deviceProps = {};
    vkGetPhysicalDeviceProperties(physicalDevice_, &deviceProps);
    LOGI("Using device '%s' (Vulkan %u.%u.%u)", deviceProps.deviceName,
         VK_API_VERSION_MAJOR(deviceProps.apiVersion),
         VK_API_VERSION_MINOR(deviceProps.apiVersion),
         VK_API_VERSION_PATCH(deviceProps.apiVersion));

    // Arm ASR ships prebuilt SPIR-V 1.4 blobs built with -enable-16bit-types.
    // Establish up front whether this device can consume them at all.
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, exts.data());

    caps_.apiVersion       = deviceProps.apiVersion;
    caps_.spirv14          = deviceProps.apiVersion >= VK_API_VERSION_1_2 ||
                             hasExtension(exts, VK_KHR_SPIRV_1_4_EXTENSION_NAME);
    caps_.storage16bit     = deviceProps.apiVersion >= VK_API_VERSION_1_2 ||
                             hasExtension(exts, VK_KHR_16BIT_STORAGE_EXTENSION_NAME);
    caps_.subgroupSizeCtrl = hasExtension(exts, VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME);

    if (hasExtension(exts, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME) ||
        deviceProps.apiVersion >= VK_API_VERSION_1_2) {
        VkPhysicalDeviceShaderFloat16Int8Features f16 = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
        VkPhysicalDeviceFeatures2 features2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features2.pNext = &f16;
        vkGetPhysicalDeviceFeatures2(physicalDevice_, &features2);
        caps_.shaderFloat16 = f16.shaderFloat16 == VK_TRUE;
    }

    LOGI("Arm ASR device gate: SPIR-V 1.4 %s | shaderFloat16 %s | 16-bit storage %s | "
         "subgroup size control %s",
         caps_.spirv14 ? "yes" : "NO", caps_.shaderFloat16 ? "yes" : "NO",
         caps_.storage16bit ? "yes" : "NO", caps_.subgroupSizeCtrl ? "yes" : "no");

    return true;
}

bool VkContext::createDevice()
{
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> available(extCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, available.data());

    std::vector<const char*> enabled;
    enabled.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    // On a Vulkan 1.2 device these are core and must not be requested again.
    const bool core12 = caps_.apiVersion >= VK_API_VERSION_1_2;
    auto tryEnable = [&](const char* name) {
        if (!core12 && hasExtension(available, name))
            enabled.push_back(name);
    };
    tryEnable(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
    tryEnable(VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
    tryEnable(VK_KHR_16BIT_STORAGE_EXTENSION_NAME);
    tryEnable(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
    if (caps_.subgroupSizeCtrl)
        enabled.push_back(VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME);

    VkPhysicalDevice16BitStorageFeatures storage16 = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
    storage16.storageBuffer16BitAccess           = VK_TRUE;
    storage16.uniformAndStorageBuffer16BitAccess = VK_TRUE;

    VkPhysicalDeviceShaderFloat16Int8Features f16 = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
    f16.shaderFloat16 = caps_.shaderFloat16 ? VK_TRUE : VK_FALSE;
    f16.pNext         = &storage16;

    VkPhysicalDeviceFeatures2 features2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features2.pNext = &f16;
    vkGetPhysicalDeviceFeatures2(physicalDevice_, &features2);
    // Keep only what the sample and Arm ASR actually need.
    VkPhysicalDeviceFeatures wanted = {};
    wanted.shaderInt16          = features2.features.shaderInt16;
    wanted.samplerAnisotropy    = features2.features.samplerAnisotropy;
    features2.features          = wanted;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueCi = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueCi.queueFamilyIndex = queueFamily_;
    queueCi.queueCount       = 1;
    queueCi.pQueuePriorities = &priority;

    VkDeviceCreateInfo ci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.pNext                   = &features2;
    ci.queueCreateInfoCount    = 1;
    ci.pQueueCreateInfos       = &queueCi;
    ci.enabledExtensionCount   = (uint32_t)enabled.size();
    ci.ppEnabledExtensionNames = enabled.data();

    VK_CHECK(vkCreateDevice(physicalDevice_, &ci, nullptr, &device_));
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
    return true;
}

bool VkContext::createSwapchain()
{
    VkSurfaceCapabilitiesKHR surfaceCaps = {};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &surfaceCaps));

    uint32_t formatCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount,
                                                  formats.data()));

    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) {
            chosen = f;
            break;
        }
    }

    swapchainExtent_ = surfaceCaps.currentExtent;
    swapchainFormat_ = chosen.format;

    uint32_t imageCount = surfaceCaps.minImageCount + 1;
    if (surfaceCaps.maxImageCount > 0 && imageCount > surfaceCaps.maxImageCount)
        imageCount = surfaceCaps.maxImageCount;

    VkSwapchainCreateInfoKHR ci = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface          = surface_;
    ci.minImageCount    = imageCount;
    ci.imageFormat      = chosen.format;
    ci.imageColorSpace  = chosen.colorSpace;
    ci.imageExtent      = swapchainExtent_;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform     = surfaceCaps.currentTransform;
    ci.compositeAlpha   = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    ci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped          = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_));

    uint32_t count = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr));
    swapchainImages_.resize(count);
    VK_CHECK(vkGetSwapchainImagesKHR(device_, swapchain_, &count, swapchainImages_.data()));

    swapchainImageViews_.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo viewCi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewCi.image                       = swapchainImages_[i];
        viewCi.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
        viewCi.format                      = swapchainFormat_;
        viewCi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewCi.subresourceRange.levelCount = 1;
        viewCi.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(device_, &viewCi, nullptr, &swapchainImageViews_[i]));
    }

    LOGI("Swapchain: %ux%u, %u images, format %d", swapchainExtent_.width,
         swapchainExtent_.height, count, (int)swapchainFormat_);
    return true;
}

void VkContext::destroySwapchain()
{
    for (VkImageView view : swapchainImageViews_)
        vkDestroyImageView(device_, view, nullptr);
    swapchainImageViews_.clear();
    swapchainImages_.clear();

    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

bool VkContext::createFrameResources()
{
    VkCommandPoolCreateInfo poolCi = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolCi.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolCi.queueFamilyIndex = queueFamily_;
    VK_CHECK(vkCreateCommandPool(device_, &poolCi, nullptr, &commandPool_));

    VkCommandBufferAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool        = commandPool_;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = kFramesInFlight;
    VK_CHECK(vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_));

    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        VkSemaphoreCreateInfo semCi = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(device_, &semCi, nullptr, &acquireSemaphores_[i]));
        VK_CHECK(vkCreateSemaphore(device_, &semCi, nullptr, &releaseSemaphores_[i]));

        VkFenceCreateInfo fenceCi = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceCi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(device_, &fenceCi, nullptr, &inFlightFences_[i]));
    }
    return true;
}

bool VkContext::init(ANativeWindow* window)
{
    return createInstance() && createSurface(window) && pickPhysicalDevice() &&
           createDevice() && createSwapchain() && createFrameResources();
}

bool VkContext::recreateSwapchain(ANativeWindow*)
{
    VK_CHECK(vkDeviceWaitIdle(device_));
    destroySwapchain();
    return createSwapchain();
}

bool VkContext::beginFrame(uint32_t* outImageIndex)
{
    VK_CHECK(vkWaitForFences(device_, 1, &inFlightFences_[frame_], VK_TRUE, UINT64_MAX));

    VkResult acquired = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                              acquireSemaphores_[frame_], VK_NULL_HANDLE,
                                              outImageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR || acquired == VK_SUBOPTIMAL_KHR)
        return false;
    if (acquired != VK_SUCCESS) {
        LOGE("vkAcquireNextImageKHR failed: %d", (int)acquired);
        return false;
    }

    VK_CHECK(vkResetFences(device_, 1, &inFlightFences_[frame_]));
    VK_CHECK(vkResetCommandBuffer(commandBuffers_[frame_], 0));

    VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(commandBuffers_[frame_], &beginInfo));
    return true;
}

bool VkContext::endFrame(uint32_t imageIndex)
{
    VK_CHECK(vkEndCommandBuffer(commandBuffers_[frame_]));

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &acquireSemaphores_[frame_];
    submit.pWaitDstStageMask    = &waitStage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &commandBuffers_[frame_];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &releaseSemaphores_[frame_];
    VK_CHECK(vkQueueSubmit(queue_, 1, &submit, inFlightFences_[frame_]));

    VkPresentInfoKHR present = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = &releaseSemaphores_[frame_];
    present.swapchainCount     = 1;
    present.pSwapchains        = &swapchain_;
    present.pImageIndices      = &imageIndex;

    VkResult presented = vkQueuePresentKHR(queue_, &present);
    frame_ = (frame_ + 1) % kFramesInFlight;

    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR)
        return false;
    if (presented != VK_SUCCESS) {
        LOGE("vkQueuePresentKHR failed: %d", (int)presented);
        return false;
    }
    return true;
}

uint32_t VkContext::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const
{
    VkPhysicalDeviceMemoryProperties memProps = {};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    return UINT32_MAX;
}

bool VkContext::waitIdle() const
{
    VK_CHECK(vkDeviceWaitIdle(device_));
    return true;
}

void VkContext::destroy()
{
    if (device_ == VK_NULL_HANDLE)
        return;

    vkDeviceWaitIdle(device_);

    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        vkDestroySemaphore(device_, acquireSemaphores_[i], nullptr);
        vkDestroySemaphore(device_, releaseSemaphores_[i], nullptr);
        vkDestroyFence(device_, inFlightFences_[i], nullptr);
    }
    vkDestroyCommandPool(device_, commandPool_, nullptr);
    destroySwapchain();
    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;

    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
}
