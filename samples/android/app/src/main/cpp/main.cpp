// Copyright © 2026 Arm Limited.
// SPDX-License-Identifier: MIT
//
// Minimal NativeActivity host for the Arm ASR Vulkan sample. Renders a
// procedural scene at a reduced resolution, upscales it with Arm ASR, and
// presents the result.

#include "asr_upscaler.h"
#include "common.h"
#include "scene.h"
#include "vk_context.h"

#include <android_native_app_glue.h>

#include <cmath>
#include <ctime>

namespace {

struct App {
    VkContext   ctx;
    Scene       scene;
    AsrUpscaler upscaler;

    bool     vulkanReady = false;
    bool     renderReady = false;
    int32_t  frameIndex  = 0;

    VkExtent2D renderExtent  = {0, 0};
    VkExtent2D displayExtent = {0, 0};

    float camZ     = 0.0f;
    float camZPrev = 0.0f;
    double lastTimeSeconds = 0.0;
};

double nowSeconds()
{
    timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

bool initRendering(App& app, ANativeWindow* window)
{
    if (!app.ctx.init(window)) {
        LOGE("Vulkan initialisation failed");
        return false;
    }
    app.vulkanReady = true;

    const DeviceCapabilities& caps = app.ctx.caps();
    if (!caps.sufficientForArmAsr()) {
        LOGE("This device cannot run Arm ASR's prebuilt shaders.");
        LOGE("  They are SPIR-V 1.4 with explicit 16-bit types, and there is no");
        LOGE("  fp32 permutation set. Missing:%s%s%s",
             caps.spirv14 ? "" : " [SPIR-V 1.4 / Vulkan 1.2]",
             caps.shaderFloat16 ? "" : " [shaderFloat16]",
             caps.storage16bit ? "" : " [16-bit storage]");
        LOGE("  The x86_64 emulator (SwiftShader) is expected to fail here.");
        return false;
    }

    app.displayExtent = app.ctx.swapchainExtent();

    // 2x per-dimension upscaling: a 1080p panel is fed from 960x540.
    const float ratio = arm::ffxmFsr2GetUpscaleRatioFactor(arm::FFXM_FSR2_UPSCALING_RATIO_X2);
    app.renderExtent  = {(uint32_t)(app.displayExtent.width / ratio),
                         (uint32_t)(app.displayExtent.height / ratio)};
    LOGI("Rendering at %ux%u, presenting at %ux%u (ratio %.2f)", app.renderExtent.width,
         app.renderExtent.height, app.displayExtent.width, app.displayExtent.height, ratio);

    if (!app.scene.init(app.ctx, app.renderExtent, app.displayExtent)) {
        LOGE("Scene initialisation failed");
        return false;
    }
    if (!app.upscaler.init(app.ctx, app.renderExtent, app.displayExtent,
                           arm::FFXM_FSR2_SHADER_QUALITY_MODE_QUALITY)) {
        LOGE("Arm ASR initialisation failed");
        return false;
    }

    app.lastTimeSeconds = nowSeconds();
    app.renderReady     = true;
    LOGI("Arm ASR sample ready");
    return true;
}

void teardownRendering(App& app)
{
    if (app.vulkanReady)
        app.ctx.waitIdle();
    app.upscaler.destroy();
    if (app.vulkanReady)
        app.scene.destroy(app.ctx);
    app.ctx.destroy();
    app.vulkanReady = false;
    app.renderReady = false;
}

void drawFrame(App& app)
{
    if (!app.renderReady)
        return;

    const double now   = nowSeconds();
    float        delta = (float)(now - app.lastTimeSeconds);
    app.lastTimeSeconds = now;
    delta = fminf(fmaxf(delta, 1.0f / 240.0f), 1.0f / 15.0f);

    app.camZPrev = app.camZ;
    app.camZ -= 6.0f * delta;   // fly forward

    uint32_t imageIndex = 0;
    if (!app.ctx.beginFrame(&imageIndex))
        return;

    VkCommandBuffer cmd = app.ctx.commandBuffer();

    float jitterX = 0.0f, jitterY = 0.0f;
    app.upscaler.jitterForFrame(app.frameIndex, &jitterX, &jitterY);

    ScenePushConstants pc = {};
    pc.renderSize[0] = (float)app.renderExtent.width;
    pc.renderSize[1] = (float)app.renderExtent.height;
    pc.jitter[0]     = jitterX;
    pc.jitter[1]     = jitterY;
    pc.camZ          = app.camZ;
    pc.camZPrev      = app.camZPrev;
    pc.tanHalfFov    = tanf(1.0471975512f * 0.5f);
    pc.aspect        = (float)app.renderExtent.width / (float)app.renderExtent.height;
    pc.nearZ         = 0.1f;
    pc.farZ          = 200.0f;
    pc.planeY        = -2.0f;
    pc.time          = (float)now;

    app.scene.record(cmd, pc);
    app.scene.barrierForUpscale(cmd);

    app.upscaler.dispatch(cmd, app.scene, app.renderExtent, delta * 1000.0f, app.frameIndex,
                          app.frameIndex == 0);

    app.scene.recordBlit(cmd, app.ctx.swapchainImageView(imageIndex),
                         app.ctx.swapchainExtent());

    app.ctx.endFrame(imageIndex);

    if (app.frameIndex == 0)
        LOGI("First frame dispatched successfully");
    ++app.frameIndex;
}

void handleCommand(android_app* androidApp, int32_t cmd)
{
    App& app = *static_cast<App*>(androidApp->userData);

    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (androidApp->window && !app.vulkanReady) {
            if (!initRendering(app, androidApp->window))
                teardownRendering(app);
        }
        break;
    case APP_CMD_TERM_WINDOW:
        teardownRendering(app);
        break;
    default:
        break;
    }
}

} // namespace

void android_main(android_app* androidApp)
{
    App app;
    androidApp->userData     = &app;
    androidApp->onAppCmd     = handleCommand;

    while (true) {
        int                   events = 0;
        android_poll_source*  source = nullptr;

        while (ALooper_pollOnce(app.renderReady ? 0 : -1, nullptr, &events,
                                reinterpret_cast<void**>(&source)) >= 0) {
            if (source)
                source->process(androidApp, source);
            if (androidApp->destroyRequested) {
                teardownRendering(app);
                return;
            }
        }

        drawFrame(app);
    }
}
