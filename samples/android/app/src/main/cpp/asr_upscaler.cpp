// Copyright © 2026 Arm Limited.
// SPDX-License-Identifier: MIT

#include "asr_upscaler.h"

#include <cstring>

using namespace arm;

namespace {

// Arm ASR takes resources as FfxmResource, which pairs the API handle with a
// description the backend uses to derive views and barriers.
FfxmResourceDescription describeTexture(const RenderTarget& rt, FfxmSurfaceFormat format,
                                        FfxmResourceUsage usage)
{
    FfxmResourceDescription desc = {};
    desc.type     = FFXM_RESOURCE_TYPE_TEXTURE2D;
    desc.format   = format;
    desc.width    = rt.extent.width;
    desc.height   = rt.extent.height;
    desc.depth    = 1;
    desc.mipCount = 1;
    desc.flags    = FFXM_RESOURCE_FLAGS_NONE;
    desc.usage    = usage;
    return desc;
}

FfxmResource wrap(const RenderTarget& rt, FfxmSurfaceFormat format, FfxmResourceUsage usage,
                  FfxmResourceStates state, wchar_t* name)
{
    return ffxmGetResourceVK(reinterpret_cast<void*>(rt.image),
                             describeTexture(rt, format, usage), name, state);
}

void messageCallback(FfxmMsgType type, const wchar_t* message)
{
    // The runtime hands back wide strings; the names are ASCII so a byte-wise
    // narrowing is exact here.
    char buffer[512];
    size_t i = 0;
    for (; message && message[i] != L'\0' && i < sizeof(buffer) - 1; ++i)
        buffer[i] = (message[i] > 0 && message[i] < 128) ? (char)message[i] : '?';
    buffer[i] = '\0';

    if (type == FFXM_MESSAGE_TYPE_ERROR)
        LOGE("Arm ASR: %s", buffer);
    else
        LOGW("Arm ASR: %s", buffer);
}

} // namespace

uint32_t AsrUpscaler::initializationFlags()
{
    // The sample renders motion vectors at render resolution, without jitter
    // baked in, and writes a conventional (non-inverted) depth buffer.
    return FFXM_FSR2_ENABLE_AUTO_EXPOSURE | FFXM_FSR2_ENABLE_DEBUG_CHECKING;
}

bool AsrUpscaler::init(VkContext& ctx, VkExtent2D renderExtent, VkExtent2D displayExtent,
                       FfxmFsr2ShaderQualityMode quality)
{
    device_        = ctx.device();
    renderExtent_  = renderExtent;
    displayExtent_ = displayExtent;

    // 1. Scratch memory. Arm ASR allocates everything out of this buffer; note
    //    it is sizeof-based, so it is larger on Android than on Windows because
    //    wchar_t is 4 bytes rather than 2.
    const size_t maxContexts   = 1;
    const size_t scratchSize   = ffxmGetScratchMemorySizeVK(ctx.physicalDevice(), maxContexts);
    scratchBuffer_.resize(scratchSize);
    LOGI("Arm ASR scratch buffer: %zu bytes", scratchSize);

    // 2. Backend device + interface.
    VkDeviceContext deviceContext = {};
    deviceContext.vkDevice         = ctx.device();
    deviceContext.vkPhysicalDevice = ctx.physicalDevice();
    deviceContext.vkDeviceProcAddr = vkGetDeviceProcAddr;

    FfxmDevice ffxmDevice = ffxmGetDeviceVK(&deviceContext);

    FfxmErrorCode err = ffxmGetInterfaceVK(&interface_, ffxmDevice, scratchBuffer_.data(),
                                           scratchBuffer_.size(), maxContexts);
    if (err != FFXM_OK) {
        LOGE("ffxmGetInterfaceVK failed: 0x%08X", (unsigned)err);
        return false;
    }

    // 3. Context.
    FfxmFsr2ContextDescription contextDesc = {};
    contextDesc.qualityMode      = quality;
    contextDesc.flags            = initializationFlags();
    contextDesc.maxRenderSize    = {renderExtent.width, renderExtent.height};
    contextDesc.displaySize      = {displayExtent.width, displayExtent.height};
    contextDesc.backendInterface = interface_;
    contextDesc.fpMessage        = messageCallback;

    err = ffxmFsr2ContextCreate(&context_, &contextDesc);
    if (err != FFXM_OK) {
        LOGE("ffxmFsr2ContextCreate failed: 0x%08X", (unsigned)err);
        return false;
    }
    contextCreated_ = true;

    jitterPhaseCount_ = ffxmFsr2GetJitterPhaseCount((int32_t)renderExtent.width,
                                                    (int32_t)displayExtent.width);
    LOGI("Arm ASR context created: %ux%u -> %ux%u, jitter phase count %d",
         renderExtent.width, renderExtent.height, displayExtent.width, displayExtent.height,
         jitterPhaseCount_);
    return true;
}

void AsrUpscaler::jitterForFrame(int32_t frameIndex, float* outX, float* outY) const
{
    *outX = 0.0f;
    *outY = 0.0f;
    if (jitterPhaseCount_ > 0)
        ffxmFsr2GetJitterOffset(outX, outY, frameIndex % jitterPhaseCount_, jitterPhaseCount_);
}

bool AsrUpscaler::dispatch(VkCommandBuffer cmd, const Scene& scene, VkExtent2D renderExtent,
                           float frameTimeMs, int32_t frameIndex, bool reset)
{
    if (!contextCreated_)
        return false;

    float jitterX = 0.0f, jitterY = 0.0f;
    jitterForFrame(frameIndex, &jitterX, &jitterY);

    static wchar_t kColorName[]  = L"ASR_Color";
    static wchar_t kDepthName[]  = L"ASR_Depth";
    static wchar_t kMotionName[] = L"ASR_MotionVectors";
    static wchar_t kOutputName[] = L"ASR_Output";

    FfxmFsr2DispatchDescription desc = {};
    desc.commandList = ffxmGetCommandListVK(cmd);
    desc.color       = wrap(scene.color(), FFXM_SURFACE_FORMAT_R16G16B16A16_FLOAT,
                            FFXM_RESOURCE_USAGE_RENDERTARGET, FFXM_RESOURCE_STATE_COMPUTE_READ,
                            kColorName);
    desc.depth       = wrap(scene.depth(), FFXM_SURFACE_FORMAT_R32_FLOAT,
                            FFXM_RESOURCE_USAGE_DEPTHTARGET, FFXM_RESOURCE_STATE_COMPUTE_READ,
                            kDepthName);
    desc.motionVectors = wrap(scene.motion(), FFXM_SURFACE_FORMAT_R16G16_FLOAT,
                              FFXM_RESOURCE_USAGE_RENDERTARGET,
                              FFXM_RESOURCE_STATE_COMPUTE_READ, kMotionName);
    desc.output      = wrap(scene.output(), FFXM_SURFACE_FORMAT_R16G16B16A16_FLOAT,
                            FFXM_RESOURCE_USAGE_UAV, FFXM_RESOURCE_STATE_UNORDERED_ACCESS,
                            kOutputName);

    desc.jitterOffset = {jitterX, jitterY};
    // The scene shader already emits motion vectors in render-resolution pixels,
    // so no rescaling is needed here.
    desc.motionVectorScale        = {1.0f, 1.0f};
    desc.renderSize               = {renderExtent.width, renderExtent.height};
    desc.enableSharpening         = true;
    desc.sharpness                = 0.4f;
    desc.frameTimeDelta           = frameTimeMs;
    desc.preExposure              = 1.0f;
    desc.reset                    = reset;
    desc.cameraNear               = 0.1f;
    desc.cameraFar                = 200.0f;
    desc.cameraFovAngleVertical   = 1.0471975512f;  // 60 degrees
    desc.viewSpaceToMetersFactor  = 1.0f;

    FfxmErrorCode err = ffxmFsr2ContextDispatch(&context_, &desc);
    if (err != FFXM_OK) {
        LOGE("ffxmFsr2ContextDispatch failed: 0x%08X", (unsigned)err);
        return false;
    }
    return true;
}

void AsrUpscaler::destroy()
{
    if (contextCreated_) {
        // The GPU must be idle before tearing the context down.
        vkDeviceWaitIdle(device_);
        ffxmFsr2ContextDestroy(&context_);
        contextCreated_ = false;
    }
    scratchBuffer_.clear();
}
