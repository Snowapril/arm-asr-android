// Copyright © 2026 Arm Limited.
// SPDX-License-Identifier: MIT
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

// With FFXM_VKLOADER_VOLK=OFF, include/host/backends/vk/ffxm_vk.h includes
// <vulkan/vulkan.h> plainly and never defines a platform surface macro - it
// only does that on the volk path. So the sample has to ask for the Win32
// surface itself, and it must happen before any vulkan.h include.
#ifndef VK_USE_PLATFORM_WIN32_KHR
#	define VK_USE_PLATFORM_WIN32_KHR 1
#endif

// Ask for the wide Win32 API. The root CMakeLists sets UNICODE/_UNICODE, but
// only for the Arm ASR subtree - add_compile_definitions() is directory-scoped
// and does not reach this target. Without it the resource-id macros such as
// IDC_ARROW expand to LPSTR and will not pass to the ...W entry points.
#ifndef UNICODE
#	define UNICODE 1
#endif
#ifndef _UNICODE
#	define _UNICODE 1
#endif

#include <windows.h>

// winspool.h, which windows.h pulls in, declares DeviceCapabilitiesA/W for the
// printing API and macro-maps the bare name onto one of them. That collides
// with VkContext's DeviceCapabilities struct. This sample uses no printing API,
// so drop the macro and keep the type name shared with the Android sample.
#undef DeviceCapabilities

#include <vulkan/vulkan.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#define ASR_TAG "ArmASR"

enum class AsrLogLevel { Info, Warn, Error };

// printf-style so the LOGI/LOGW/LOGE call sites shared with the Android sample
// compile unchanged. Output goes to the console and, so it is visible when
// running under the Visual Studio debugger, to OutputDebugStringA as well.
inline void asrLog(AsrLogLevel level, const char* fmt, ...)
{
    char body[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);

    const char* prefix = level == AsrLogLevel::Error   ? ASR_TAG "/E: "
                         : level == AsrLogLevel::Warn  ? ASR_TAG "/W: "
                                                       : ASR_TAG "/I: ";

    char line[600];
    snprintf(line, sizeof(line), "%s%s\n", prefix, body);

    fputs(line, level == AsrLogLevel::Info ? stdout : stderr);
    fflush(level == AsrLogLevel::Info ? stdout : stderr);
    OutputDebugStringA(line);
}

#define LOGI(...) asrLog(AsrLogLevel::Info,  __VA_ARGS__)
#define LOGW(...) asrLog(AsrLogLevel::Warn,  __VA_ARGS__)
#define LOGE(...) asrLog(AsrLogLevel::Error, __VA_ARGS__)

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
