// Copyright © 2026 Arm Limited.
// SPDX-License-Identifier: MIT
//
// Minimal Win32 host for the Arm ASR Vulkan sample. Renders a procedural scene
// at a reduced resolution, upscales it with Arm ASR, and presents the result.

#include "asr_upscaler.h"
#include "common.h"
#include "scene.h"
#include "vk_context.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace {

// A 1080p swapchain gives a clean 960x540 render extent at the 2x ratio, which
// is what the Android sample uses; the fallback keeps the window on-screen for
// smaller desktops.
constexpr int kPreferredWidth   = 1920;
constexpr int kPreferredHeight  = 1080;
constexpr int kFallbackWidth    = 1280;
constexpr int kFallbackHeight   = 720;

struct App {
    VkContext   ctx;
    Scene       scene;
    AsrUpscaler upscaler;

    HWND     hwnd          = nullptr;
    bool     vulkanReady   = false;
    bool     renderReady   = false;
    bool     validation    = false;
    bool     quit          = false;
    bool     swapchainDirty = false;
    int32_t  frameIndex    = 0;

    VkExtent2D renderExtent  = {0, 0};
    VkExtent2D displayExtent = {0, 0};

    float camZ     = 0.0f;
    float camZPrev = 0.0f;
    double lastTimeSeconds = 0.0;
};

double nowSeconds()
{
    using clock = std::chrono::steady_clock;
    static const clock::time_point origin = clock::now();
    return std::chrono::duration<double>(clock::now() - origin).count();
}

VkExtent2D clientExtent(HWND hwnd)
{
    RECT client = {};
    GetClientRect(hwnd, &client);
    return {(uint32_t)(client.right - client.left), (uint32_t)(client.bottom - client.top)};
}

// Everything that depends on the swapchain size: the render/display extents,
// the scene's G-buffer, and the Arm ASR context.
bool createSizedResources(App& app)
{
    app.displayExtent = app.ctx.swapchainExtent();

    // 2x per-dimension upscaling: a 1080p window is fed from 960x540.
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
    app.frameIndex = 0;   // the next dispatch gets reset = true
    return true;
}

void destroySizedResources(App& app)
{
    app.ctx.waitIdle();
    app.upscaler.destroy();
    app.scene.destroy(app.ctx);
}

bool initRendering(App& app)
{
    if (!app.ctx.init(app.hwnd, app.validation)) {
        LOGE("Vulkan initialisation failed");
        return false;
    }
    app.vulkanReady = true;

    const DeviceCapabilities& caps = app.ctx.caps();
    if (!caps.sufficientForArmAsr()) {
        LOGE("This GPU cannot run Arm ASR's prebuilt shaders.");
        LOGE("  They are SPIR-V 1.4 with explicit 16-bit types, and there is no");
        LOGE("  fp32 permutation set. Missing:%s%s%s",
             caps.spirv14 ? "" : " [SPIR-V 1.4 / Vulkan 1.2]",
             caps.shaderFloat16 ? "" : " [shaderFloat16]",
             caps.storage16bit ? "" : " [16-bit storage]");
        LOGE("  Most discrete and recent integrated GPUs support all three;");
        LOGE("  try updating the graphics driver. Software rasterisers such as");
        LOGE("  lavapipe or SwiftShader do not advertise shaderFloat16.");
        return false;
    }

    if (!createSizedResources(app))
        return false;

    app.lastTimeSeconds  = nowSeconds();
    app.renderReady      = true;
    app.swapchainDirty   = false;   // absorb the WM_SIZE from window creation
    LOGI("Arm ASR sample ready. Press ESC to quit.");
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

// The swapchain, the scene G-buffer and the Arm ASR context are all sized from
// the window, so a resize tears down and rebuilds all three.
//
// This deliberately rebuilds unconditionally rather than comparing the client
// rect against the current extent. Skipping on "same size" would stall forever
// when the driver reports OUT_OF_DATE without a size change, and could loop
// every frame if the surface clamps currentExtent away from the client rect.
void rebuildSwapchainResources(App& app)
{
    const VkExtent2D client = clientExtent(app.hwnd);
    if (client.width == 0 || client.height == 0)
        return;   // minimised: stay dirty and retry once the window is restored

    app.swapchainDirty = false;

    destroySizedResources(app);
    app.renderReady = false;

    if (!app.ctx.recreateSwapchain(app.hwnd)) {
        LOGE("Swapchain recreation failed");
        return;
    }
    if (!createSizedResources(app)) {
        LOGE("Could not rebuild resources after resize");
        return;
    }
    app.lastTimeSeconds = nowSeconds();
    app.renderReady     = true;
}

void drawFrame(App& app)
{
    if (!app.renderReady)
        return;

    const double now   = nowSeconds();
    float        delta = (float)(now - app.lastTimeSeconds);
    app.lastTimeSeconds = now;
    delta = std::min(std::max(delta, 1.0f / 240.0f), 1.0f / 15.0f);

    app.camZPrev = app.camZ;
    app.camZ -= 6.0f * delta;   // fly forward

    uint32_t imageIndex = 0;
    if (!app.ctx.beginFrame(&imageIndex)) {
        app.swapchainDirty = true;   // OUT_OF_DATE / SUBOPTIMAL
        return;
    }

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

    if (!app.ctx.endFrame(imageIndex))
        app.swapchainDirty = true;

    if (app.frameIndex == 0)
        LOGI("First frame dispatched successfully");
    ++app.frameIndex;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!app)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_CLOSE:
        app->quit = true;
        return 0;
    case WM_DESTROY:
        app->quit = true;
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            app->quit = true;
            return 0;
        }
        break;
    case WM_SIZE:
        // Never rebuild Vulkan objects inside the message handler; the loop
        // does it once the pump has drained.
        app->swapchainDirty = true;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND createWindow(App& app)
{
    const wchar_t* kClassName = L"ArmAsrSampleWindow";

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = wndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) {
        LOGE("RegisterClassExW failed (%lu)", GetLastError());
        return nullptr;
    }

    // Pick a client size that fits the desktop work area.
    RECT work = {};
    int  width = kPreferredWidth, height = kPreferredHeight;
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        const int availW = work.right - work.left;
        const int availH = work.bottom - work.top;
        if (availW < kPreferredWidth || availH < kPreferredHeight) {
            width  = kFallbackWidth;
            height = kFallbackHeight;
            LOGI("Work area is %dx%d; using a %dx%d client area", availW, availH, width, height);
        }
    }

    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rect = {0, 0, width, height};
    AdjustWindowRectEx(&rect, style, FALSE, 0);   // size the *client* area

    HWND hwnd = CreateWindowExW(0, kClassName, L"Arm ASR Vulkan sample", style,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                rect.right - rect.left, rect.bottom - rect.top,
                                nullptr, nullptr, wc.hInstance, &app);
    if (!hwnd) {
        LOGE("CreateWindowExW failed (%lu)", GetLastError());
        return nullptr;
    }
    ShowWindow(hwnd, SW_SHOW);
    return hwnd;
}

bool validationRequested(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--validation") == 0)
            return true;
    }
    char buffer[16] = {};
    if (GetEnvironmentVariableA("ASR_SAMPLE_VALIDATION", buffer, sizeof(buffer)) > 0)
        return strcmp(buffer, "0") != 0;
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    App app;
    app.validation = validationRequested(argc, argv);

    app.hwnd = createWindow(app);
    if (!app.hwnd)
        return 1;

    if (!initRendering(app)) {
        teardownRendering(app);
        DestroyWindow(app.hwnd);
        return 1;
    }

    while (!app.quit) {
        MSG msg = {};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                app.quit = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (app.quit)
            break;

        if (app.swapchainDirty)
            rebuildSwapchainResources(app);

        const VkExtent2D client = clientExtent(app.hwnd);
        if (client.width == 0 || client.height == 0) {
            Sleep(16);   // minimised: idle rather than spin
            continue;
        }

        drawFrame(app);
    }

    teardownRendering(app);
    DestroyWindow(app.hwnd);
    LOGI("Exited cleanly");
    return 0;
}
