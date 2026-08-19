// Copyright © 2026 Arm Limited.
// SPDX-License-Identifier: MIT
#pragma once

#include <android/log.h>
#include <vulkan/vulkan.h>

#include <cstdlib>

#define ASR_TAG "ArmASR"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  ASR_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  ASR_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, ASR_TAG, __VA_ARGS__)

// Vulkan calls in this sample are checked but never fatal on their own: the
// point of the sample is to report *why* a device cannot run Arm ASR, not to
// abort with no explanation.
#define VK_CHECK(expr)                                                      \
    do {                                                                    \
        VkResult vkCheckResult_ = (expr);                                   \
        if (vkCheckResult_ != VK_SUCCESS) {                                 \
            LOGE("%s:%d: %s failed with VkResult %d", __FILE__, __LINE__,   \
                 #expr, (int)vkCheckResult_);                               \
            return false;                                                   \
        }                                                                   \
    } while (0)
